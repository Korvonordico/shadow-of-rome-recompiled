// Skeleton rules (sor-cattivo, 16/11/2026): every frame, cheap checks on the bones of every character the game
// keeps in its actor lists, to catch a body that breaks apart ("Octavius turned into a chopped up ball").
// On only with the sentinel switch (sor_blackbox.cpp: SOR_SENTINEL, sentinel= in settings.ini, sentinel.on);
// SOR_SKELETON=0 turns only these rules off.
//
// Where the game keeps the bones (from the DWARF debug info of the European executable, checked in memory):
//   WORK_MAN 0x4E69E0: lists of {next, actor} nodes, +0 player, +4 NPCs, +8 enemies and allies, +0x10 weapons,
//   +0x20 objects. Actor + 0x50 = classOBJ_MODEL::mModel (classMODEL_BASE); actor + 0x90 = position.
//   classMODEL_BASE: +0x6a mJnt_num (u16), +0x80 mpJnt (_JNT_WK, 0xE0 bytes each), +0x90 mpWmat (one 4x4 world
//   matrix per joint, 0x40 bytes, translation at +0x30). _JNT_WK: +0 Lmat (local matrix, translation at +0x30 =
//   offset from the parent), +0xd5 nOya (parent joint, 0xFF for the root).
//
// Rules, each must hold for 3 game steps in a row before it counts (the game thread may be writing while we read).
// Checked only while the game's logic runs (its pad reads move), and from scratch after every area or scene change
// and for the first 30 steps of a new model (loads build the bodies a piece at a time):
//   number:  every value of a world matrix is a real number and smaller than 1e6;
//   length:  characters only (weapons and objects stretch on purpose: a spear 21 -> 95 units, a prop 0 -> 160).
//            A bone is "out" when the distance between a joint and its parent moves more than max(50%, 6 units) from
//            its first value (scaled by how much the joint's own local offset changed, and by how much the parent's
//            world scale changed: sor-giro, enemy 00350207, areas 0c4/045). One bone out alone is normal:
//            the end joints of the bodies (hands, props, joints 23-27 of the 28-joint enemies) stretch by animation,
//            up to about x2 (the author's games of 16/11 19:01-20:06: 12.8 -> 4.8, 24.2 -> 40.2, 9.3 -> 19.0 units).
//            It counts when bones of 3 or more different limbs (chains of joints between branchings) of one body are
//            out at the same time (a body coming apart; several bones of one chain stretch together in a normal fight,
//            enemy 00350207 joints 23-27, sor-giro), or when one
//            bone is far out: more than max(2x its length, 40 units) away from it (a limb thrown off the body);
//   scale:   each axis of a joint's world matrix keeps its first length within x0.5-x2 (a squashed or blown up part);
//   far:     no joint is farther from the actor's position than max(4x the farthest seen at the start, 800 units);
//            weapons and objects max(8x, 5000 units): they fly and swing away from their actor position on purpose
//            (16/11: an object 007b0807 at 2864 units, a weapon 000d0407 at 817).
// A break writes "[skeleton] BROKEN ..." in the black box log, naming the list, the actor, the joint and its parent,
// and saves a black box folder "skeleton" (at most one every 60 s). The same body is reported again after 10 s.
#include "ps2_runtime.h"
#include "blackbox/blackbox.h"
#include "sor_indirizzi.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern std::atomic<uint64_t> g_sorContaPad; // Stubs/Pad.cpp: pad reads, they stop if the game's logic stops

namespace
{
    const int kMaxJoints = 128, kMaxActors = 96, kNeed = 3, kSettle = 30;
    const char *const kListName[5] = {"player", "npc", "enemy", "weapon", "object"};
    const uint32_t kListOffset[5] = {0x0, 0x4, 0x8, 0x10, 0x20};

