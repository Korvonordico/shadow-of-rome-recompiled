// Option: turn off the game's screen veil (European game SLES-52950, ELF CRC B1995E29).
//
// On only with the environment variable SOR_NO_VIGNETTE=1 (the launcher's GRAPHICS page, row VIGNETTE set to OFF).
// Without it nothing here runs and nothing is registered: the game is exactly as before.
//
// What the veil is (addresses and names from the DWARF data of the PAL ELF; LAVORO-SHADOW-OF-ROME.md, 09/11, vignette):
// - The game draws its full-screen filters with one module, classSCREFC (c_screfc.cpp). The global classSCREFC Screfc
//   (0x547280) holds 16 effect slots of 0x200 bytes; each starts with SCREFC_HDR: id +0x00, displv +0x01, pri +0x02,
//   active +0x03. Every area loads its own slots from its data (resource chunk 0x23 -> classSCREFC::set_data 0x2E5940).
// - classSCREFC::move (0x2E5460, once per frame) first lists the slots with id != 0 and active != 0 (0x2E5668..0x2E56C8),
//   sorts them by pri and draws each through classSCREFC::apply_effect (0x2E4960, jump table 0x4A07D0). It is the only
//   caller of apply_effect. A slot with active == 0 is simply not drawn: the area data itself has many such slots.
// - The veil is three of these effects, all drawn over the whole finished picture:
//     id 11 random_noise (0x2C5580): a grain texture blended over the screen (SCREFC_RANDOM_NOISE.modulate alpha
//        about 1.5-12.5%, tinted): the "patina" and the "watermark". Active in 87 of the 136 areas' data.
//     id 16 color_correction (0x2BEDF0): a colour wash, the picture's brightness remapped to a colour ramp and blended
//        about 50% over it. Active in 64 areas' data.
//     id 6 anti_flicker (0x2D0A00) when its slides are 0 or +-1: the picture drawn again over itself half a pixel off at
//        about 23% (the PS2 TV anti-flicker softening). Active in 129 areas' data.
//   No code other than the area data creates ids 11 and 16 (every classSCREFC::get_effect/create/remove caller was
//   checked). id 6 is also the crowd-cheer zoom blur of the arena (classAUDIENCE blur_control 0x1FBBC0, slides +-0x50 /
//   +-0x3C); eight area slots use those slides: those are kept, only the static softening (every slide within
//   +-32, which since 14/11/2026 includes area 041's 0,17,0,17) is skipped. Fog, glow, lens flare, gamma, depth of field, cross fades and fades are untouched.
//
// How: classSCREFC::move is wrapped. Before it runs, the active byte of those slots is set to 0; right after, it is put
// back (if the game did not change it meanwhile). So the slots are skipped by the game's own path for inactive slots,
// and everything else that reads them (the arena code that backs up and restores slot 6) sees them unchanged.
// Undo: set VIGNETTE to ON in the launcher (or remove SOR_NO_VIGNETTE).
// For tests only: SOR_NO_VIGNETTE_IDS=11,16,6 picks which ids are skipped (6 = only the static softening).
// SOR_NO_HAZE=1 (14/11/2026, the author: "a light milky film over everything" in the fort, area 053) also skips
// the near part of id 5, SCREFC_GRADATION_FOG (apply_effect case 5 -> 0x2C04C0; start_z +0x10, end_z +0x14 floats,
// then a colour ramp): a slot whose fog starts almost at the camera (start_z < 100) lays the fog colour over
// everything, near objects too. Area 053 has one from 4.4 to 4000 (it lifts the darkest 5% of the picture from 19
// to 35) and a distance fog from 900 to 9932, which stays. Removing every id 5 slot made areas too dark (tested).
// Works with or without SOR_NO_VIGNETTE.
// Also with SOR_NO_HAZE=1 (14/11/2026, the author: area 041 "a bit blurred"): every fog lays its first colour, at its
// first alpha (alpha +0x6C, 16 uchar keys; colour +0x18, 16 RGBA keys), over everything nearer than start_z too.
// With a dark colour that is barely visible; with a light one (luma >= 100, e.g. area 041: 8D8563 at 24/128, from
// z 3505 to 23900) it is a milky film that lifts the cave's blacks (mean 42 -> 60). For those fogs only the first
// alpha key is set to 0 for the frame, so near things are clear and far ones still fade into the fog. Areas 040,
// 042, 050, 052, 053, 054 have only dark fogs and do not change.
// SOR_NO_DOF=1 (14/11/2026, the author: "a tiny doubling of far objects") skips id 3, SCREFC_DEPTH_OF_FIELD
// (apply_effect case 3 -> 0x2D0620; start_z +0x10, end_z +0x14, power +0x18, colour +0x1C): it blurs what is far
// away by laying a shifted copy of the picture over it, which at 4x-8x shows as a double outline on far trees.
// Area 053: from z 4700 to 15500, power 1. The river area 052 has none.
// SOR_NO_DOF=1 also skips id 4, SCREFC_FOCUS (apply_effect case 4 -> 0x2C1870; reserve +0x10, out_start_z +0x14,
// out_end_z +0x18, range +0x1C, detail +0x20, power +0x24): the same kind of blur, out of focus from very near. In the
// Rome street (area 0D1) it starts at z 23 and is full at 1000, so the player and everything behind get a soft copy
// over the sharp picture (14/11/2026, the author). Frozen frame, 3x: centre sharpness 7.28 -> 9.18 without it.
// Kept: id 17 SCREFC_HEAT_HAZE (amp 5/5, speed 10/10, from z 1000 there), the shimmer far away (7.28 -> 8.09).
// Log: "[sor] vignette off (classSCREFC::move 0x2E5460)" once, then one line each time the skipped set or the area
// changes (at most 200 lines).
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <string>
#include "sor_indirizzi.h" // SA(): the game's addresses, European or American build

