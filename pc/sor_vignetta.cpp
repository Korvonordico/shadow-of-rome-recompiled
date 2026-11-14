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

// Called once after loadELF (pc/main_sor.cpp), after the other replacements.
void sorVignettaInstalla(PS2Runtime &rt)
{
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
