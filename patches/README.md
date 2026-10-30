# Patches to PS2Recomp

`thirdparty/PS2Recomp` is a submodule at upstream commit `e5636981` of
[ran-j/PS2Recomp](https://github.com/ran-j/PS2Recomp). It stays at that commit;
`scripts/apply_patches.sh` applies the files in `ps2recomp/` to the checkout, in
name order, and skips a patch that is already applied. Lines added by this project
are marked `sor`. Some comments are in Italian.

To undo them: `git -C thirdparty/PS2Recomp checkout -- .`

| Patch | What it changes |
|---|---|
| `0001-iop-module-args-rpc-and-pacing.patch` | IOP modules start with `start(argc, argv)` as on the console (Capcom's sound driver `SNDDRVHD.IRX` needs it). `sceSifBindRpc` from the IOP to a game server marks the client as bound, and `sceSifCallRpc` from the IOP to the game clears the reply and finishes at once, without running the game's server. The IOP runs in packets of at least 512 cycles, and one instruction counts as 4 cycles: this took the game from a quarter of its speed to about the console's speed. Thread, RPC server and module lists for the debug report. |
| `0002-runtime-stacks-heap-display-and-reports.patch` | `PS2Runtime::setAsyncCallbackStackRegion`: the callback stacks started at the top of RAM, where this game keeps its main thread's stack, and overwrote it. `PS2Runtime::pinGuestHeap`: the runtime's heap stays in a fixed area, and the game's `SetupHeap` no longer moves it over the game's data. The image is always shown in 4:3. `SOR_FOTO`, `SOR_FOTO_OGNI` and `SOR_FOTO_VRAM` save frames during tests. Messages when the runtime stops. |
| `0003-gs-image-upload-and-interlaced-display.patch` | `sceGsExecLoadImage` and `sceGsExecStoreImage` multiplied the VRAM address by 8, so textures landed in the wrong place and the screen stayed black. With INT=1 and FFMD=1 the buffer has half of the display's lines: half are read and each one is shown twice. `SOR_FOTO_FBP` shows another buffer during tests. Draw counts for the debug report (`sorGsRapporto`). |
| `0004-pad-modes-and-scripted-keys.patch` | `scePadInfoMode` reports two modes, digital and analog: with only one the game says the analog controller is not connected. `SOR_TASTI` presses keys by itself during tests. |
| `0005-menu-images-64bit-branches-and-keyboard.patch` | The translator tested `BLTZ`, `BGEZ`, `BLEZ` and `BGTZ` on 32 bits only, while the EE's registers have 64: the game's software floating-point subtraction went wrong for some values (the volume 10 in Options was drawn off screen). `sceGsExecLoadImage` reads the game's real `sceGsLoadImage` packet. The VIF1 interpreter skips the NOP and DIRECT after an image header instead of taking them as data (Options and Load Game were black). `DrawTriangle` computes Z exactly (dots on the Options page). The keyboard works even with a controller connected, and the log says which controller it sees. `SOR_TASTI` accepts `m<seconds>:<key>` (seconds from the first skipped movie), and L1 and R1. Debug: `SOR_GIFDUMP`, `SOR_GIFDUMP_DA`, `SOR_DMADUMP`, `SOR_IMGDUMP`. |
| `0006-first-arena-3d-saves.patch` | Stage 2. The callbacks of `sceMpegDemuxPssRing` run inside the call, as on the console: queued, the video stream overran its buffer and overwrote the player's model. DMA chains are no longer cut at 4096 tags (the VIF kept a wrong `CYCLE` for the next frame). New threads start with `vf0` = (0,0,0,1). The VIF's V4-5 format widens 5 bits to 8 (bone weights in the character microcode were 8 times too small). `sceMcOpen` with `SCE_CREAT` alone creates the file, so saving works. With `SOR_TASTI`, a real controller is ignored, and the script can move the left stick (`ssu`, `sgiu`, `ssx`, `sdx`). Debug: `SOR_VU1DUMP`, `SOR_VIFDUMP`, `SOR_MPGDUMP`. |

Outside the patches, in `pc/`: `sor_hle.cpp` finds files by reading the disc image's
ISO9660 index, and skips the movies (the build has no FFmpeg); `main_sor.cpp` places the
stacks and the heap; `sor_profilo.cpp` is a sampling profiler (`SOR_PROFILO`, read with
`strumenti/profilo.py`). In `strumenti/stubs_da_analyzer.py`, `InitAlarm` is left to the
runtime: translated, it made every `SetAlarm` fail. `sceGsSetDefLoadImage`, instead, stays
translated: the game sends by itself the packet that function fills. `main_sor.cpp` also stops
Esc from closing the window: Esc is ○. `sceVu0*` also stay translated: the runtime's `sceVu0MulMatrix` multiplies in the opposite order from the original.
