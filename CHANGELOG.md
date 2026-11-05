# Changelog

Version numbers count the big steps of the port. Each digit goes from 0 to 9 and then carries over: 0.0.9 is followed by 0.1.0.

## 0.0.7 (not released yet)

### The launcher

- **A ready-made package**, to play without building anything: the launcher and the game's program, built for Windows. The first Play asks for your disc image and reads from it, into `disco` next to the launcher, the files the game needs (the executable, `SYSTEM.CNF` and the IOP modules), as `scripts/setup.sh` does. It checks that the image is the European disc (the executable's SHA-1) and says so when it is not. Checked by script on the disc this port was made on: the files it writes are identical, byte for byte, to the ones `scripts/setup.sh` writes. Not tested by hand yet.
- `ShadowOfRome.exe --read-disc <image> <folder>` does the same reading without opening the window (for tests).
- A build made with `scripts/setup.sh` keeps working as before: the launcher uses `..\disco` when it is there.

## 0.0.6 (4 November 2026)

### The game

- **The flat grey shape on the sand near the camera, in the arenas, is gone** (patch `0017`). It was a hole, not a colour problem: the game draws the parts of the floor closest to the camera in a separate way, and it decides which parts need it from a flag of the PS2's vector unit. The translated code never set that flag, so those parts were never drawn and the background showed through. The same check is used in 22 places in the game (models, lights, blood, shadows, enemies appearing), so some of those may look different now too. Checked by script on screenshots in the arenas. Not tested by hand yet.
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