namespace
{
    const uint32_t kMove = SA(0x2E5460u);     // classSCREFC::move(this)
    const uint32_t kSlots = 16u;          // classSCREFC: 16 slots of 0x200 bytes from +0x0
    const uint32_t kSlotSize = 0x200u;
    const uint32_t kArea = SA(0x4BE2E8u);     // Global.Area_no (ushort)
    const uint8_t kIdAntiFlicker = 6u;
    const uint8_t kIdNoise = 11u;
    const uint8_t kIdColour = 16u;
    const uint8_t kIdHaze = 5u;
    const uint8_t kIdDof = 3u;
    const uint8_t kIdFocus = 4u;
    const uint32_t kFogColour0 = 0x18u;   // SCREFC_GRADATION_FOG: colour keys (RGBA), alpha keys, their counts
    const uint32_t kFogColourNum = 0x68u;
    const uint32_t kFogAlpha0 = 0x6Cu;
    const uint32_t kFogAlphaNum = 0x8Cu;

    PS2Runtime::RecompiledFunction g_move = nullptr;
    bool g_skip[32] = {};
    uint32_t g_lastMask = 0xFFFFFFFFu;
    uint32_t g_lastArea = 0xFFFFFFFFu;
    int g_lines = 0;

    // SCREFC_ANTI_FLICKER: slide_l/t/r/b (sint, 1/16 pixel) at +0x14..+0x20. Up to +-32 (2 pixels) = the static
    // softening from the area data: 0 or +-1 in most areas, 0,17,0,17 in area 041 (14/11/2026, the author: "a soft
    // copy over the sharp picture"; the picture drawn again about 1 pixel lower, 8 pixels at 8x). The arena's
    // crowd-cheer zoom blur (classAUDIENCE blur_control 0x1FBBC0) only writes +-0x50 / +-0x3C, 0, or a backup copy.
    bool staticSoftening(const uint8_t *slot)
    {
        for (uint32_t k = 0; k < 4u; ++k)
        {
            int32_t s;
            std::memcpy(&s, slot + 0x14u + 4u * k, 4);
            if (s > 32 || s < -32)
                return false;
        }
        return true;
    }

