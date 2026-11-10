# Changelog

Version numbers count the big steps of the port.

## 2.6 (not released yet)

### Smoother play

- **PCSX2's VU1 recompiler (microVU) now runs the game's 3D programs.** The PS2's VU1 turns the models into triangles. Until now the port ran its programs through PS2Recomp's interpreter, which took most of the graphics thread's time. Now it uses microVU from PCSX2, through the kzvu library made for the Killzone port ([drakolordx7/KillzoneRecomp](https://github.com/drakolordx7/KillzoneRecomp), GPL-3.0). It is 2.7 times faster on the same scene. A comparison mode ran both on 389,737 programs and found no difference in the pictures, the memory or the registers. It needs a processor with AVX2 (most PCs from 2015 on); without it the game goes back to the old interpreter by itself. OPTIONS > SPEED TESTS > VU1 ENGINE can choose the old one or the comparison.
- **Fewer stutters with VSYNC.** The clock that kept the pictures in step with the screen woke up late or skipped refreshes, so some pictures waited one refresh too many. It now runs at the highest priority. In tests, pictures on time went from about 95% to 98%.
- **G-SYNC and FreeSync screens are recognised by themselves.** On a variable refresh screen the game waited for a refresh that only came with the next picture. Now each picture is shown as soon as it is ready, and the game keeps its own 60 a second. Tested by hand on a FreeSync screen: 98.7 to 99.3% of the pictures on time, against about 95% before.
- **The video card's shaders are made at the start.** The first time a scene used a new effect, making it cost 20 to 160 ms in the middle of play. The port now keeps a list of the effects used (gpu-data\cache\sor_tfx_pipelines.bin) and makes them all at the start, in less than a tenth of a second. The package comes with a list filled by playing the first levels.
- **SMOOTH AUDIO** (OPTIONS): the music and voices are stretched a little instead of stopping for an instant when the game is late, with SoundTouch (LGPL-2.1, the same as PCSX2). On by default.
- **Fixed: the sound slowed down while the game was paused.** Tested by hand.

### The picture

