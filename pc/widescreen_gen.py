#!/usr/bin/env python3
"""Widescreen 16:9 and 21:9 in the translated code (after every translation; run by compila_pc.sh, safe to repeat).

The PCSX2 patch for SLES-52950 (pcsx2_patches, SLES-52950_B1995E29.pnach, by sergx12) changes three constants:
the camera's horizontal field 0x3EEEEEEF -> 0x3F1F49F9 at 0x146c08/0x146c10 and 0x146e24/0x146e2c
(classCAMERA::make_view_mat), and 224.0 -> 168.0 at 0x1e757c (classAREA::area_init).
Here each constant becomes a choice made while the game runs: sorWidescreenOn() (pc/main_sor.cpp,
SOR_WIDESCREEN=1, set by the launcher), so the same sor.exe plays both ways. 21:9 (SOR_WIDESCREEN=2,
sorUltrawideOn()) widens the same constants by 64/27 / (4/3): 0x3F54629C and 224 -> 126.0."""
W16 = ("sorWidescreenOn() ? 16159 : 16110", "sorWidescreenOn() ? 18937 : 61167", "sorWidescreenOn() ? 17192 : 17248")
W21 = ("sorWidescreenOn() ? (sorUltrawideOn() ? 16212 : 16159) : 16110",
       "sorWidescreenOn() ? (sorUltrawideOn() ? 25244 : 18937) : 61167",
       "sorWidescreenOn() ? (sorUltrawideOn() ? 17148 : 17192) : 17248")
# 32:9 (SOR_WIDESCREEN=3, sorSuperwideOn()): 0x3F9F49F5 and 224 -> 84.0
W32 = ("sorWidescreenOn() ? (sorUltrawideOn() ? (sorSuperwideOn() ? 16287 : 16212) : 16159) : 16110",
       "sorWidescreenOn() ? (sorUltrawideOn() ? (sorSuperwideOn() ? 18933 : 25244) : 18937) : 61167",
       "sorWidescreenOn() ? (sorUltrawideOn() ? (sorSuperwideOn() ? 17064 : 17148) : 17192) : 17248")
# The gameplay camera tells the GS it is in use (sorWideCamera(), pc/main_sor.cpp). The films, the pause menu and
# the title menus tell it they stay 4:3 (sorFourThree(), FOUR_THREE below): the GS shows them 4:3 with black bars
# and everything else wide once the camera has been used after them. Talks and tutorial pages freeze the game with
# the wide view on screen, so they stay wide. A camera view with no gameplay camera in it (the game's own scenes)
# is 4:3 as well (gs_pcsx2_mirror.cpp).
WC = tuple(x.replace("sorWidescreenOn() ?", "sorWideCamera() ?", 1) for x in W32)
import pathlib
import sys

gen = pathlib.Path(sys.argv[1])
DECL = ("bool sorWidescreenOn(); // sor: widescreen 16:9 (pc/widescreen_gen.py)\n"
        "bool sorUltrawideOn(); // sor: widescreen 21:9 (pc/widescreen_gen.py)\n"
        "bool sorSuperwideOn(); // sor: widescreen 32:9 (pc/widescreen_gen.py)\n"
        "bool sorWideCamera(); // sor: the gameplay camera is in use (pc/widescreen_gen.py)\n"
        "void sorFourThree(); // sor: this view stays 4:3 on a wide screen (pc/widescreen_gen.py)\n"
        "void sorCameraView(); // sor: the camera makes a view (pc/widescreen_gen.py)\n"
        "int sorVideoMode(); // sor: 60 Hz (pc/widescreen_gen.py)\n"
        "bool sorInterlaceOn(); // sor: steady picture (pc/widescreen_gen.py)\n"
        "float sorFovScale(); // sor: FIELD OF VIEW, 1 = as on the PS2 (pc/widescreen_gen.py)\n")
