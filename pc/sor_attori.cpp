// Actor list, a debug key for us and for mods (European game SLES-52950 and American SLUS-20902). On only with SOR_ACTORS=1 (launcher: DEBUG, ACTOR LIST).
//
// F10 writes "attori.txt" in the game's working folder (next to sor.exe, overwritten every time) and a copy with the
// date and time in its name in the folder "actor-lists". One line per actor of the game's actor lists: the player,
// the people (NPCs), the fighters (enemies and allies), the weapons and the objects, with the type id, a name where
// one is known, the position, the distance from the player and the direction in plain words. Weapons say if they lie
// free on the ground or who holds them; the rudis (wooden sword) is named. It only reads the game's memory.
// Addresses (DWARF of the PAL ELF; notes in LAVORO-SHADOW-OF-ROME.md «Tasto F10, lista degli attori (sor-oggetti)»):
//   0x4E69E0 Work_man (classWORK_MAN): +0x00 sixteen list heads; node = {next, actor}. Lists: 0 player, 1 people
//            (NPC), 2 enemies and allies, 4 weapons, 8 objects; other lists may hold the same actors again (deduplicated).
//   actor +0x00 mBe_flag, +0x01 mGroup (its list), +0x02 mId (the type), +0x03 mType, +0x04 mKind, +0x20 mCondition,
//         +0x50 the model: +0x90 position x y z (floats), +0xB2 facing (mDir_y), +0x180 mFade_alpha, +0x181 mDisp_status;
//         characters: +0x300 height, +0x380 max life, +0x382 life; weapons: +0x382 life, +0x384 holder (0 = free).
//   0x4C8B40 Camera: +0x22 its direction (0x1000 = a full turn); the W key walks toward that direction + 0x800.
//   The rudis is the weapon with id 0x22, used only in area 089: Rudis_search_all_disp (0x2042F0, called only there)
//   marks exactly that on screen when mBe_flag has 0x04 and mCondition has 0x01. area089_prog (0x3CE370) creates it
//   where one fighter dies (rudisCarrier) and gives the win to the player who stands holding it in a zone of type 5
//   (goalZones): in area 089 the list says which fighter leaves it and where that zone is.
// It runs once per frame inside classSYSTEM::wbuf_swap (0x1E92D0), chained after the quick save.
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "raylib.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "sor_indirizzi.h" // SA(): the game's addresses, European or American build

bool sorTelecomandoAcceso(); // pc/sor_prove.cpp: the test remote control holds the keys
bool sorTastoIniettato(int codice);

namespace
{
    const uint32_t kSwap = SA(0x1E92D0u);    // classSYSTEM::wbuf_swap
    const uint32_t kWorkMan = SA(0x4E69E0u); // classWORK_MAN Work_man
    const uint32_t kArea = SA(0x4BE2E8u);    // ushort area
    const uint32_t kCamera = SA(0x4C8B40u);  // classCAMERA Camera
    const uint32_t kCamDir = 0x22u;          // camera direction, 0x1000 = a full turn
    const uint8_t kRudisId = 0x22u;          // the weapon id Rudis_search_all_disp looks for
    const uint16_t kRudisArea = 0x89u;       // the only area that uses the rudis (c_cockpit entry, area089_prog)
    const uint32_t kAppendInfo = SA(0x4852B8u); // area089_colosseum_info+0x28: -> Area089_em_append_info
    const uint32_t kRound = SA(0x4BE304u);   // Global+0x104 (byte): index into that table
    const uint32_t kActAtr = SA(0x4E8080u);  // Act_atr: +0x3C the area's zone table
    const int kLists = 16, kMaxNodes = 600;
    const double kPi = 3.14159265358979323846;

    PS2Runtime::RecompiledFunction g_swap = nullptr;
    bool g_prev = false;