- **FIELD OF VIEW** (GRAPHICS): the playing camera sees wider: ORIGINAL, +10%, +20%, +30%. The camera is the game's own, made wider, and the edges of the view follow. The movies and the game's own scenes stay as they are. Tested by hand at +30%.
- **Crouching (L2), the mouse turns the view** as when standing, left, right, up and down (before, it worked there like a controller's stick).
- **Running in a diagonal** with a controller no longer turns into a slow walk: today's sticks reach only about 71% on each axis in a diagonal, the PS2's reached almost the full value; the left stick is now stretched from a circle to a square. Thanks to [@n0l4n666](https://github.com/n0l4n666) for the report. The keyboard does not change.
- **Fixed: the OSD never showed** (in 2.5 too, where it was on by default). It now shows the frames a second and the video card's statistics.

### First start

- **New first-time settings:** 4x, FIELD OF VIEW +30%, a window, TOGGLE on L2 (one press crouches, the next stands up), and UPSCALE FIX "special", which removes a soft double outline seen at 4x and 8x on the characters and on far trees. The DEBUG page stays there for everyone, all off at first.

### For testing

- **SPEED TESTS** (OPTIONS): VU1 ENGINE (PCSX2, the old one, or both compared), TIMINGS, which writes where the time goes in the log, and UPSCALE FIX, PCSX2's other fixes for the edges of the picture. TIMINGS slows the game a little: leave it off to play.

### Known problems

- The water in the Germanian Forest (Chapter 1) is still wrong.
- A short stutter can remain at the start of a scene, when the game loads new textures, and during loading.

## 2.5 (10 November 2026)

### The movies

- **In time with their sound at 60 frames per second.** Thanks to [@n0l4n666](https://github.com/n0l4n666) for the report ([#3](../../issues/3)). The game shows a movie picture every second vblank, 30 a second by its own clock, while the movies are made at 29.97 with sound at 48 kHz; with VSYNC on a screen that is not a multiple of 60 (181.6 Hz here) the clock ran a little faster still. Now, while a movie plays, the game follows the movie's own speed and checks each picture against the sound actually played: when they drift apart it shows a picture again or leaves one out. After the movie it goes back to its normal speed. Tested by hand: a long movie at 60 frames per second on a 180 Hz screen, voices and lips together to the end.
- **Start skips every movie.** The original game lets you skip a movie only after you have seen it once. A Start already held before the movie does not count. Asked for by [@n0l4n666](https://github.com/n0l4n666) too. Tested by hand.

### Controls

- **Every key and button can be changed** in CONTROLS: a KEYBOARD page and a CONTROLLER page. Choose a row and press the new key or button. If it is already used, a second press swaps the two. Delete leaves an action with no key, Backspace cancels. One key per action. The keys of the port itself (F8, F11, F12) cannot be taken. Tested by hand.
- **Mouse buttons and the wheel** can be given to actions too: left, right, middle, the two side buttons, wheel up and down. The click that catches the pointer presses nothing. Tested by hand.
- **The mouse turns the camera,** directly like in a PC game, not through the right stick: no speed limit, and the camera stays where you leave it, up, down and to the side. MOUSE LOOK, MOUSE SPEED (1 to 10) and MOUSE Y in CONTROLS. Click in the window to start, Alt frees the pointer. In the movies, the game's own scenes and the places where the game holds the camera, the game keeps it. The controller's right stick works as before. Tested by hand.
- **New standard keys:** ✕ left mouse button, ○ E, ▢ Q, △ F, L1 Left Ctrl, R1 right mouse button, L2 C, R2 G, L3 and R3 Numpad 8 and 9, Start Esc. W A S D, the arrow keys, I J K L and Tab as before.

### The picture

- **Wide screens:** only the movies stay 4:3. The menus, the pause menu and the game's own scenes fill the screen, stretched, with no more switching between 4:3 and wide. Tested by hand in 16:9 and 21:9.
- **TOGGLE** (CONTROLS): choose one button (for example the one that crouches): one press holds it down, the next lets it go. It works with its key, its mouse button and its controller button. Off at first.
- **RESOLUTION 6x and 8x:** the game drawn six or eight times the PS2's size and shrunk to your screen, the smoothest edges there are. They need a strong video card.
- **ANTIALIASING** (GRAPHICS): FXAA, PCSX2's own filter, softens the jagged edges (and the text a little), with DirectX 11, DirectX 12 and Vulkan. On by default. Asked for by [@n0l4n666](https://github.com/n0l4n666) ([#4](../../discussions/4)).
- **VIGNETTE** (GRAPHICS): the game lays grain, a colour haze and an anti-flicker filter over the picture, as on the PS2. Off by default; ON brings them back. Tested by hand.
- **Fixed: after Alt+Tab the picture stayed black** until the window was moved. Tested by hand.

### Sound

- **BGM VOLUME and SE VOLUME** in OPTIONS change the music (with the movies) and the effects, the same values as in the game's own Sound options: a change made in the game shows in the launcher too. Tested by hand.

### DEBUG

- **A DEBUG page** in OPTIONS (Ctrl+D also opens it), for testing and for finding bugs: INVINCIBLE, WEAPONS, ONE HIT (any hit kills, even you), MONEY, UNLOCK ALL, NO FILMS, FREE CAMERA (F8: the game stops, you fly around with the mouse, W A S D, Q / E, the arrow keys or a controller, and Ctrl+1 to 9 saves up to nine places), WIREFRAME (F7 shows the game's polygons as wires, with DirectX 11 only) and ENGINE PANEL (F12). MONEY and UNLOCK ALL change your save for good: they ask for a second press. **Use a separate save.** The game's log says which options were on. Tested by hand.

### The launcher

- **A new look:** a frame, Rome in the background with the Colosseum and fire, falling ash (BACKGROUND), menu sounds (MENU SOUNDS), and a Latin word under each choice. The icon is a gold aureus of the emperor Nerva (photo by the American Numismatic Society, public domain).
- **UPDATES** (OPTIONS) asks GitHub whether a newer version is out; Enter opens its page.
- **Error messages say what to do,** in plain words.
- **New first-time settings:** DirectX 12, 4x, sharpness high, FXAA, 16:9, 60 frames per second, VSYNC and OSD on, VIGNETTE off, full screen, music and effects at 5 of 10, the mouse turning the camera, every DEBUG option off. Your own `launcher.ini` keeps your choices.
- **A log of each game** in `logs` (the last 5), with your Windows user name taken out. If you report a problem, attach `run-1.txt`.

### Bug reports

- **A black box** in the game: it keeps the last pictures and log lines in memory and, when the game freezes, crashes, shows a black screen for a long time or slows down, saves them in `blackbox` as one picture of numbered frames and a short text report (the last 5 are kept). F9 saves one by hand. `blackbox=0` in `settings.ini` turns it off.
- **F3 and F6, test tools** for the water in the Germanian Forest: F3 saves what the game draws in the next 4 frames (with the processor only), F6 runs one part of the game in its exact, slower version. They found that the water is drawn the same way by both renderers; the cause is still open.

### Known problems

- **The water in the Germanian Forest** (Chapter 1) shows a blurred copy of the screen, not the forest reflected. See the README.

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