    // A fog whose first colour is light and whose first alpha is not 0: the milky film over near things.
    bool lightFogNear(const uint8_t *slot)
    {
        uint32_t colours, alphas;
        std::memcpy(&colours, slot + kFogColourNum, 4);
        std::memcpy(&alphas, slot + kFogAlphaNum, 4);
        if (colours == 0 || alphas == 0 || slot[kFogAlpha0] == 0)
            return false;
        const uint8_t *c = slot + kFogColour0;
        return (299u * c[0] + 587u * c[1] + 114u * c[2]) / 1000u >= 100u;
    }

    const char *effectName(uint32_t id)
    {
        if (id == kIdNoise)
            return "grain";
        if (id == kIdColour)
            return "colour wash";
        if (id == kIdAntiFlicker)
            return "soft filter";
        if (id == kIdHaze)
            return "haze";
        if (id == kIdDof)
            return "depth of field";
        if (id == kIdFocus)
            return "focus blur";
        return "effect";
    }

    void moveWithoutVeil(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (ctx->pc != kMove)
        {
            g_move(rdram, ctx, rt);
            return;
        }
        const uint32_t self = GPR_U32(ctx, 4);
        uint8_t saved[kSlots];
        uint8_t savedAlpha[kSlots] = {};
        uint32_t mask = 0;
        for (uint32_t i = 0; i < kSlots; ++i)
        {
            saved[i] = 0;
            uint8_t *slot = rdram + ((self + i * kSlotSize) & PS2_RAM_MASK);
            const uint8_t id = slot[0];
            if (id == 0 || id >= 32u || !g_skip[id] || slot[3] == 0)
                continue;
            if (id == kIdAntiFlicker && !staticSoftening(slot))
                continue; // the arena's crowd-cheer zoom blur: keep it
            if (id == kIdHaze)
            {
                float startZ;
                std::memcpy(&startZ, slot + 0x10u, 4);
                if (!(startZ < 100.0f))
                {
                    // a distance fog: keep it, without the light film it lays over near things
                    if (lightFogNear(slot))
                    {
                        savedAlpha[i] = slot[kFogAlpha0];
                        slot[kFogAlpha0] = 0;
                        mask |= 1u << id;
                    }
                    continue;
                }
            }
            saved[i] = slot[3];
            slot[3] = 0;
            mask |= 1u << id;
        }
        g_move(rdram, ctx, rt);
        for (uint32_t i = 0; i < kSlots; ++i)
        {
            uint8_t *slot = rdram + ((self + i * kSlotSize) & PS2_RAM_MASK);
            if (saved[i] && slot[3] == 0)
                slot[3] = saved[i];
            if (savedAlpha[i] && slot[kFogAlpha0] == 0)
                slot[kFogAlpha0] = savedAlpha[i];
        }
        uint16_t area;
        std::memcpy(&area, rdram + kArea, 2);
        if ((mask != g_lastMask || area != g_lastArea) && g_lines < 200)
        {
            g_lastMask = mask;
            g_lastArea = area;
            ++g_lines;
            std::string what;
            for (uint32_t id = 1; id < 32u; ++id)
                if (mask & (1u << id))
                {
                    if (!what.empty())
                        what += ", ";
                    what += effectName(id);
                }
            std::printf("[sor] vignette: area %03x, skipped: %s\n", area, what.empty() ? "nothing" : what.c_str());
            std::fflush(stdout);
        }
    }
}