    struct Actor
    {
        uint32_t addr = 0;
        int list = 0;                       // the first list it was found in
        uint8_t flag = 0, group = 0, id = 0, type = 0, kind = 0;
        uint64_t cond = 0;
        float x = 0, y = 0, z = 0, height = 0;
        int16_t dir = 0, life = 0, lifeMax = 0;
        uint8_t fade = 0, disp = 0;
        uint32_t holder = 0;                // weapons: who holds it, 0 = free
        int setFile = 0, setNo = 0;         // mSet_file_type (+0x12) and mSet_no (+0x05): its place in the area's sets
    };

    uint8_t *at(uint8_t *rdram, uint32_t a) { return rdram + (a & PS2_RAM_MASK); }
    uint32_t rd32(uint8_t *rdram, uint32_t a)
    {
        uint32_t v;
        std::memcpy(&v, at(rdram, a), 4);
        return v;
    }
    uint64_t rd64(uint8_t *rdram, uint32_t a)
    {
        uint64_t v;
        std::memcpy(&v, at(rdram, a), 8);
        return v;
    }
    int16_t rd16(uint8_t *rdram, uint32_t a)
    {
        int16_t v;
        std::memcpy(&v, at(rdram, a), 2);
        return v;
    }
    float rdF(uint8_t *rdram, uint32_t a)
    {
        float v;
        std::memcpy(&v, at(rdram, a), 4);
        return v;
    }
    // A pointer into the game's memory that an actor (about 0x560 bytes) can sit at.
    bool okPtr(uint32_t p) { return p > 0x100000u && p < 0x2000000u - 0x600u && (p & 3u) == 0; }

    Actor readActor(uint8_t *rdram, uint32_t a, int list)
    {
        Actor r;
        r.addr = a;
        r.list = list;
        const uint8_t *c = at(rdram, a);
        r.flag = c[0];
        r.group = c[1];
        r.id = c[2];
        r.type = c[3];
        r.kind = c[4];
        r.setNo = c[5];
        r.setFile = static_cast<int8_t>(c[0x12]);
        r.cond = rd64(rdram, a + 0x20);
        r.x = rdF(rdram, a + 0x90);
        r.y = rdF(rdram, a + 0x94);
        r.z = rdF(rdram, a + 0x98);
        r.dir = rd16(rdram, a + 0xB2);
        r.fade = c[0x180];
        r.disp = c[0x181];
        if (r.group <= 2) // characters
        {
            r.height = rdF(rdram, a + 0x300);
            r.lifeMax = rd16(rdram, a + 0x380);
            r.life = rd16(rdram, a + 0x382);
        }
        else if (r.group == 4) // weapons
        {
            r.lifeMax = rd16(rdram, a + 0x380);
            r.life = rd16(rdram, a + 0x382);
            r.holder = rd32(rdram, a + 0x384);
        }
        return r;
    }

    // Every actor of the 16 lists, each once. The lists of the five known groups are walked first.
    std::vector<Actor> collect(uint8_t *rdram)
    {
        static const int order[kLists] = {0, 1, 2, 4, 8, 3, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15};
        std::vector<Actor> out;
        std::map<uint32_t, bool> seen;
        for (int li : order)
        {
            uint32_t node = rd32(rdram, kWorkMan + 4u * static_cast<uint32_t>(li));
            for (int n = 0; n < kMaxNodes && okPtr(node); ++n)
            {
                const uint32_t actor = rd32(rdram, node + 4);
                if (okPtr(actor) && !seen.count(actor))
                {
                    seen[actor] = true;
                    out.push_back(readActor(rdram, actor, li));
                }
                node = rd32(rdram, node);
            }
        }
        return out;
    }

    const char *groupName(uint8_t g)
    {
        switch (g)
        {
        case 0: return "player";
        case 1: return "person";
        case 2: return "fighter";
        case 4: return "weapon";
        case 8: return "object";
        default: return "other";
        }
    }