    struct Track
    {
        uint32_t actor = 0, type = 0, jnt = 0, wmat = 0;
        int joints = 0, list = 0;
        uint64_t seen = 0, lastStep = 0;
        bool based = false;
        float len[kMaxJoints];      // first world length of the bone to the parent (-1: root or no parent)
        float local[kMaxJoints];    // first length of the joint's local offset
        float scale[kMaxJoints][3]; // first length of each axis of the world matrix
        float farBase = 0.f;
        float lastFar = 0.f; // the farthest joint at the step before (weapons and objects: a jump, not a walk away)
        float lastSig = 0.f; // sum of the joint positions at the last check: unchanged = the bones were not computed again
        bool haveSig = false;
        uint8_t parent[kMaxJoints];
        uint8_t limb[kMaxJoints]; // the first joint of the chain this joint is on (a chain ends at a joint with 2+ children)
        bool elastic[kMaxJoints]; // by limb: seen with 3+ bones out at once (a stretching chain, not a detached part)
        uint8_t badNumber = 0, badFar = 0, badMany = 0, badManyRaw = 0;
        uint8_t badLen[kMaxJoints], badScale[kMaxJoints];
        bool told = false;
        uint64_t toldAt = 0, notedAt = 0;
    };

    bool g_on = false;
    Track g_tracks[kMaxActors];
    uint64_t g_step = 0, g_lastPad = 0;
    uint32_t g_lastWhere = 0xFFFFFFFFu;
    double g_lastSave = -1e9;
    std::atomic<int> g_testBone{0};
    std::atomic<int> g_testExplode{0};
    std::atomic<int> g_testDump{-1};
    uint64_t g_checks = 0, g_breaks = 0, g_resets = 0;

    inline bool okAddress(uint32_t a, uint32_t size) { return a >= 0x100000u && a + size <= 0x2000000u && !(a & 3u); }

    inline uint32_t r32(const uint8_t *ram, uint32_t a)
    {
        uint32_t v;
        std::memcpy(&v, ram + a, 4);
        return v;
    }

