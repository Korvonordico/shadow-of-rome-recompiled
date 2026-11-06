# sor-gs.dll: the video card part

`SorGS.cpp` turns the GS (the PS2's graphics chip) of the emulator [PCSX2](https://github.com/PCSX2/pcsx2) into a library. The game (`sor.exe`) loads it when it finds `sor-gs.dll` next to itself and the launcher chooses the video card. The game sends it everything it gives the GS (patch `0018`, `gs_pcsx2_mirror.cpp`), and PCSX2's GS draws the picture inside the game's window with DirectX 11, DirectX 12 or Vulkan. Without the DLL the game draws on the processor, as in 0.0.7.

What it adds around PCSX2's GS:

- it runs the GS on a thread of its own: the game queues the GS data at every vsync and goes on;
- PCSX2's fixes for this game, from its GameIndex (half-pixel offset and native scaling), and no deinterlacing (the game's own interlacing is switched off in the translated code, as in PCSX2's No-Interlacing patch);
- VSYNC with a steady pace: when the screen's rate is a whole multiple of the game's, the game's clock follows the screen's;
- the sharpness levels (anisotropic filtering and contrast-adaptive sharpening);
- the wide screens: wide while the game is played and in talks, 4:3 with black bars in the movies, the game's own scenes, the pause menu and the title menus;
- texture replacements in `textures\SLES-52950\replacements`, next to the game;
- its log in `gpu-data\logs\sor-gs.log` (with `SORGS_LOG=1`).

The launcher sets everything at Play, through environment variables: `SOR_GRAPHICS` (`gpu <scale 1-4> <1 DirectX 11, 2 DirectX 12, 3 Vulkan>` or `cpu`), `SOR_WIDESCREEN` (0 4:3, 1 16:9, 2 21:9), `SORGS_VSYNC`, `SORGS_SHARPNESS` (0 off, 1 low, 2 high), `SORGS_OSD` and `SORGS_MODS` (0 or 1).

## Building it

On Windows, with Microsoft's compiler tools. Tested on one PC only, with Windows 11 and Visual Studio 2022 Build Tools.

1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) (Community, or the Build Tools) with "Desktop development with C++" and the "C++ Clang tools for Windows" part. CMake and Ninja come with it. Install also [Git for Windows](https://git-scm.com/download/win) and [7-Zip](https://www.7-zip.org) in their usual folders: the dependencies script uses them.

2. Open "x64 Native Tools Command Prompt for VS 2022" from the Start menu. In the folder that holds `shadow-of-rome-recompiled`, get PCSX2 at the commit this was made with, and add this folder and the patch:

   ```
   git clone https://github.com/PCSX2/pcsx2.git
   cd pcsx2
   git checkout 1ccd6a8dc3574771c138849039d71951a3a1c175
   git apply ..\shadow-of-rome-recompiled\patches\pcsx2\0001-sor-gs-in-pcsx2.patch
   xcopy /E /I ..\shadow-of-rome-recompiled\sor-gs sor-gs
   copy ..\shadow-of-rome-recompiled\patches\pcsx2\build-deps-sor.bat .github\workflows\scripts\windows\
   ```

3. Build PCSX2's libraries once. They go into the `deps` folder:

   ```
   .github\workflows\scripts\windows\build-deps-sor.bat
   ```

4. Build the DLL:

   ```
   cmake -B build-sor -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_PREFIX_PATH=%CD%\deps -DENABLE_QT_UI=OFF -DENABLE_TESTS=OFF -DDISABLE_ADVANCE_SIMD=ON
   cmake --build build-sor --target sor-gs
   ```

5. Put next to `sor.exe`:
   - `build-sor\sor-gs\sor-gs.dll`;
   - from `deps\bin`: `SDL3.dll`, `dxcompiler.dll`, `freetype.dll`, `jpeg62.dll`, `libpng16.dll`, `libsharpyuv.dll`, `libwebp.dll`, `lz4.dll`, `plutosvg.dll`, `plutovg.dll`, `ryml.dll`, `shaderc_shared.dll` (for Vulkan), `z.dll` and `zstd.dll`;
   - from `bin\resources`, the folders `shaders` and `fonts`, into a folder called `resources`.

The ready-made package on the Releases page holds the same files, built this way: with a `sor.exe` you built yourself, you can also copy them from there.

The DLLs need the [Microsoft Visual C++ Redistributable](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist) (x64), which most PCs already have. Without it the game cannot load `sor-gs.dll` and draws on the processor.

## Licence

`SorGS.cpp` is PCSX2's GS put to use: GPL-3.0, like PCSX2 and like the rest of this project.
