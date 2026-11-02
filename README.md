<p align="center">
  <img src="docs/logo.png" width="60%" alt="Shadow of Rome Recompiled">
</p>

<h1 align="center">Shadow of Rome Recompiled</h1>

Native PC port of Shadow of Rome (PS2) through static recompilation.

<p align="center">
  <img src="docs/first-arena-by-hand.gif" width="70%" alt="Agrippa fighting in the first arena, played by hand on this build">
</p>
<p align="center">
  <a href="docs/first-arena-by-hand.mp4">Watch one minute of the first arena, played by hand on this build</a> (MP4, 9 MB, no sound)
</p>
<p align="center"><b>This is only a sample to show that the game starts and runs.</b> It is not fully playable yet: it still has many problems. The video was recorded before the camera fix, so in it the camera moves in close behind the shoulder, which it no longer does. See <a href="#status">Status</a> below.</p>

**Boots to the main menu on Windows, every menu and submenu works, the movies play, and the first arena can be played at full speed (50 Hz in a window). City Arena Rounds II and III and the first stage with Octavianus load and run, but so far they were only reached by forcing each win in a test.**

This is an experiment. I can't promise I'll finish it, for personal reasons, and it will take a long time before the game is playable: I work on it with limited time and resources, so there are no deadlines.

If someone is already working on a port of this game, or is further ahead, please let me know as soon as possible by opening an [issue](../../issues), so I don't do useless work and can spend my time on something that hasn't been done yet.

You need your own legal copy of the game (PAL SLES-52950). No game files are included.

## Made with AI