    // A short name. Only the rudis is certain (the game's own search); the rest are the code family of the type
    // (WpNN, EmNN, NpcNN, OmNN) with a hint from the names of the game's functions, marked with "?".
    std::string actorName(const Actor &a)
    {
        char fam[16];
        switch (a.group)
        {
        case 0:
            return "the player";
        case 1:
            std::snprintf(fam, sizeof(fam), "Npc%02x", a.id);
            return std::string(fam) + " person";
        case 2:
        {
            std::snprintf(fam, sizeof(fam), "Em%02x", a.id);
            const char *hint = "";
            switch (a.id)
            {
            case 0x04: case 0x3a: hint = " ally?"; break;
            case 0x09: hint = " guard?"; break;
            case 0x0f: hint = " statue or lever?"; break;
            case 0x2b: hint = " man of the Forum?"; break;
            case 0x42: hint = " elephant?"; break;
            case 0x49: hint = " dog?"; break;
            default: hint = " enemy or ally"; break;
            }
            return std::string(fam) + hint;
        }
        case 4:
            if (a.id == kRudisId)
                return "RUDIS (wooden sword)";
            if (a.id == 0xC8) // seen in the city (area 0B8): the game's own label on them is APPLE
                return "Wpc8 apple";
            if (a.id == 0x24) // seen lying in area 089: a bow
                return "Wp24 bow";
            std::snprintf(fam, sizeof(fam), "Wp%02x", a.id);
            return std::string(fam) + " weapon";
        case 8:
        {
            std::snprintf(fam, sizeof(fam), "Om%02x", a.id);
            const char *hint = "";
            switch (a.id)
            {
            case 0x00: hint = " pole?"; break;
            case 0x05: case 0x06: hint = " gate?"; break;
            case 0x10: hint = " tree?"; break;
            case 0x42: case 0x46: hint = " item?"; break;
            case 0x5d: hint = " fire?"; break;
            case 0x80: hint = " door?"; break;
            default: hint = " object"; break;
            }
            return std::string(fam) + hint;
        }
        default:
            std::snprintf(fam, sizeof(fam), "id %02x", a.id);
            return fam;
        }
    }

    // An angle in degrees folded into (-180, 180].
    double fold(double d)
    {
        while (d > 180.0) d -= 360.0;
        while (d <= -180.0) d += 360.0;
        return d;
    }

    // Heading of a step (dx, dz) in degrees, the same measure as the camera's: atan2(dx, dz).
    double heading(double dx, double dz) { return std::atan2(dx, dz) * 180.0 / kPi; }

    // Plain words for an angle relative to "ahead" (0). Positive angles are to the LEFT: the W key walks toward
    // camera + 180 degrees and D toward 90 degrees less (the walking test script _tmp_solido/lotta.py).
    const char *words(double rel)
    {
        static const char *const w[8] = {"ahead", "ahead-left", "left", "behind-left",
                                         "behind", "behind-right", "right", "ahead-right"};
        int s = static_cast<int>(std::floor((fold(rel) + 360.0 + 22.5) / 45.0)) % 8;
        return w[s];
    }

    std::string stamp(const char *fmt)
    {
        char b[64];
        const std::time_t t = std::time(nullptr);
        const std::tm *tmv = std::localtime(&t); // only the game's thread calls it
        if (!tmv || !std::strftime(b, sizeof(b), fmt, tmv))
            return "unknown-time";
        return b;
    }

    std::string hex8(uint32_t v)
    {
        char b[16];
        std::snprintf(b, sizeof(b), "%08X", v);
        return b;
    }

    // Who holds a weapon: "the player", the holder's name, or "someone" when the holder is not in the lists.
    std::string holderName(const std::vector<Actor> &all, uint32_t holder)
    {
        for (const Actor &h : all)
            if (h.addr == holder)
                return h.list == 0 ? "the player" : actorName(h) + " " + hex8(h.addr);
        return "someone not in the lists " + hex8(holder);
    }

