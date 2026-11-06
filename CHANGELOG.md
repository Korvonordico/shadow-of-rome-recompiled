# Changelog

Version numbers count the big steps of the port.

## 1.9 (6 November 2026)

The picture moves to the video card. **The game has not been played to the end yet: it still needs full testing.**

### The picture

- **Drawn by the video card:** the GS (the PS2's graphics chip) of the emulator [PCSX2](https://github.com/PCSX2/pcsx2), built as a library of its own, `sor-gs.dll` (source in `sor-gs/`, build steps in [sor-gs/README.md](sor-gs/README.md)). It draws inside the game's window with DirectX 11, DirectX 12 or Vulkan, at 1x to 4x the PS2's resolution. The drawing on the processor of 0.0.7 is still there: GRAPHICS, RENDERER, PROCESSOR. Measured by script in the forest: with the video card the game used 136-219% of the processor (out of 600% on 6 cores), against 420-454% when the processor drew as well.
- **60 frames per second:** FRAME RATE 60, the default, starts the game in its own 60 Hz mode. Tested by hand: smooth with VSYNC on DirectX 12.
- **VSYNC**, on by default: each picture stays on screen for the same time, so the movement is steady even when the screen's rate is not a multiple of 60 (tested by hand on a 180 Hz screen). Windows 11 no longer slows down the game's timer when its window is covered or hidden, which gave short pauses.
- **A steady picture:** PCSX2's fixes for this game (from its GameIndex) and PCSX2's "No-Interlacing" patch for it. The picture no longer moves half a line up and down at every frame, and the thin lines are gone. Tested by hand.
- **SHARPNESS:** OFF (as on the PS2), LOW (the default: 8x anisotropic filtering and 40% contrast-adaptive sharpening) or HIGH (16x and 80%). Tested by hand.
- **SCREEN:** 4:3, 16:9 or 21:9 (21:9 with the video card only). While you play, the game's camera is widened (the 16:9 values are the ones of PCSX2's widescreen patch for this disc, by sergx12); the movies, the game's own scenes, the pause menu and the title menus stay 4:3, with black bars. Talks and tutorial pages stay wide: they stop the game with the wide view on screen. 21:9 was not made for this game: some views may show flaws. 16:9 tested by hand, before the talks were made wide; the wide talks and 21:9 checked by script only, or not at all.
- **OSD:** frames per second, frame times and the video card's load on screen (PCSX2's).
- **Texture packs**, as in PCSX2: pictures put in `textures\SLES-52950\replacements` (with the names of PCSX2's dumps) replace the game's own. MODS turns them off. Checked by script with one test picture, not with a real pack.
- **Full screen:** OPTIONS, DISPLAY, or F11 / Alt+Enter while you play. Tested by hand.

### The game

- **Fixed: the voices ran ahead of the lips** in the movies. The sound followed the PC's clock and the pictures followed the game's: each time the game stopped for a moment (when the PC was busy with something else, for example), the pictures lost that moment and the sound did not, so the gap grew a little each time. The sound now follows the game's clock (`iop_spu2.cpp`, `EeScheduler.cpp`; `SOR_AUDIO_ORA=1` brings the old behaviour back). Measured by script, with the game stopped for 60 ms every 2 seconds: before, the sound moved more than 2 seconds away from the pictures in 2 minutes; now it stays where it was, with the same 30-60 ms in the speakers' queue as before. If the game cannot keep its speed, the sound now pauses with it instead of running on. Tested by hand in a movie: the voices are in time with the lips.
- **Fixed: long dark stripes on the floor between the characters.** They were the characters' shadows. The game sends their data with `sceDmaSendI` in interleave mode, which PS2Recomp handled as a normal chain: the corners arrived shifted. Patch `0018`. Tested by hand: the shadows look right.

### The launcher

- GRAPHICS, OPTIONS, CONTROLS and MODS are open. Each page saves only with APPLY. CREDITS is gone.
- CONTROLS, for the controller: swap ✕ and ○, and invert the camera (the right stick) up and down or left and right. Not tested by hand yet.
- The version number is larger, in gold, at the bottom right.

### The package and the source

- The package holds `sor-gs.dll`, the libraries it needs and their licences (in `licenses`).
- A test tool of mine (a picture and a short report when the game freezes) is not in the package any more. 0.0.7 had it inside without its source; its licence is not the GPL, so it stays in my local builds only.
- New in the repository: patch `0018` for PS2Recomp, `sor-gs/` and `patches/pcsx2/` for the video card, `pc/widescreen_gen.py` (the wide camera, 60 Hz at start and the steady picture, written into the translated code after each translation).
- From now on the version numbers simply go up: 0.0.7 was followed by 1.9.

## 0.0.7 (5 November 2026)

### The launcher

- **A ready-made package**, to play without building anything: the launcher and the game's program, built for Windows. The first Play asks for your disc image and reads from it, into `disco` next to the launcher, the files the game needs (the executable, `SYSTEM.CNF` and the IOP modules), as `scripts/setup.sh` does. It checks that the image is the European disc (the executable's SHA-1) and says so when it is not. Checked by script on the disc this port was made on: the files it writes are identical, byte for byte, to the ones `scripts/setup.sh` writes. Tested by hand: the game starts from the package.
- `ShadowOfRome.exe --read-disc <image> <folder>` does the same reading without opening the window (for tests).
- A build made with `scripts/setup.sh` keeps working as before: the launcher uses `..\disco` when it is there.