    inline float dist(const float *a, const float *b)
    {
        const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    Track *track(uint32_t actor, int list)
    {
        Track *free = nullptr, *oldest = &g_tracks[0];
        for (Track &t : g_tracks)
        {
            if (t.actor == actor && t.list == list)
                return &t;
            if (!free && t.actor == 0)
                free = &t;
            if (t.lastStep < oldest->lastStep)
                oldest = &t;
        }
        Track *t = free ? free : oldest;
        *t = Track{};
        t->actor = actor;
        t->list = list;
        return t;
    }

    void report(Track &t, const char *what)
    {
        t.told = true;
        t.toldAt = g_step;
        ++g_breaks;
        blackbox::event("[skeleton] BROKEN %s actor %08x (type %08x, %d joints): %s", kListName[t.list], t.actor, t.type, t.joints, what);
        const double now = static_cast<double>(g_step) / 60.0;
        if (now - g_lastSave >= 60.0)
        {
            g_lastSave = now;
            blackbox::save("skeleton");
        }
    }

    void check(const uint8_t *ram, int list, uint32_t actor)
    {
        const uint32_t model = actor + 0x50u;
        const int joints = static_cast<int>(static_cast<uint16_t>(r32(ram, model + 0x68u) >> 16));
        const uint32_t jnt = r32(ram, model + 0x80u), wmat = r32(ram, model + 0x90u), type = r32(ram, actor);
        if (joints < 2 || joints > kMaxJoints || !okAddress(jnt, joints * 0xE0u) || !okAddress(wmat, joints * 0x40u))
            return;
        Track &t = *track(actor, list);
        if (t.jnt != jnt || t.wmat != wmat || t.joints != joints || t.type != type)
        {
            const uint32_t a = t.actor;
            const int l = t.list;
            t = Track{};
            t.actor = a;
            t.list = l;
            t.type = type;
            t.jnt = jnt;
            t.wmat = wmat;
            t.joints = joints;
        }
        t.lastStep = g_step;
        ++t.seen;
        ++g_checks;

        float pos[3];
        std::memcpy(pos, ram + actor + 0x90u, 12);
        float w[kMaxJoints][16];
        bool numbersOk = true;
        int badJoint = -1;
        for (int j = 0; j < joints; ++j)
        {
            std::memcpy(w[j], ram + wmat + static_cast<uint32_t>(j) * 0x40u, 64);
            for (int k = 0; k < 16; ++k)
                if (!std::isfinite(w[j][k]) || std::fabs(w[j][k]) > 1e6f)
                {
                    if (numbersOk)
                        badJoint = j;
                    numbersOk = false;
                }
        }
        // Were the bones computed again since the last step? In the pause menu (and on the frame where the actor is moved
        // by hand) the world matrices stay where they were while the actor's position can change: the "far" rule must
        // not count then (false alarms of run e1, 16/11: the bad module moved Octavius while the game was paused).
        float sig = 0.f;
        for (int j = 0; j < joints; ++j)
            sig += w[j][12] + 3.f * w[j][13] + 7.f * w[j][14] + w[j][0] + w[j][5];
        const bool bonesMoved = !t.haveSig || sig != t.lastSig;
        t.lastSig = sig;
        t.haveSig = true;
        const int test = g_testBone.load(std::memory_order_relaxed);
        if (test > 0 && list == 0)
        {
            g_testBone.fetch_sub(1);
            w[joints / 2][12] += 1000.f; // a broken joint, only in what these rules see
        }
        if (g_testExplode.load(std::memory_order_relaxed) > 0 && list == 0)
        {
            // a body coming apart: every joint 1.8x farther from the actor, so every bone 1.8x longer but still well
            // inside the "far" limit (tests the many-bones rule alone), only in what these rules see
            g_testExplode.fetch_sub(1);
            for (int j = 0; j < joints; ++j)
                for (int k = 0; k < 3; ++k)
                    w[j][12 + k] = pos[k] + 1.8f * (w[j][12 + k] - pos[k]);
        }
        if (t.seen < kSettle)
            return;
        if (t.told)
        {
            if (g_step - t.toldAt < 600)
                return;
            t.told = false;
            t.badNumber = t.badFar = t.badMany = 0;
            std::memset(t.badLen, 0, sizeof(t.badLen));
            std::memset(t.badScale, 0, sizeof(t.badScale));
            std::memset(t.elastic, 0, sizeof(t.elastic)); // a report (or a test) may have stretched every limb
        }
        if (!numbersOk && std::isfinite(pos[0]))
        {
            // Only bones that were good before (based): an actor waiting to appear can have matrices that were never
            // computed (false alarm of run e2, 16/11: an enemy and its weapon of area 051, never on screen).
            if (t.based && ++t.badNumber == kNeed)
            {
                char what[160];
                std::snprintf(what, sizeof(what), "joint %d has a value that is not a number or is huge (world matrix at %08x)", badJoint,
                              wmat + static_cast<uint32_t>(badJoint) * 0x40u);
                report(t, what);
            }
            return;
        }
        t.badNumber = 0;
        if (!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2]) || !numbersOk)
            return;