    // area089_prog (0x3CE370), step 0: file_set_no_work_get(&Work_man, 2, Area089_em_append_info[Global+0x104].byte4,
    // 7) finds the fighter with that set file and set number 7; when its mCondition2 (+0x30) gets bit 36 (set by the
    // enemies' *_die functions and com_damage_life_set), Weapon_set(0x22, ..., its position, its facing) makes the rudis.
    uint32_t rudisCarrier(uint8_t *rdram, const std::vector<Actor> &all)
    {
        const uint32_t table = rd32(rdram, kAppendInfo);
        if (!okPtr(table))
            return 0;
        const int wantFile = *at(rdram, table + *at(rdram, kRound) + 4);
        for (const Actor &a : all)
            if (a.list == 2 && !(a.flag & 0x08) && a.setNo == 7 && a.setFile == wantFile)
                return a.addr;
        return 0;
    }

    struct Zone
    {
        float x0, x1, y0, y1, z0, z1;
    };

    // The area's zones of type 5 (Act_atr +0x3C: header word = count << 24 | first offset << 8; 0x80 bytes per zone,
    // type at byte 0, two corners as floats at +0x20 and +0x30). In area 089, area089_prog checks
    // act_atr_check(&Act_atr, 1, 5, ...): the player in a type 5 zone holding weapon 0x22 wins; without it, a message.
    std::vector<Zone> goalZones(uint8_t *rdram)
    {
        std::vector<Zone> out;
        const uint32_t p = rd32(rdram, kActAtr + 0x3Cu);
        if (!okPtr(p))
            return out;
        const uint32_t h = rd32(rdram, p);
        const uint32_t off = (h >> 8) & 0xFFu, count = h >> 24;
        for (uint32_t k = 0; k < count && k < 128u; ++k)
        {
            const uint32_t z = p + off + k * 0x80u;
            if (!okPtr(z) || *at(rdram, z) != 5)
                continue;
            Zone r{rdF(rdram, z + 0x20), rdF(rdram, z + 0x30), rdF(rdram, z + 0x24),
                   rdF(rdram, z + 0x34), rdF(rdram, z + 0x28), rdF(rdram, z + 0x38)};
            if (r.x0 > r.x1) std::swap(r.x0, r.x1);
            if (r.y0 > r.y1) std::swap(r.y0, r.y1);
            if (r.z0 > r.z1) std::swap(r.z0, r.z1);
            out.push_back(r);
        }
        return out;
    }