This work was done with the help of AI: Claude (by Anthropic), guided by Francesco ([Korvonordico](https://github.com/Korvonordico)). Claude wrote most of the code, the scripts and the patches, and ran the tests.

It is not a one-off prompt. Claude runs inside [Hermes](https://github.com/NousResearch/hermes-agent), an agent platform I configured myself: a personal setup with my own skills, rules, notes and safety checks, built up over time. What that setup did for this port:

- a skill for PS2 game porting, written from my earlier port attempts, so the agent follows tested steps instead of guessing;
- [Quotient](https://github.com/Korvonordico/quotient-claude-tokens), my own tool to estimate, split and schedule long jobs: reaching the menu took 4 planned sessions, about 15 million weighted tokens;
- written handoff notes after every session, so each one starts from where the last one stopped.

Added for the menu work, because that is what the problems needed:

- scripted key presses timed from a point in the game, plus contact sheets of screenshots (`strumenti/mosaico.py`) that Claude reads to see each screen: every menu result below was checked on a picture;
- a script that plays by itself, with key presses, reading positions and life from the game's memory: this is how the First Match was played to its end;
- a small script that sends real key presses to the window with Windows' `SendInput` (`strumenti/tasti_veri.py`);
- logging wrapped around single functions of the running game, to follow a wrong value back to its cause.

An example of this debugging: a volume of 10 was invisible in the Options menu. Claude followed it through the number printing and the game's software floating-point code, down to a translated branch that looked at only 32 bits of a 64-bit register. That happened in an unattended session; my part was the plan for this stage and the rule that a result counts only when it has been checked directly.

Added for the first arena:

- a copy of the game's whole memory taken at a chosen function call, and watches on single memory words that report which call changed them;
- dumps of what the game sends to the PS2's vector unit, and a small disassembler for its microcode (`strumenti/vudis.py`);
- the original machine code of a function, read from the game's executable and compared with what PS2Recomp's replacement does.

Another example: the screen went black after the difficulty choice, and the first clue pointed at the game's timer code. That was only the victim. Following the memory watches back showed the movie's video data overflowing its buffer and overwriting the player's model, because PS2Recomp ran the movie callbacks later instead of during the call. Reading the broken model then corrupted the rest of the memory. After that fix, the 3D took four more separate fixes. One of them: in the character microcode, bone weights were 8 times too small, and that was found by reading the microcode itself.

Added for the speed and the sound:

- a test remote control inside the program (`pc/sor_prove.cpp`): keys injected at the point where the program reads the keyboard, without touching the real one, plus screenshots, a sampling profile and per-thread timings on command;
- a list of every drawing pass of one frame (`SOR_GSDUMP`), and one number that measures how wrong a screenshot is, so that many tries can be compared without looking at each picture;
- a comparison of the new fast drawing code with the original, primitive by primitive (`SOR_GSCONFRONTO`), and with the VU1 interpreter (`SOR_VU1VERIFICA`);
- a recording of the sound output to a file (`SOR_AUDIO_PCM`), read as numbers: nobody can listen from inside a script.

Another example: after the drawing moved to several threads, part of the first 3D scene turned blue and green. It looked like a race between threads, but it stayed the same with one thread. Trying which group of passes fixed the picture showed that one pass read a page and the next one wrote it: the check looked only at the opposite order (read after write). The threads take the screen in bands, so a band further down could read what a band above had just overwritten.

Added for the speed of the first arena:

- a cost table of the drawing by kind of primitive and by segment (`SOR_GSCLASSI`), and a run with no pixels drawn at all (`SOR_GSSENZADISEGNO`), which showed that the drawing was the real limit and not the game's thread;
- the same screenshots taken with the published build and the new one, in the same scene, compared number by number;
- a trap on one page of the game's memory (`SOR_TRAPPOLA`) that reports which code writes to it;
- a profile-guided build, tried and dropped: it gained about 5%, not worth the complication.

Another example: a change that made the drawing faster also put a light veil over the picture, in 63% of the pixels. It was faint, and the comparison with the published build is what showed it. It stayed the same with one thread, so it was not a race. Trying the image uploads in groups led to two of them, which had been separating drawing passes by accident: the game writes the same memory as 16-bit colour and then as depth, and in those two formats the same memory lands on different screen lines, so the two passes fell into different bands and lost their order. The fix checks for exactly that case. The picture now matches the published build, apart from 0.005% of the pixels.

Added for the saves and the controls:

- commands in the test remote control that read and write the game's memory while it runs (`=peek`, `=poke`): one of them forces a win, so the save screen can be reached without playing a whole round, and it is written down in Status above that the win was forced;
- reading the camera's position from the game's memory while it runs, to measure when and how far it jumps.

Another example: in the fights the camera kept jumping in close behind Agrippa's shoulder, and it looked like a fault in the controls. The measurements showed the camera moving about 400 units away from where it should be in a quarter of a second. Reading the code that runs in that room led to one instruction, the square root: PS2Recomp took its value from the wrong register, so every distance computed that way was wrong. The instruction appears 182 times in the game, and a small script confirmed that the register PS2Recomp was reading is the unused one in all 182.

Added for the music (temporary code, not kept in the repository):

- a report, once per second, of the IOP's threads with the cycles each one used, how often each semaphore was signalled, and the state of the game's music stream in the IOP's memory;
- a trap on single words of the IOP's memory, reporting the code that writes to them.

Another example: the title screen had no music. Reading the game's code suggested a problem in the music stream, and that was only the victim. The report showed one sound thread using 100% of the IOP's time, so the music thread never got a turn. That thread was spinning because its semaphore number had been overwritten, and the trap showed who did it: a copy in the sound driver. PS2Recomp counted the size of that copy (`_wmemcopy`) in words, while the driver passes bytes, so it copied 4 times too much. After that the music was still silent, for a second reason: the input volume 0x7FFF was doubled and became -2.

Added for the movies and the freeze after the First Match:

- the trap on a memory word now reports only the code that really changes that word (`SOR_TRAPPOLA`), and the test remote control can also read the IOP's memory (`=ipeek`).

Another example: the movie's picture was black, with only its subtitles and one small square in a corner. The decoding was fine: the frames were real. The game builds a list of one upload per 16x16 block, and only the first one reached the graphics chip. The size in the list's first tag was double the right one, because one of PS2Recomp's replacements for a library function also updated it, on top of the game's own translated code. The DMA chain then read image commands as if they were its next tag, and stopped.

The work is planned in steps, every step is tested on the real game, and I decide what goes on and what gets published.

## Status

**What works**

- The whole executable is translated to C++ and built for Windows, with no errors.
- All the game's original IOP modules load and run in PS2Recomp's IOP simulator (the IOP is the PS2's second processor: it runs the sound, the disc and the memory cards).
- From the start to the menu, on screen: the language choice, Start Game and Change video mode (with the NTSC test), the memory card check, the violence warning, the CAPCOM logo, the title screen and the main menu.
- Every menu, opened and closed, checked on pictures:

| Menu | Works | Notes |
|---|---|---|
| New Game | Yes, up to the first arena | See below |
| Load Game | Yes | It finds a game saved after City Arena Round I and loads it. With no game saved it says "No data present": the file made at startup is not a game in progress |
| Secrets of Rome | Yes | Cheats and the Salvo Master List with its pages open and close. The other 6 entries are locked, as on a new game |
| Options: Subtitles / Tutorial, Sound, Controller, Violence | Yes | Each value changed and put back |
| Options: Screen | Yes | Brightness and Screen Position pages |
| Options: Control Explanation | Yes | All 3 pages |
| Options: Default, OK | Yes | Default puts the changed values back |

- **The first arena, in 3D:** after New Game and the difficulty choice come the trainer's briefing, the "First Match" map, the controls page, then Agrippa at the gate of the City Arena. Checked on pictures, with scripted keys.
- **Saving and loading:** the game saves after the First Match ("First Battle - Fini", see below) and offers to save after City Arena Round I. Tested on City Arena Round I: "Save Successful", the game closed and opened again, Load Game finds "City Arena Round I", "Load Successful", and the game goes on from City Arena Round II. **The win was forced by writing to the game's memory in the test:** nobody has won that round by playing yet. Starting from an empty memory card also works, both when you create the save file at the start and when you skip it and save later (that second way was followed up to the last confirmation). The saves are in `disco/mc0/`.
- **The 60 Hz choice is remembered:** after you choose it in Change video mode and press Start Game, the game writes `settings.ini` next to `sor.exe`, and the next start begins in that mode. See [Settings](#settings). Tested with scripted keys, not yet from the launcher.
- The keyboard, with real key presses sent to the window by a script (Enter, the down, left and right arrows, Space, X, V). The arrow keys and W A S D move the left stick, and with them Agrippa walks in the first arena (checked with keys injected inside the program, not on a real keyboard).
- **Speed at 50 Hz, the game's PAL mode (the European version):** full speed from the menu to the fight, in a window, on the test PC with nothing else running. Measured in vblanks per second, which here means frames per second: 50 is full speed. The previous build is on the same PC:

| Part | Previous build | Now | Lowest second now |
|---|---|---|---|
| Menu | 50 | 50 | 49.9 |
| Trainer's briefing | 43 | 50 | 42.6 (one second, once) |
| First room | 41.8 | 50 | 49.7 |
| Arena | 36.9 | 49.9 | 49.8 |
| Fight | 37 | 50 | 49.9 |

- **The sound:** Francesco listened to it by hand on an older build: it sounds fairly good. The sound mix has changed since (see the reverb below) and has not been listened to again.
- **The music:** the title screen, the menu and the arena now have their music, which used to be silent. Checked as numbers on a recording of the output, not by ear: the level goes from silence to about 3000-4000 RMS on the title screen and in the menu, with no clipping. In 30 seconds of title music, the short holes of exact silence went from 155 to 0.
- **The movies:** the opening movie plays with its subtitles and sound, 30 pictures per second at 50 Hz and at 60 Hz, and goes on to the title screen at its end. Enter skips it. The title screen's attract movie and the movie after New Game play too. Checked on pictures and as numbers on a recording of the output (all 178 seconds of sound, with no holes in a 60-second sample), not by ear. Only these movies have been tried. They are decoded with a small part of [FFmpeg](https://ffmpeg.org), and the picture reaches the screen as a texture, as in the original game.
- **The First Match, played to its end:** this win was not forced. A test script (not published) reads positions and life from the game's memory and presses the keys: it walks to the enemies, hits them, closes the tutorial pages and opens the doors with ○. All the waves die, including the last opponent (2816 life), and the game does the rest by itself: result screen, save, "Chapter 1: The Wickedness of Men", and the next stage, a Roman camp in a forest (area 050). Load Game then finds the save as "First Battle - Fini" and goes on to Chapter 1, even after closing the game. **The script had help:** it refilled Agrippa's life 18 times when it fell below 500 of 2048, because it fights badly and never guards. So nobody has won this fight without help yet. Without the refill, in one attempt Agrippa died with 1 enemy of the first group left, and in another he had 39 life left when that group was dead. The Game Over works: blood runs down the screen and Continue goes back to the waiting room. Over that whole session the median was 50 frames per second, and 5% of the seconds, loading included, were below 33 (not looked into).
- **The forest stage, played to its end, and what comes after:** this was not forced either, and the same script played it, with the same kind of help (Agrippa's life refilled once). The stage asks you to climb the hill at the far end of the camp, talk twice to the soldier by the fence, and choose "Inform the men that we are moving out." in his menu. If you press ✕ without moving down, the menu answers "Nothing, yet." and nothing happens. Then enemies appear; when they are dead, the game goes by itself to the Forum in Rome, with Octavianus, then to a street leading to the Senate House, then to the Germanian Forest with Agrippa. That is the order the game's own story follows. The tests skipped movies, so the Senate House was not seen: it may be a movie. The Germanian Forest has not been finished: the script fought through part of it and climbed a ladder (ladders work), then **the game froze**, with the same picture staying on the screen and the game's logic stopped. Something had written drawing data over most of the game's memory, starting at address 0x4000; the cause has not been found, and it has happened once in this build, after about 95 minutes of play. A second test, using a forced jump to the upper walkway of that stage, did not freeze in about 30 minutes. From that walkway ○ on a door leads to the next area (052), where the script killed the enemy and picked up the "Germanic Tribe Fort Key". It has not opened the fort's door yet. The picture in the forests is very dark: it has not been compared with the original.
- **The next arena, reached with one forced ending:** a different test forced only the end of the forest stage. The game then goes by itself to the waiting room, with a dialogue with the trainer, and after the script talks to the people there it goes on to the next arena (area 072), with 9 opponents and 4 statues. The script finished this fight with the same help (life refilled 24 times), and the game went back to the waiting room by itself. The story does not go there after the forest when played, so this only shows that the fight works. It is slow: a median of 32 frames per second in an earlier test, and 5% of the seconds below 20. Drawing still runs on the CPU; this is left for the move to the graphics card.
- **After the first arena:** with each win forced by writing to the game's memory (nobody has won these by playing), City Arena Rounds II and III and the first stage with Octavianus load and run at full speed, the game saves after each of them, and Octavianus walks with scripted keys. A forced win alone does not end the Octavianus stage: it asks to save and starts the stage again, because the game also needs to know which story point comes next (see below).
- **Later parts of the game, reached by jumping:** nobody has played this far. A test script writes to the game's memory to change area, the same way the game does it, without following the story. Of the first 93 areas tried, most load and show the place, including the chariot race, the forest camp and later arena fights, usually at or near full speed (9 were below 40 frames per second when measured). 10 stop on "Now Loading", 3 could not be entered, and in 1 the game closed by itself. It is not known yet whether these are real bugs or come from the jump, which does not change the playable character. Water surfaces used to break the picture and slow the game to a few frames per second: that is fixed.
- **The story order, walked with forced endings:** nobody has played this far either. The game keeps a table of its story points, in order. A test script writes a point's number to the game's memory and forces the end of the stage, and from there the game does the rest by itself: result screen, save, the next area. The script also writes which character should be used next. 31 points were tried, from the first Octavianus stage to area 0c0. 30 of them reach their area and keep running, and the pictures checked show real dialogues, story pages and the right character. Most ran at 40-50 frames per second; the slowest measured were 24, 35 and 39, each measured once. One point (area 031, scene 14) stops while loading every time, even from a fresh start: it is not known yet whether that comes from the forced ending. Two points (the last ones in area 082) moved on to area 061 by themselves a few seconds later, and it is not known why. Stages were entered, not played, so this does not show that each one can be finished. Area 081 stopped on "Now Loading" when jumped to, but loads this way, so that stop seems to have come from the jump.
- **Long sessions:** the game could close by itself ("EE invocation stack space exhausted") after many area changes in one session. Each game thread that needed one got a small block of memory, there is room for only 64, and they were never given back. They are now reused when a thread is deleted: in two test sessions, 30 and 26 blocks were asked for and only 4 had to be new each time. Neither session asked for more than 64, so a very long session has not been tested on the new version.
- A launcher, `ShadowOfRome.exe`: **Play** starts the game, **Quit** closes it. Options, Graphics, Controls, Mods and Credits are shown greyed out as "coming soon". It is built with the C# compiler that already ships with Windows, so nothing has to be installed (`launcher/build.bat`, source in `launcher/Launcher.cs`).

<p align="center">
  <img src="docs/launcher.png" width="70%" alt="The launcher: Play and Quit, the other entries greyed out as coming soon, and the project's logo">
</p>

<p align="center">
  <img src="docs/titolo-press-start.png" width="45%" alt="The title screen">
  <img src="docs/menu-principale.png" width="45%" alt="The main menu">
</p>
<p align="center">
  <img src="docs/load-game.png" width="23%" alt="Load Game">
  <img src="docs/secrets-of-rome.png" width="23%" alt="Secrets of Rome">
  <img src="docs/options-sound.png" width="23%" alt="Options, Sound">
  <img src="docs/options-controls.png" width="23%" alt="Options, Control Explanation">
</p>

<p align="center">
  <img src="docs/first-arena-lanista.png" width="30%" alt="The first arena: the trainer's briefing">
  <img src="docs/first-arena.png" width="30%" alt="The first arena: Agrippa seen from behind, in front of the trainer and the gate">
  <img src="docs/first-arena-walking.png" width="30%" alt="The first arena: Agrippa after walking, with the City Arena sign">
  <img src="docs/first-arena-fight.png" width="30%" alt="The arena: Agrippa and the gladiators fighting">
</p>

**What does not work, or is not checked**

- **60 frames per second is not steady yet.** The game's own 60 Hz mode (Change video mode, the NTSC test) can be chosen and works. Two runs on the test PC: menu 60 and 60, trainer's briefing 59.9 and 57.9, first room 59 and 58, arena 43.9 and 50.9, fight 51.9 and 54.9. With the drawing switched off, every part holds about 60, so the drawing is what is missing: it runs on the processor and fills about 15 screens of pixels per frame. Steady 60 needs the drawing moved to the graphics card, which has not been started.
- **Speed needs the cores:** in the arena the drawing threads use 70-87% of their cores at 50 Hz. If other programs keep the processor busy, it will slow down. It has been measured on one PC only.
- **The camera:** it no longer jumps in close behind the shoulder, and the first scene, the first room and the arena now show the wider view. The cause was a wrong square root in PS2Recomp (see `0010` in [patches/README.md](patches/README.md)). The same scripted test, pressing ✕ in the first room: before, the camera moved from distance 101 to 526; now from 94 to 121. A second fix (`0011`, float to integer conversion that rounded instead of truncating) removed the bars that stood in front of the view in the arena. With both fixes, short drops appear in the first room and on the way to the arena (35 to 39 frames per second for 1 to 6 seconds): the cause has not been looked for. Tested with scripted keys and compared with the old build on screenshots, not yet by hand, and not against the original game on a PS2. The same fix changes every distance the game computes this way, so enemies may behave differently too: this has not been checked one by one.
- **Fighting is checked by a script, not by a skilled player:** a test script steers Agrippa with key presses and fights the whole First Match (see "The First Match, played to its end" above). Agrippa's life bar goes down when he is hit, enemies lose life and die, and weapons and shields can be picked up (seen on pictures and in the game's memory). Whether the fights feel right, and whether the timing is fair, can only be judged by someone playing. Francesco played this build by hand in the first arena for about three minutes and fought there (the video above). After the First Match the game used to freeze sometimes while loading the next round: garbage read as a position by the lock-on code made the wall check write outside its grid. That is fixed (see `0013` in [patches/README.md](patches/README.md)).
- **Reverb is written but not heard:** the game's own sound driver runs in the IOP simulator and drives a simulated SPU2 sound chip (48 voices, envelopes, DMA, streaming, reverb). In the first arena the game keeps the reverb's output volume at 0, so it changes nothing there. Forcing that volume in a test: no clipping and nothing runs away, and the sounds of the fight get up to about 15% stronger. Nobody has listened to it yet, and the zones where the game turns it on have not been reached.
- **Played by hand only for a short time:** Francesco reached the arena by hand on the slower build, and played a few minutes of the first arena by hand on this one, with a PS4 controller (seen by Windows as "Wireless Controller"): it works for walking and fighting. Everything else was tested with scripted keys.
- **A flat grey shape in the arenas:** near the camera, a flat grey shape often covers part of the sand. It has not been looked into: it is left for the move to the graphics card.
- **A small difference left in the picture:** drawing on several threads and drawing strictly in order differ slightly (0.85% of the pixels by more than 16 out of 255, in the trainer's briefing). The published build has the same difference. It has not been explained yet.
- **It has run on one PC only** (Windows 11, Intel i5-8600K).

## What you need

- Your own image of the disc: Shadow of Rome, PAL, SLES-52950, as an `.iso` file.
- Windows 10 or 11 (64-bit), with [WSL](https://learn.microsoft.com/windows/wsl/install) and Ubuntu. Tested on Windows 11 with Ubuntu 26.04.
- A CPU with SSE4.1 (nearly every PC since about 2009), and about 2 GB of free disk space.
- An internet connection during the build: it downloads FFmpeg's source code, for the movies.
- In Ubuntu: Git, CMake 3.24 or newer, a C++ compiler and Python 3.11 or newer:

  ```bash
  sudo apt install build-essential cmake git python3
  ```

- [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases), the compiler for Windows: download the `ucrt-ubuntu` `x86_64` archive and unpack it, then rename its folder to `~/llvm-mingw`. Tested with clang 23.1.4. If you keep it somewhere else, set the `LLVM_MINGW` variable to its folder.

## How to build

Run these in the Ubuntu terminal.

1. Go to a folder on a Windows drive, so that Windows can start the game from there, and download the project with its submodule:

   ```bash
   cd /mnt/c/Users/YOUR_NAME
   git clone --recurse-submodules https://github.com/Korvonordico/shadow-of-rome-recompiled.git
   cd shadow-of-rome-recompiled
   ```

2. Build everything from your disc image. In WSL, `C:\Games\game.iso` is written `/mnt/c/Games/game.iso`:

   ```bash
   scripts/setup.sh "/mnt/c/Games/game.iso"
   ```

   This takes about 25 minutes on a 6-core PC. It patches PS2Recomp and builds its tools, reads the executable and the IOP modules from your image, makes the function map from the executable's own debug information, translates the game to C++, downloads and builds the part of FFmpeg the movies need (in `~/.cache/shadow-of-rome-ffmpeg`) and builds `_build/sor.exe`. If the build fails, the reason is at the end of `_build.log`.

Nothing it creates is committed to Git: `disco/`, `mappa/`, `analisi/`, `gen/`, `_tools/` and `_build/` stay on your PC. The game's large files are not copied: they are read from your image while you play, so keep the image where it is.

## How to start it

- **With the launcher (Windows):** double-click `launcher/build.bat` once, then start `_build/ShadowOfRome.exe`. Choose **Play**: the first time it asks where your `.iso` file is and remembers it in `_build/launcher.ini`. Arrows and Enter, or the mouse.
- **Or drag your `.iso` file onto `run.bat`.**
- **Or from the Ubuntu terminal:**

  ```bash
  scripts/run.sh "/mnt/c/Games/game.iso"
  ```

Close it with the X of the window.

## Keys

The keyboard always works, with or without a controller connected:

| PS2 | Keyboard |
|---|---|
| D-pad | Arrow keys |
| Left stick | W A S D, or the arrow keys (which move the D-pad too) |
| Right stick | I J K L |
| L3 / R3 (pressing the sticks) | F / O |
| ✕ | X or Space |
| ○ | C or Esc |
| ▢ | Z |
| △ | V |
| L1 / R1 | Q / E |
| L2 / R2 | Left Shift / Right Shift |
| Start | Enter |
| Select | Tab |

In most menus ✕ confirms and △ goes back. Some menus read only the D-pad, so use the arrow keys there.

To walk, W A S D is the safer choice: according to the game's list of commands, the D-pad drops your helmet and weapons, and the arrow keys press it too. This has not been seen happening in a test yet. R3 centres the camera behind Agrippa.

## Settings

The port keeps one setting of its own, in `_build/settings.ini`, next to `sor.exe`:

```
video=60
```

- `video=60` starts the game in its 60 Hz mode, `video=50` in its 50 Hz mode (PAL, the original speed of this version).
- The game writes this file by itself when you choose a mode in Change video mode and press Start Game. You don't need to edit it.
- With no file, nothing changes: the game starts at 50 Hz as the original does, and no file is created unless you choose 60 Hz.
- To go back to the original behaviour, delete the file.

## From the start to the menu

1. Press Enter, then X to choose the language, then X on Start Game.
2. The first time, with an empty memory card, the game offers to create a save file: press the left arrow to choose Yes, then X. If you choose No, it asks "Begin game anyway?": left arrow, then X.
3. After the CAPCOM logo the opening movie starts: Enter skips it. Then press Enter at the title screen.

## From the menu to the first arena

Choose New Game and a difficulty. The briefing pages move on with X. The game asks for the left stick to walk: with a controller that is the stick, and on the keyboard the arrow keys or W A S D. Talk to the man with the mace (○, which is C or Esc on the keyboard) to enter the arena.

## How it works

[PS2Recomp](https://github.com/ran-j/PS2Recomp) rewrites each instruction of the PS2 executable as C++. That C++ is then built for Windows together with a runtime that plays the part of the rest of the console: memory, operating system, disc reader, graphics, sound and I/O processor. The game's original code runs, translated; it is not rewritten by hand.

- `pc/`: our part of the program, and the build files.
- `strumenti/` ("tools"): Python scripts that read the disc and make the function map.
- `patches/`: our changes to PS2Recomp, each one explained in [patches/README.md](patches/README.md).
- `thirdparty/PS2Recomp`: the PS2Recomp submodule, at the commit we use.
- `scripts/`: the scripts to build and start the game.

Many names and comments in the code are in Italian. `pc/prova.sh` is the quick round used during development.

## Credits

- [ran-j/PS2Recomp](https://github.com/ran-j/PS2Recomp), the recompiler and runtime this port is built on. We use upstream commit `e5636981` directly, with our patches, and no fork of it.
- [victor-takai/rogue-galaxy-recomp](https://github.com/victor-takai/rogue-galaxy-recomp), used as a reference for how to organize the project and for how to use PS2Recomp. It is built on [Sinan Karakaya's fork](https://github.com/Sinan-Karakaya/PS2Recomp) of PS2Recomp.
- [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) by Martin Storsjö, the compiler used to build for Windows.
- [FFmpeg](https://ffmpeg.org) (LGPL), the MPEG-2 decoder for the movies. It is downloaded and built by `pc/compila_ffmpeg.sh`: no part of it is in this repository.
- [PCSX2](https://github.com/PCSX2/pcsx2) (GPL-3.0), the PS2 emulator: the reverb follows its `SPU2/Reverb.cpp` and `SPU2/ReverbResample.cpp`, including the filter tables.

Shadow of Rome belongs to Capcom. The logo is the official Capcom logo, from the copy uploaded to [SteamGridDB](https://www.steamgriddb.com/logo/17719) by [DARTH-HMD](https://www.steamgriddb.com/profile/76561198842967508), with "Recompiled" added. If Capcom asks, it will be removed. This project is not connected to Capcom or Sony.

## License

GPL-3.0, the same as PS2Recomp, which the built program links. See [LICENSE](LICENSE).

---

If you'd like to support me, have a look at my other projects on my profile: [github.com/Korvonordico](https://github.com/Korvonordico).