// SOR_GABO_NOPOST=1 (test only, 17/11/2026): Gabominated's PCSX2 patch "Disable Post-Processing Effects"
// (github.com/Gabominated/PCSX2, SLES-52950_B1995E29.pnach: patch=1,EE,002E57C8,word,00000000), used with the
// author's permission (Gabominated/PCSX2 issue #36). 0x2E57C8 is `jal 0x2E4960` (0C0B9258) in classSCREFC::move,
// the only call in the whole ELF to classSCREFC::apply_effect (no other jal/j, no data pointer to it). Writing a
// NOP into memory does nothing here (the code is recompiled), so apply_effect itself is replaced by a function that
// returns at once: the same result as the NOP (its delay slot, a0 = s1, still runs; move ignores v0 and reloads
// ra from its stack). Every full-screen effect of the area data is then not drawn: blur, depth of field, focus,
// bloom and glows, lens flare, fog, grain, colour wash, gamma, anti-flicker, edge anti-aliasing, mist, water...
// American build: the call is at 0x2E5258 (jal 0x2E43F0), same code.
//
// SOR_NO_POST=1 (17/11/2026, the launcher's GRAPHICS > POST EFFECTS set to OFF, the default): the same idea, effect by
// effect. The full patch also turns the river's water black (area 052) and would stop the event cross fades.
// apply_effect(this, a1 = the slot, SCREFC_HDR: id at +0x00) is replaced by a function that runs the original only
// for the kinds kept, runs the fog only past its end (below), and returns at once for the others.
// The river's grey water is not the water effect: keeping id 18 alone still left it black (tested, 17/11). Nothing
// is drawn under the water surface, so there the depth is past the fog's end_z and the fog (id 5) lays its full
// colour: that is the water's look on the PS2. screfc_gradation_fog (0x2C04C0) turns every depth into a colour and
// an alpha through a table (t = 255 * (depth - start_z) / (end_z - start_z), 0..255; alpha keys +0x6C, their
// positions +0x7C, their number +0x8C): for each fog slot the alpha keys are set, for that call only, to 0 at 0,
// 0 at 254 and the fog's own alpha at 255, then put back. So only what lies past the fog's end gets the fog (the
// water, and what is past the end of the distance fog); everything nearer is as with the full patch. This is done
// only in the areas that use the water effect (id 18: the river 052, the cave 041); elsewhere the fog is skipped:
// the sky is past the fog's end too, and on the Rome street it turned pale again (tested, 17/11).
// SOR_NO_POST_FOG=0 (tests): the fog skipped too.
// Kinds (apply_effect jump table 0x4A07D0, names from the DWARF data): 1 base, 2 edge_detect, 3 depth_of_field,
// 4 focus, 5 gradation_fog, 6 anti_flicker, 7 directional_glow, 8 star_glare, 9 bloom_glare, 10 edge_antialiase,
// 11 random_noise, 12 light_scattering, 13 light_glare, 14 lens_flare, 15 bloom_glow, 16 color_correction,
// 17 heat_haze, 18 water, 19 procedural_mist, 20 cross_fade, 21 gamma_correct, 22 after_image, 23 thermography.
// Kept: 18 water (screfc_water 0x2CEF40: its own copy of the screen, shrunk and laid on the water surface), 20
// cross_fade (made by the events, classEVENT::ev_cross_fade 0x2A9BC0, and by the player change, c_pl_change exit
// 0x30FF6C; apply_effect also runs its timer, update_cross_fade 0x2BDED0, so without it a fade would never end), and
// 1 base (screfc_base 0x2E2640): it draws nothing, it only copies the area's scene values (0x4E4810/18/20) that the
// camera (make_view_matF), the models, the scene fog and the water's stencil (c_water.cpp scr0_stencil_tag) read.
// Every other kind is skipped: what the full patch removes, but for those three and the fog past its end. The water
// effect is kept in case it draws somewhere (in 052 it changes nothing visible). Gameplay code that only reads an
// effect (Effect_get_water_height reads the water slot) is not affected: the slots stay as they are.
// For tests only: SOR_NO_POST_KEEP=1,18,20 picks the kinds kept (an empty list keeps nothing = the full patch).
// SOR_GABO_NOPOST=1 still means the full patch (tests).
namespace
{
    const uint32_t kApplyEffect = SA(0x2E4960u); // classSCREFC::apply_effect(this, slot)
    const uint32_t kAreaNo = SA(0x4BE2E8u);      // Global.Area_no (ushort)
    PS2Runtime::RecompiledFunction g_apply = nullptr;
    bool g_applyLogged = false;
    bool g_keep[32] = {};
    bool g_fogFar = false;
    // Dim fogs (17/11/2026, the author with CLEAN: "in alcuni punti è molto scuro, tipo nella foresta nella prima scena
    // del capitolo 1", area 050). The fogs of the forest, the fort, the river and the caves are dim (lightest colour
    // key luma 64..131: areas 041, 050-055): they lift the blacks to the fog's colour, and without them up to 44% of
    // the picture went almost black (area 050 mean 28 -> 19; fort 053 34 -> 23). The fogs that turned the Rome street's
    // sky pale are near white (luma 234..255: areas 0D1, 082, 0B8). Measured 17/11 (runs eu-L, eu-G3): the dim fogs
    // stay with CLEAN, at SOR_NO_POST_DARKFOG % of their alpha (default 100: as on the PS2; 0: skipped as before);
    // the near-white ones are still skipped. kFogKeepLuma is the line between the two.
    int g_darkFogPct = 0;
    const uint8_t kPostFog = 5u;           // SCREFC_GRADATION_FOG
    const uint32_t kFogKeys = 0x6Cu;       // alpha[16] +0x6C, alpha_t[16] +0x7C, alpha_num (uint) +0x8C
    const uint32_t kFogKeysSize = 0x24u;