    void writeList(uint8_t *rdram)
    {
        const std::vector<Actor> all = collect(rdram);
        const Actor *pl = nullptr;
        for (const Actor &a : all)
            if (a.list == 0)
            {
                pl = &a;
                break;
            }
        const uint16_t area = static_cast<uint16_t>(rd32(rdram, kArea) & 0xFFFFu);
        const int16_t camRaw = rd16(rdram, kCamera + kCamDir);
        // "Ahead on screen": where W walks, camera direction + half a turn.
        const double screenAhead = (static_cast<uint16_t>(camRaw) + 0x800) % 0x1000 * 360.0 / 4096.0;
        // "Ahead of the player": his facing, same units as the camera (checked at run time, see LAVORO).
        const double bodyAhead = pl ? static_cast<uint16_t>(pl->dir) % 0x1000 * 360.0 / 4096.0 : 0.0;

        std::string text;
        char line[512];
        std::snprintf(line, sizeof(line),
                      "Shadow of Rome - actor list (F10) - %s - %s game - area %03X (%u in quicksave.ini)\n",
                      stamp("%Y-%m-%d %H:%M:%S").c_str(), SOR_DISC_SERIAL, area, area);
        text += line;
        if (pl)
        {
            std::snprintf(line, sizeof(line),
                          "Player: actor %08X at x=%.1f y=%.1f z=%.1f, height %.0f units, life %d/%d, facing raw %04X, "
                          "camera raw %04X\n",
                          pl->addr, pl->x, pl->y, pl->z, pl->height, pl->life, pl->lifeMax,
                          static_cast<uint16_t>(pl->dir), static_cast<uint16_t>(camRaw));
            text += line;
        }
        else
            text += "No player in the scene (menu, film or loading): no distances or directions.\n";
        text += "Distances are in game units, flat on the ground (the player's height above is in the same units).\n"
                "\"screen\" = as you see it: ahead = into the screen, left/right = screen left/right.\n"
                "\"player\" = from where the player's body faces.\n"
                "\"dy\" = height difference from the player.\n\n";

        // ", 812 units away, screen: ahead-left, player: behind, dy 400" (nothing without a player)
        auto rel = [&](double x, double y, double z) -> std::string {
            if (!pl)
                return "";
            const double dx = x - pl->x, dz = z - pl->z, h = heading(dx, dz);
            char b[128];
            std::snprintf(b, sizeof(b), ", %.0f units away, screen: %s, player: %s, dy %.0f", std::sqrt(dx * dx + dz * dz),
                          words(h - screenAhead), words(h - bodyAhead), y - pl->y);
            return b;
        };
        const bool rudisArena = area == kRudisArea;
        const uint32_t carrier = rudisArena ? rudisCarrier(rdram, all) : 0;

        // The rudis first, the thing this list was made for.
        int rudis = 0;
        for (const Actor &a : all)
        {
            if (a.group != 4 || a.id != kRudisId)
                continue;
            ++rudis;
            const std::string where = !a.holder || a.holder == a.addr ? "free, lying in the scene"
                                                                       : "held by " + holderName(all, a.holder);
            std::snprintf(line, sizeof(line), "RUDIS: actor %08X at x=%.1f y=%.1f z=%.1f, %s", a.addr, a.x, a.y, a.z,
                          where.c_str());
            text += line + rel(a.x, a.y, a.z);
            if (rudisArena)
                text += (a.flag & 4) && (a.cond & 1)
                            ? ", its on-screen marker can show (when it is in view and not behind a wall)"
                            : ", the game does not mark it on screen now";
            text += "\n";
        }
        if (!rudis && !rudisArena)
            text += "RUDIS: not in this scene (the game uses it in area 089).\n";
        if (!rudis && rudisArena)
        {
            text += "RUDIS: not created yet. In this arena the game makes it appear where one enemy dies";
            const Actor *c = nullptr;
            for (const Actor &a : all)
                if (a.addr == carrier)
                    c = &a;
            if (c)
            {
                std::snprintf(line, sizeof(line), ": %s, actor %08X at x=%.1f y=%.1f z=%.1f, life %d/%d",
                              actorName(*c).c_str(), c->addr, c->x, c->y, c->z, c->life, c->lifeMax);
                text += line + rel(c->x, c->y, c->z) + ".\n";
            }
            else
                text += " (number 7 of a later group of enemies): that enemy is not in the scene now, it should come later.\n";
        }
        if (rudisArena)
            for (const Zone &z : goalZones(rdram))
            {
                std::snprintf(line, sizeof(line),
                              "GOAL: to win, stand holding the rudis in the zone at x=%.0f..%.0f y=%.0f..%.0f z=%.0f..%.0f",
                              z.x0, z.x1, z.y0, z.y1, z.z0, z.z1);
                text += line + rel((z.x0 + z.x1) / 2, (z.y0 + z.y1) / 2, (z.z0 + z.z1) / 2) + "\n";
            }
        text += "\n";

        text += "  # list address  group   id type kind set  name                              x         y         z"
                "     dist screen       player        dy   life    state\n";
        int n = 0;
        for (const Actor &a : all)
        {
            ++n;
            char dist[96] = "        -  -            -             -";
            if (pl && a.addr != pl->addr)
            {
                const double dx = a.x - pl->x, dz = a.z - pl->z;
                const double h = heading(dx, dz);
                std::snprintf(dist, sizeof(dist), "%9.0f %-12s %-12s %5.0f", std::sqrt(dx * dx + dz * dz),
                              words(h - screenAhead), words(h - bodyAhead), a.y - pl->y);
            }
            char life[24] = "-";
            if (a.group <= 2 || a.group == 4)
                std::snprintf(life, sizeof(life), "%d/%d", a.life, a.lifeMax);
            std::string state;
            if (a.group == 4)
                state = !a.holder || a.holder == a.addr ? "free" : "held by " + holderName(all, a.holder);
            if (a.addr == carrier)
                state = "LEAVES THE RUDIS WHEN IT DIES";
            if (a.flag & 0x08)
                state += state.empty() ? "being removed" : ", being removed";
            char raw[96];
            std::snprintf(raw, sizeof(raw), " [flag %02X cond %llX disp %02X fade %02X]", a.flag,
                          static_cast<unsigned long long>(a.cond), a.disp, a.fade);
            std::snprintf(line, sizeof(line),
                          "%3d %4d %08X %-7s %02X   %02X   %02X %d.%-2d %-28s %9.1f %9.1f %9.1f %s %7s %s%s\n", n, a.list,
                          a.addr, groupName(a.group), a.id, a.type, a.kind, a.setFile, a.setNo, actorName(a).c_str(), a.x,
                          a.y, a.z, dist, life, state.c_str(), raw);
            text += line;
        }
        text += "\nset = mSet_file_type.mSet_no (its place in the area's set files); flag = mBe_flag (08 = being removed),"
                " cond = mCondition, disp = mDisp_status, fade = mFade_alpha:\nraw values for mods, their meaning is not"
                " all known. Names with \"?\" are guesses from the names of the game's functions.\n";

        {
            std::ofstream f("attori.txt", std::ios::trunc | std::ios::binary);
            f << text;
        }
        std::error_code ec;
        std::filesystem::create_directories("actor-lists", ec);
        {
            std::ofstream f("actor-lists/attori_" + stamp("%Y%m%d_%H%M%S") + ".txt", std::ios::trunc | std::ios::binary);
            f << text;
        }
        std::printf("[sor] F10: %d actors written to attori.txt (area %03X%s)\n", n, area,
                    rudis ? ", the rudis is in the list" : "");
        std::fflush(stdout);
    }