        float localLen[kMaxJoints], axis[kMaxJoints][3];
        for (int j = 0; j < joints; ++j)
        {
            float l[3];
            std::memcpy(l, ram + jnt + static_cast<uint32_t>(j) * 0xE0u + 0x30u, 12);
            localLen[j] = std::isfinite(l[0]) && std::isfinite(l[1]) && std::isfinite(l[2]) ? std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]) : 0.f;
            for (int r = 0; r < 3; ++r)
                axis[j][r] = std::sqrt(w[j][4 * r] * w[j][4 * r] + w[j][4 * r + 1] * w[j][4 * r + 1] + w[j][4 * r + 2] * w[j][4 * r + 2]);
        }
        float farthest = 0.f;
        int farJoint = 0;
        for (int j = 0; j < joints; ++j)
        {
            const float d = dist(&w[j][12], pos);
            if (d > farthest)
            {
                farthest = d;
                farJoint = j;
            }
        }
        if (!t.based)
        {
            // The first values, after the model had a few steps to settle.
            for (int j = 0; j < joints; ++j)
            {
                const int p = ram[jnt + static_cast<uint32_t>(j) * 0xE0u + 0xd5u];
                t.parent[j] = static_cast<uint8_t>(p);
                t.len[j] = p < joints && p != j ? dist(&w[j][12], &w[p][12]) : -1.f;
                t.local[j] = localLen[j];
                for (int r = 0; r < 3; ++r)
                    t.scale[j][r] = axis[j][r];
                t.badLen[j] = t.badScale[j] = 0;
                t.elastic[j] = false;
            }
            // Chains ("limbs"): a joint belongs to the chain that starts below the nearest joint with 2 or more children
            // (or the root). A weapon's chain or a hand's end joints are one limb; a body coming apart has many.
            int children[kMaxJoints] = {};
            for (int j = 0; j < joints; ++j)
                if (t.parent[j] < joints && t.parent[j] != j)
                    ++children[t.parent[j]];
            for (int j = 0; j < joints; ++j)
            {
                int k = j;
                for (int guard = 0; guard < joints; ++guard)
                {
                    const int p = t.parent[k];
                    if (p >= joints || p == k || children[p] >= 2)
                        break;
                    k = p;
                }
                t.limb[j] = static_cast<uint8_t>(k);
            }
            t.farBase = t.lastFar = farthest;
            t.based = true;
            return;
        }

        const float farLimit = t.list >= 3 ? std::max(8.f * t.farBase, 5000.f) : std::max(4.f * t.farBase, 800.f);
        // Weapons and objects (sor-giro, 16/11/2026): in the chariot race (area 030) three objects of type 000b0807
        // (20 joints) had joint 19 walk away from the actor little by little, past 5066 units: the rival chariots
        // running along the track, normal on the pictures. A broken matrix JUMPS: so for them the far rule counts only
        // when the farthest joint got more than half the limit farther in one step (then it must stay out kNeed steps).
        // Characters keep the plain rule (the `bbtest bone` test: one joint of the player 1000 units away at once).
        const bool farJump = t.list < 3 || t.badFar > 0 || farthest - t.lastFar > 0.5f * farLimit;
        if (bonesMoved)
            t.lastFar = farthest;
        if (farthest > farLimit && farJump)
        {
            if (!bonesMoved)
            {
                // bones not computed this step: no new evidence either way
            }
            else if (++t.badFar == kNeed)
            {
                char what[200];
                std::snprintf(what, sizeof(what), "joint %d (parent %d) is %.0f units from the actor (limit %.0f; at the start the farthest was %.0f)", farJoint,
                              t.parent[farJoint] == 0xFF ? -1 : t.parent[farJoint], farthest, farLimit, t.farBase);
                report(t, what);
                return;
            }
        }
        else
            t.badFar = 0;

        int outBones = 0, outJoint = -1, outLimbs = 0;
        float outLen = 0.f, outExpected = 0.f;
        bool limbOut[kMaxJoints] = {};
        int limbBones[kMaxJoints] = {}; // bones out this step, by limb
        bool farOut[kMaxJoints] = {};
        float farLen[kMaxJoints], farExpected[kMaxJoints];
        for (int j = 0; j < joints; ++j)
        {
            // scale. An axis brought to (almost) 0 is a part HIDDEN on purpose, not a broken one (sor-giro, 16/11/2026:
            // chariot race 033, a rival chariot crashing, object type 00430807 joint 2 axis 1 from 1.00 to 0.00, normal on
            // the pictures): it does not count. Blown up, or squashed to a size still visible, does.
            bool squashed = false;
            int axisNo = 0;
            for (int r = 0; r < 3; ++r)
                if (t.scale[j][r] > 0.05f && axis[j][r] >= 0.02f * t.scale[j][r] &&
                    (axis[j][r] > 2.f * t.scale[j][r] || axis[j][r] < 0.5f * t.scale[j][r]))
                {
                    squashed = true;
                    axisNo = r;
                }
            if (squashed)
            {
                if (++t.badScale[j] == kNeed)
                {
                    char what[200];
                    std::snprintf(what, sizeof(what), "joint %d (parent %d) is %s: axis %d is %.2f long, it was %.2f", j,
                                  t.parent[j] == 0xFF ? -1 : t.parent[j], axis[j][axisNo] > t.scale[j][axisNo] ? "blown up" : "squashed", axisNo,
                                  axis[j][axisNo], t.scale[j][axisNo]);
                    report(t, what);
                    return;
                }
            }
            else
                t.badScale[j] = 0;

            // length: only for the characters (player, NPCs, enemies). Weapons and objects stretch their bones on purpose
            // (sor-sfarfallio, 16/11/2026, false alarms of the author's game in area 088: weapon type 00240407, 8 joints,
            // bone 0-5 from 21 to 95 units; object type 00aa0807, 2 joints, bone 0-1 from 0 to 160): for them the other
            // rules (numbers, scale, far) still hold.
            const int p = t.parent[j];
            if (t.list >= 3 || t.len[j] < 0.f || p >= joints)
                continue;
            const float len = dist(&w[j][12], &w[p][12]);
            const float plain = t.local[j] > 0.5f ? t.len[j] * (localLen[j] / t.local[j]) : t.len[j];
            // The parent's world matrix carries a scale, and the bone to the child grows and shrinks with it (sor-giro,
            // 16/11/2026: enemy type 00350207 of areas 0c4 and 045 in a normal fight, 4 bones of the end chain from
            // joint 23 out at once; joint 23-24 22.6 units long where the local offset alone said 45, half: the parent
            // scaled to ~0.5). Squashed or blown up parents stay with the scale rule above (x0.5-x2).
            float now = 0.f, first = 0.f;
            for (int r = 0; r < 3; ++r)
                if (t.scale[p][r] > 0.05f)
                {
                    now += axis[p][r];
                    first += t.scale[p][r];
                }
            const float parentScale = first > 0.f ? now / first : 1.f;
            const float expected = plain * parentScale;
            const float off = std::fabs(len - expected);
            if (off > std::max(0.5f * expected, 6.f))
            {
                if (outJoint < 0)
                {
                    outJoint = j;
                    outLen = len;
                    outExpected = expected;
                }
                ++outBones;
                if (!limbOut[t.limb[j]])
                {
                    limbOut[t.limb[j]] = true;
                    ++outLimbs;
                }
                ++limbBones[t.limb[j]];
                // one bone far out alone is enough (sor-giro, 16/11: one bone out by up to x2 is normal animation);
                // checked after the loop, once we know whether its limb is elastic
                if (off > std::max(2.f * expected, 40.f))
                {
                    farOut[j] = true;
                    farLen[j] = len;
                    farExpected[j] = expected;
                }
                else
                    t.badLen[j] = 0;
            }
            else
                t.badLen[j] = 0;
        }
        // Elastic limbs (sor-giro, 16/11/2026): the same enemy type 00350207 of area 045, in a normal fight on the
        // pictures, had 4 bones of the chain from joint 23 out at once and then joint 26-27 63.9 units long where 10.6
        // was expected. A limb that really comes off moves rigidly: only the bone that holds it changes length (a single
        // displaced joint changes at most 2). A limb seen with 3 or more bones out at once stretches on purpose (a whip,
        // a chain, a flag): from then on the single-bone rule does not count on it. The other rules still do.
        for (int j = 0; j < joints; ++j)
            if (limbBones[j] >= 3)
                t.elastic[j] = true;
        for (int j = 0; j < joints; ++j)
        {
            if (!farOut[j])
                continue;
            if (t.elastic[t.limb[j]])
            {
                t.badLen[j] = 0;
                continue;
            }
            if (++t.badLen[j] == kNeed)
            {
                char what[220];
                std::snprintf(what, sizeof(what), "bone from joint %d to joint %d is %.1f units long, expected %.1f (first %.1f, local offset %.1f, was %.1f)", t.parent[j],
                              j, farLen[j], farExpected[j], t.len[j], localLen[j], t.local[j]);
                report(t, what);
                return;
            }
        }
        // Bones out on 3 or more different limbs at the same time: the body is coming apart. Several bones of ONE chain
        // stretch together in a normal fight (sor-giro, 16/11/2026: enemy type 00350207 of areas 0c4 and 045, 3-4 bones
        // from joint 23 out at once, joint 23-24 68.4 units long where 30 was expected, normal on the pictures).
        if (outBones >= 3 && outLimbs >= 3)
        {
            if (++t.badMany == kNeed)
            {
                char what[220];
                std::snprintf(what, sizeof(what), "%d bones on %d limbs changed length at once (first: joint %d to joint %d is %.1f units long, expected %.1f)", outBones,
                              outLimbs, t.parent[outJoint], outJoint, outLen, outExpected);
                report(t, what);
                return;
            }
        }
        else
            t.badMany = 0;
        // A line (no folder) when several bones are out but on fewer limbs: evidence that the limb rule is what keeps quiet.
        if (outBones >= 3 && outLimbs < 3)
        {
            if (++t.badManyRaw == kNeed && g_step - t.notedAt > 600)
            {
                t.notedAt = g_step;
                blackbox::event("[skeleton] note: %s actor %08x type %08x: %d bones out on %d limb%s only, not counted (first: joint %d to %d is %.1f long, expected %.1f)",
                                kListName[t.list], t.actor, t.type, outBones, outLimbs, outLimbs == 1 ? "" : "s", t.parent[outJoint], outJoint, outLen, outExpected);
            }
        }
        else
            t.badManyRaw = 0;
    }

    void dump(int all)
    {
        for (const Track &t : g_tracks)
            if (t.actor && t.lastStep == g_step && (all || t.list == 0))
                blackbox::event("[skeleton] %s actor %08x type %08x joints %d based %d far %.0f seen %llu", kListName[t.list], t.actor, t.type, t.joints,
                                t.based ? 1 : 0, t.farBase, static_cast<unsigned long long>(t.seen));
        std::printf("[skeleton] steps %llu, checks %llu, breaks %llu, resets %llu\n", static_cast<unsigned long long>(g_step),
                    static_cast<unsigned long long>(g_checks), static_cast<unsigned long long>(g_breaks), static_cast<unsigned long long>(g_resets));
        std::fflush(stdout);
    }
}