    // The fog past its end is laid only in an area that uses the water screen effect (id 18 went through apply_effect
    // less than half a second ago: the river 052, the cave 041). Elsewhere it would also pale the sky, which lies past
    // the end of the fog too: the Rome street 0D1 turned pale with it everywhere, and also with it only while a water
    // surface was drawn (classWATER_SURFACE::draw 0x304740 runs there too) (tested, 17/11).
    const uint8_t kPostWater = 18u;
    std::chrono::steady_clock::time_point g_waterLast;
    bool g_waterEver = false;

    bool waterSeen()
    {
        return g_waterEver && std::chrono::steady_clock::now() - g_waterLast < std::chrono::milliseconds(500);
    }

    // The alpha screfc_gradation_fog (0x2C0700..0x2C0868) gives to the depths at or past end_z (t = 255): the first
    // alpha key whose position is >= t, interpolated from the key before it; 0x80 when no key reaches t.
    uint8_t fogAlphaAt255(const uint8_t *slot)
    {
        uint32_t num;
        std::memcpy(&num, slot + kFogKeys + 0x20u, 4);
        const int t = 255;
        for (uint32_t k = 0; k < num && k < 16u; ++k)
        {
            const int pos = slot[kFogKeys + 0x10u + k];
            if (pos < t)
                continue;
            const uint32_t prev = k ? k - 1u : 0u;
            const int ppos = slot[kFogKeys + 0x10u + prev];
            const int a0 = slot[kFogKeys + prev];
            const int a1 = slot[kFogKeys + k];
            const int frac = (t == ppos || pos == ppos) ? 0 : ((t - ppos) << 7) / (pos - ppos);
            return static_cast<uint8_t>(a0 + (((a1 - a0) * frac) >> 7));
        }
        return 0x80u;
    }
    const uint32_t kFogKeepLuma = 180u;
    // A dim fog: every colour key darker than kFogKeepLuma.
    bool darkFog(const uint8_t *slot)
    {
        uint32_t colours;
        std::memcpy(&colours, slot + kFogColourNum, 4);
        if (colours == 0 || colours > 16u)
            return false;
        for (uint32_t k = 0; k < colours; ++k)
        {
            const uint8_t *c = slot + kFogColour0 + 4u * k;
            if ((299u * c[0] + 587u * c[1] + 114u * c[2]) / 1000u >= kFogKeepLuma)
                return false;
        }
        return true;
    }