EDITS = {
    "classCAMERA_make_view_mat_146aa0_0x146aa0.cpp": [
        ("SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)16110 << 16));",
         "SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)(" + WC[0] + ") << 16));", 2),
        ("SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) | (uint64_t)(uint16_t)61167);",
         "SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) | (uint64_t)(uint16_t)(" + WC[1] + "));", 2),
    ],
    # (the FIELD OF VIEW edit of make_view_mat is added below, after this table)
    # The camera's other view (make_view_matF, the same field at 0x146718 and 0x146934) stays as on the PS2, as
    # in the PCSX2 patch: the game's own scenes keep their 4:3 look. A translation widened here by an older
    # version of this script is put back.
    "classCAMERA_make_view_matF_1465b0_0x1465b0.cpp": [
        ("SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)(" + WC[0] + ") << 16));",
         "SET_GPR_S32(ctx, 2, (int32_t)((uint32_t)16110 << 16));", -1),
        ("SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) | (uint64_t)(uint16_t)(" + WC[1] + "));",
         "SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) | (uint64_t)(uint16_t)61167);", -1),
    ],
    "classAREA_area_init_1e7250_0x1e7250.cpp": [
        ("    // 0x1e757c: 0x3c044360  lui         $a0, 0x4360\n    ctx->pc = 0x1e757cu;\n"
         "    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)17248 << 16));",
         "    // 0x1e757c: 0x3c044360  lui         $a0, 0x4360\n    ctx->pc = 0x1e757cu;\n"
         "    SET_GPR_S32(ctx, 4, (int32_t)((uint32_t)(" + WC[2] + ") << 16));", 1),
    ],
    # 60 Hz (SOR_60HZ=N, from the launcher): the game's own video mode byte (0x551402, written by the
    # "Change video mode" screen) is set before classSYSTEM::init hands it to change_display_mode, so the
    # game starts in that mode instead of asking at every start. The game reads the byte itself to adapt.
    "classSYSTEM_init_1e9430_0x1e9430.cpp": [
        ("    SET_GPR_ZE32(ctx, 5, (uint8_t)FAST_READ8(0x551402u));",
         "    if (sorVideoMode()) FAST_WRITE8(0x551402u, sorVideoMode());\n"
         "    SET_GPR_ZE32(ctx, 5, (uint8_t)FAST_READ8(0x551402u));", 1),
    ],
    # Steady picture (PCSX2's "No-Interlacing" patch for SLES-52950, 0x1e9384 andi v0,v0,8 -> 0): the game
    # moves every other frame half a line up and down (interlacing); on a PC screen that makes the picture
    # shake and blur. sorInterlaceOn() (SOR_INTERLACE=1) brings the original back.
    "classSYSTEM_wbuf_swap_1e92d0_0x1e92d0.cpp": [
        ("    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) & (uint64_t)(uint16_t)8);",
         "    SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) & (uint64_t)(uint16_t)(sorInterlaceOn() ? 8 : 0));", 1),
    ],
}
# FIELD OF VIEW (SOR_FOV, pc/main_sor.cpp sorFovScale()): the gameplay camera's screen distance (camera+0x3C, read
# into $f25 at 0x146c04 and used for the view and the clip matrices) and the area's screen distance (224 / tan at
# 0x1e7584 in classAREA::area_init, the constant the widescreen also changes) are multiplied by 100 / percent.
EDITS["classCAMERA_make_view_mat_146aa0_0x146aa0.cpp"].append(
    ("    ctx->pc = 0x146c04u;\n    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 17), 60)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[25] = f; }",
     "    ctx->pc = 0x146c04u;\n    { uint32_t bits = READ32(ADD32(GPR_U32(ctx, 17), 60)); float f; std::memcpy(&f, &bits, sizeof(f)); ctx->f[25] = f * sorFovScale(); }", 1))
EDITS["classAREA_area_init_1e7250_0x1e7250.cpp"].append(
    ("    ctx->pc = 0x1e7584u;\n    { uint32_t bits = GPR_U32(ctx, 4); std::memcpy(&ctx->f[2], &bits, sizeof(bits)); }",
     "    ctx->pc = 0x1e7584u;\n    { uint32_t bits = GPR_U32(ctx, 4); std::memcpy(&ctx->f[2], &bits, sizeof(bits)); ctx->f[2] *= sorFovScale(); }", 1))
# Views that stay 4:3 on a wide screen: each frame they call sorFourThree() (pc/main_sor.cpp).
FOUR_THREE = ("classMOVIE_TASK_task_move_308ca0_0x308ca0", "classPAUSE_MENU_main_349390_0x349390",
              "classPAUSE_MENU_trans_349270_0x349270", "classTITLE_TASK_task_move_13fb60_0x13fb60",
              "classPALSELECT_TASK_move_start_menu_32afa0_0x32afa0")
for stem in FOUR_THREE:
    head = "void " + stem + "(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {\n"
    EDITS[stem + ".cpp"] = [(head, head + "    sorFourThree(); // sor: this view stays 4:3 on a wide screen\n", 1)]
# Every view the camera makes calls sorCameraView() (pc/main_sor.cpp): with no gameplay camera in it, the GS
# knows that one of the game's own scenes is playing.
for stem in ("classCAMERA_make_view_mat_146aa0_0x146aa0", "classCAMERA_make_view_matF_1465b0_0x1465b0"):
    head = "void " + stem + "(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime) {\n"
    EDITS[stem + ".cpp"].append((head, head + "    sorCameraView(); // sor: the camera makes a view\n", 1))
for name, edits in EDITS.items():
    p = gen / name
    s = p.read_text(encoding="utf-8")
    before = s
    for a, b, c, w in zip(W16, W21, W32, WC):  # a translation patched by an older version of this script
        if w not in s:
            s = s.replace("(" + c + ")", "(" + w + ")").replace("(" + b + ")", "(" + w + ")").replace("(" + a + ")", "(" + w + ")")
    for old, new, count in edits:
        if count < 0:  # put back: only when present
            s = s.replace(old, new)
            continue
        if new in s:
            continue  # already patched (the script may run again on the same translation)
        n = s.count(old)
        assert n == count, (name, old[:50], n)
        s = s.replace(old, new)
    for line in DECL.splitlines(keepends=True):
        if line not in s:
            i = s.index('#include "ps2_runtime.h"\n') + len('#include "ps2_runtime.h"\n')
            s = s[:i] + line + s[i:]
    if s == before:
        print("widescreen: already in", name)
        continue
    p.write_text(s, encoding="utf-8")
    print("widescreen: patched", name)