## 0.0.6 (4 November 2026)

### The game

- **The flat grey shape on the sand near the camera, in the arenas, is gone** (patch `0017`). It was a hole, not a colour problem: the game draws the parts of the floor closest to the camera in a separate way, and it decides which parts need it from a flag of the PS2's vector unit. The translated code never set that flag, so those parts were never drawn and the background showed through. The same check is used in 22 places in the game (models, lights, blood, shadows, enemies appearing), so some of those may look different now too. Checked by script on screenshots in the arenas. Tested by hand: the game starts from the package.
- **That way of drawing no longer slows the game to 4 frames per second** (patch `0017`): its small programs for the vector unit now run in the fast code too. Measured by script, one run each: 47.9 frames per second instead of 48.4 on a walk, 44.3 instead of 45.5 in a fight (the two fights were not identical). The small loss comes from the floor that is drawn now.

### The launcher

- `launcher/build.bat` works on a fresh copy of the repository. The path to the C# compiler in it was broken, so it always said that the compiler was not found.
- The launcher shows its version, in its bottom right corner and in the window title, and has its icon.
- **Play** says what is missing if the game has not been built yet. If the game closes within 3 seconds, the launcher comes back, says so, and asks for the disc image again at the next Play.
- `scripts/setup.sh` builds the launcher at its end, so after the build you can start the game with `_build/ShadowOfRome.exe`.

### For tests (off unless you turn them on)

- `gstutti` in the test remote control writes every drawing primitive of 4 frames to a file; `SOR_SPU_TRACCIA` traces the sound streams; `SOR_VU1VERIFICA=N` starts the vector unit comparison after N seconds.
- The test remote control no longer crashes the game when a script reads an address near `0xffffffff` (change `0018`, in `pc/sor_prove.cpp`: it is not a PS2Recomp patch, so it has no file in `patches/`). The bounds check wrapped around and let it read outside the game's memory.

### Documentation

- The README is rewritten for someone who arrives from zero.
- `patches/README.md` describes patches `0014` and `0015`, which were missing from its table, and `0017`.

## Before 0.0.6

The earlier versions were published as commits, without a changelog, and only 0.0.1 has a tag. The main steps:

- 30 October 2026: the game boots to the main menu and [every menu works](https://github.com/Korvonordico/shadow-of-rome-recompiled/commit/a7d60ac), with the first launcher ([v0.0.1](https://github.com/Korvonordico/shadow-of-rome-recompiled/commit/ff5988f)). The same day, [the first arena in 3D](https://github.com/Korvonordico/shadow-of-rome-recompiled/commit/1ded5af).
- 31 October: [speed, keyboard and sound](https://github.com/Korvonordico/shadow-of-rome-recompiled/commit/da84f05), then [full speed from the menu to the first fight](https://github.com/Korvonordico/shadow-of-rome-recompiled/commit/5166610), [saves and the 60 Hz choice](https://github.com/Korvonordico/shadow-of-rome-recompiled/commit/0953bc9), [the camera](https://github.com/Korvonordico/shadow-of-rome-recompiled/commit/e203b35).
- 1 November: [the music](https://github.com/Korvonordico/shadow-of-rome-recompiled/commit/e5f3a00), [the movies, and no more freeze after the First Match](https://github.com/Korvonordico/shadow-of-rome-recompiled/commit/0764cc5), [water surfaces](https://github.com/Korvonordico/shadow-of-rome-recompiled/commit/109e604), [story points and long sessions](https://github.com/Korvonordico/shadow-of-rome-recompiled/commit/2605e18).
- 2 November: [no more grey screen and beep after the City Arena title card](https://github.com/Korvonordico/shadow-of-rome-recompiled/commit/1afd1b9) (patch `0016`).