    // The lightest colour key of a fog (luma 0..255), for the log.
    uint32_t fogMaxLuma(const uint8_t *slot)
    {
        uint32_t colours, best = 0;
        std::memcpy(&colours, slot + kFogColourNum, 4);
        for (uint32_t k = 0; k < colours && k < 16u; ++k)
        {
            const uint8_t *c = slot + kFogColour0 + 4u * k;
            const uint32_t l = (299u * c[0] + 587u * c[1] + 114u * c[2]) / 1000u;
            best = l > best ? l : best;
        }
        return best;
    }

    uint32_t g_postArea = 0xFFFFFFFFu;
    uint32_t g_postMask = 0;
    int g_postLines = 0;

    const char *const kEffectNames[24] = {"-", "base", "edge_detect", "depth_of_field", "focus", "gradation_fog",
                                          "anti_flicker", "directional_glow", "star_glare", "bloom_glare",
                                          "edge_antialiase", "random_noise", "light_scattering", "light_glare",
                                          "lens_flare", "bloom_glow", "color_correction", "heat_haze", "water",
                                          "procedural_mist", "cross_fade", "gamma_correct", "after_image",
                                          "thermography"};

    void applyEffectSkipped(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (ctx->pc != kApplyEffect)
        {
            g_apply(rdram, ctx, rt);
            return;
        }
        const uint8_t id = rdram[GPR_U32(ctx, 5) & PS2_RAM_MASK];
        if (id == kPostWater)
        {
            g_waterLast = std::chrono::steady_clock::now();
            g_waterEver = true;
        }
        const bool keep = id < 32u && g_keep[id];
        uint8_t *const effSlot = rdram + (GPR_U32(ctx, 5) & PS2_RAM_MASK);
        const bool darkKept = id == kPostFog && !keep && g_darkFogPct > 0 && darkFog(effSlot); // a dark fog: kept
        const bool fogFar = !darkKept && id == kPostFog && g_fogFar && waterSeen(); // the fog past its end, water area
        // bit 31: the fog past its end, bit 30: a dark fog kept (id 5 alone = the fog skipped)
        const uint32_t bit = fogFar ? 31u : darkKept ? 30u : id;
        // one line per area the first time each kind is skipped (or, for the kept ones but base, drawn) there; at most
        // 200 lines
        uint16_t area;
        std::memcpy(&area, rdram + kAreaNo, 2);
        if (area != g_postArea)
        {
            g_postArea = area;
            g_postMask = 0;
        }
        if (id < 31u && (!keep || id != 1u) && !(g_postMask & (1u << bit)) && g_postLines < 200)
        {
            g_postMask |= 1u << bit;
            ++g_postLines;
            char how[48];
            std::snprintf(how, sizeof how, "kept (dim fog, %d%%)", g_darkFogPct);
            char lum[48] = "";
            if (id == kPostFog)
                std::snprintf(lum, sizeof lum, " (lightest colour: luma %u)", fogMaxLuma(effSlot));
            std::printf("[sor] post effects: area %03x, %s %s%s\n", area,
                        keep ? "kept" : darkKept ? how : (fogFar ? "only past its end (water area), kept" : "skipped"),
                        id < 24u ? kEffectNames[id] : "?", lum);
            std::fflush(stdout);
        }
        if (keep)
        {
            g_apply(rdram, ctx, rt); // a kind that is kept: the game's own code, unchanged
            return;
        }
        if (darkKept)
        {
            if (g_darkFogPct >= 100)
            {
                g_apply(rdram, ctx, rt); // the dark fog as on the PS2
                return;
            }
            // its alpha keys at g_darkFogPct %, for this call only; in a water area the depths past its end (the
            // water) keep the full alpha, as with the fog past its end below
            uint8_t saved[kFogKeysSize];
            std::memcpy(saved, effSlot + kFogKeys, kFogKeysSize);
            const uint8_t far = fogAlphaAt255(effSlot);
            uint32_t num;
            std::memcpy(&num, effSlot + kFogKeys + 0x20u, 4);
            if (num > 16u)
                num = 16u;
            bool at255 = false;
            for (uint32_t k = 0; k < num; ++k)
            {
                effSlot[kFogKeys + k] = static_cast<uint8_t>(effSlot[kFogKeys + k] * g_darkFogPct / 100);
                if (effSlot[kFogKeys + 0x10u + k] == 255u)
                {
                    at255 = true;
                    if (waterSeen())
                        effSlot[kFogKeys + k] = far;
                }
            }
            if (waterSeen() && !at255 && num < 16u)
            {
                effSlot[kFogKeys + num] = far;
                effSlot[kFogKeys + 0x10u + num] = 255u;
                const uint32_t n2 = num + 1u;
                std::memcpy(effSlot + kFogKeys + 0x20u, &n2, 4);
            }
            g_apply(rdram, ctx, rt);
            std::memcpy(effSlot + kFogKeys, saved, kFogKeysSize); // the area data as it was
            return;
        }
        if (fogFar)
        {
            // the fog only where the picture is farther than its end_z: the river's water (nothing drawn under it)
            uint8_t *slot = rdram + (GPR_U32(ctx, 5) & PS2_RAM_MASK);
            uint8_t saved[kFogKeysSize];
            std::memcpy(saved, slot + kFogKeys, kFogKeysSize);
            const uint8_t far = fogAlphaAt255(slot);
            std::memset(slot + kFogKeys, 0, kFogKeysSize);
            slot[kFogKeys + 0x00] = 0; // alpha keys: 0 at 0, 0 at 254, the fog's own alpha at 255
            slot[kFogKeys + 0x01] = 0;
            slot[kFogKeys + 0x02] = far;
            slot[kFogKeys + 0x10] = 0; // their positions (alpha_t)
            slot[kFogKeys + 0x11] = 254;
            slot[kFogKeys + 0x12] = 255;
            slot[kFogKeys + 0x20] = 3; // alpha_num
            g_apply(rdram, ctx, rt);
            std::memcpy(slot + kFogKeys, saved, kFogKeysSize); // the area data as it was
            return;
        }
        if (!g_applyLogged)
        {
            g_applyLogged = true;
            std::printf("[sor] post-processing off: first screen effect skipped (id %u)\n", id);
            std::fflush(stdout);
        }
        ctx->pc = GPR_U32(ctx, 31); // straight back to move, as if the call were not there
    }