void sorScheletroStart()
{
    if (const char *e = std::getenv("SOR_SKELETON"))
        if (e[0] == '0')
            return;
    g_on = true;
    std::printf("[skeleton] on: bones of every actor checked on every game step (numbers, bone lengths, scale, distance)\n");
    std::fflush(stdout);
}

// Once per shown frame, on the presenting thread (sor_blackbox.cpp).
void sorScheletroFrame(PS2Runtime *rt)
{
    if (!g_on || !rt)
        return;
    const uint8_t *ram = rt->memory().getRDRAM();
    if (!ram)
        return;
    const int d = g_testDump.exchange(-1);
    if (d >= 0)
        dump(d);
    // Only when the game's logic made a step since the last frame (no step: nothing new to check; loads stop it).
    const uint64_t pad = g_sorContaPad.load(std::memory_order_relaxed);
    if (pad == g_lastPad)
        return;
    g_lastPad = pad;
    ++g_step;
    // A new area or scene: every body starts again.
    uint16_t area = 0, scene = 0;
    std::memcpy(&area, ram + SA(0x4be2e8u), 2);
    std::memcpy(&scene, ram + SA(0x4be2e0u), 2);
    const uint32_t where = (static_cast<uint32_t>(area) << 16) | scene;
    if (where != g_lastWhere)
    {
        g_lastWhere = where;
        for (Track &t : g_tracks)
            t = Track{};
        ++g_resets;
    }
    const uint32_t man = SA(0x4E69E0u);
    for (int l = 0; l < 5; ++l)
    {
        uint32_t node = r32(ram, man + kListOffset[l]);
        for (int k = 0; k < 48 && okAddress(node, 8); ++k)
        {
            const uint32_t actor = r32(ram, node + 4u);
            if (okAddress(actor, 0x100))
                check(ram, l, actor);
            node = r32(ram, node);
        }
    }
}

// bbtest bone N: the next N checks of the player see one joint 1000 units away. bbtest skeleton 0|1: a dump.
void sorScheletroCommand(const char *what, int value)
{
    if (!std::strcmp(what, "bone"))
        g_testBone.store(value > 0 ? value : 10);
    else if (!std::strcmp(what, "explode"))
        g_testExplode.store(value > 0 ? value : 10);
    else
        g_testDump.store(value);
}