    bool pressed(int k, bool &prev)
    {
        const bool d = sorTelecomandoAcceso() ? sorTastoIniettato(k) : IsKeyDown(k);
        const bool hit = d && !prev;
        prev = d;
        return hit;
    }

    // On only with SOR_ACTORS=1 (the launcher's DEBUG page, row ACTOR LIST; test scripts set it too).
    bool actorsOn()
    {
        static const bool env = std::getenv("SOR_ACTORS") && std::getenv("SOR_ACTORS")[0] == '1';
        return env;
    }

    void actorsSwap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (ctx->pc == kSwap && actorsOn() && IsWindowReady() && pressed(KEY_F10, g_prev))
            writeList(rdram);
        g_swap(rdram, ctx, rt);
    }
}

// Called once after loadELF (pc/main_sor.cpp), after the quick save (its hook on wbuf_swap is chained here).
void sorAttoriInstalla(PS2Runtime &rt)
{
    g_swap = rt.lookupFunction(kSwap);
    if (!g_swap)
    {
        std::printf("[sor] F10 actor list: wbuf_swap 0x%06X not found, it stays off\n", kSwap);
        std::fflush(stdout);
        return;
    }
    rt.registerFunction(kSwap, actorsSwap);
    std::printf("[sor] F10 actor list: %s\n", actorsOn() ? "on (SOR_ACTORS=1)" : "off (launcher: DEBUG, ACTOR LIST)");
    std::fflush(stdout);
}