    void sorGaboInstalla(PS2Runtime &rt)
    {
        const char *env = std::getenv("SOR_GABO_NOPOST");
        const char *sel = std::getenv("SOR_NO_POST");
        const bool full = env && std::strcmp(env, "1") == 0;
        const bool selective = sel && std::strcmp(sel, "1") == 0;
        if (!full && !selective)
            return;
        g_apply = rt.lookupFunction(kApplyEffect);
        if (!g_apply)
        {
            std::printf("[sor] post-processing: classSCREFC::apply_effect 0x%06X not found, effects stay on\n",
                        kApplyEffect);
            std::fflush(stdout);
            return;
        }
        std::string kept;
        if (!full)
        {
            const char *ids = std::getenv("SOR_NO_POST_KEEP");
            if (ids)
            {
                const char *p = ids;
                while (*p)
                {
                    char *end = nullptr;
                    const long v = std::strtol(p, &end, 10);
                    if (end == p)
                    {
                        ++p;
                        continue;
                    }
                    if (v > 0 && v < 32)
                        g_keep[v] = true;
                    p = end;
                }
            }
            else
            {
                g_keep[1] = true;  // base: no drawing, the area's scene values
                g_keep[18] = true; // water
                g_keep[20] = true; // cross fades
            }
            for (uint32_t id = 1; id < 32u; ++id)
                if (g_keep[id])
                {
                    if (!kept.empty())
                        kept += ", ";
                    kept += id < 24u ? kEffectNames[id] : "?";
                }
            const char *fog = std::getenv("SOR_NO_POST_FOG");
            g_fogFar = !g_keep[kPostFog] && !(fog && std::strcmp(fog, "0") == 0);
            const char *dark = std::getenv("SOR_NO_POST_DARKFOG");
            g_darkFogPct = g_keep[kPostFog] ? 0 : dark && *dark ? std::atoi(dark) : 100;
            g_darkFogPct = g_darkFogPct < 0 ? 0 : g_darkFogPct > 100 ? 100 : g_darkFogPct;
            if (g_darkFogPct > 0)
            {
                char d[48];
                std::snprintf(d, sizeof d, "dim gradation_fog at %d%%", g_darkFogPct);
                kept += kept.empty() ? "" : ", ";
                kept += d;
            }
            if (g_fogFar)
                kept += kept.empty() ? "gradation_fog past its end in water areas"
                                     : ", gradation_fog past its end in water areas";
        }
        rt.registerFunction(kApplyEffect, applyEffectSkipped);
        if (full)
            std::printf("[sor] post-processing off (SOR_GABO_NOPOST=1, Gabominated's patch: classSCREFC::apply_effect "
                        "0x%06X not run)\n", kApplyEffect);
        else
            std::printf("[sor] post-processing off (SOR_NO_POST=1, idea of Gabominated's patch: classSCREFC::apply_effect "
                        "0x%06X runs only for: %s)\n", kApplyEffect, kept.empty() ? "nothing" : kept.c_str());
        std::fflush(stdout);
    }
}

// Called once after loadELF (pc/main_sor.cpp), after the other replacements.
void sorVignettaInstalla(PS2Runtime &rt)
{
    sorGaboInstalla(rt);
    const char *env = std::getenv("SOR_NO_VIGNETTE");
    const char *haze = std::getenv("SOR_NO_HAZE");
    const bool noVeil = env && std::strcmp(env, "1") == 0;
    const bool noHaze = haze && std::strcmp(haze, "1") == 0;
    const char *dof = std::getenv("SOR_NO_DOF");
    const bool noDof = dof && std::strcmp(dof, "1") == 0;
    if (!noVeil && !noHaze && !noDof)
        return;
    g_move = rt.lookupFunction(kMove);
    if (!g_move)
    {
        std::printf("[sor] vignette: classSCREFC::move 0x%06X not found, the veil stays on\n", kMove);
        std::fflush(stdout);
        return;
    }
    const char *ids = std::getenv("SOR_NO_VIGNETTE_IDS");
    if (!noVeil)
    {
        // only the haze
    }
    else if (ids && *ids)
    {
        const char *p = ids;
        while (*p)
        {
            char *end = nullptr;
            const long v = std::strtol(p, &end, 10);
            if (end == p)
            {
                ++p;
                continue;
            }
            if (v > 0 && v < 32)
                g_skip[v] = true;
            p = end;
        }
    }
    else
    {
        g_skip[kIdNoise] = true;
        g_skip[kIdColour] = true;
        g_skip[kIdAntiFlicker] = true;
    }
    if (noHaze)
        g_skip[kIdHaze] = true;
    if (noDof)
    {
        g_skip[kIdDof] = true;
        g_skip[kIdFocus] = true;
    }
    rt.registerFunction(kMove, moveWithoutVeil);
    std::printf("[sor] vignette off%s%s (classSCREFC::move 0x%06X)\n", noHaze ? ", haze off" : "",
                noDof ? ", depth of field off" : "", kMove);
    std::fflush(stdout);
}
