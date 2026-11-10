# ReShade files of the package

The Windows package ships ReShade 6.8.0 (`ReShade64.dll`, from the setup at https://reshade.me, BSD-3-Clause) with these files.
The launcher (MODS > RESHADE, off at first) copies `ReShade64.dll` as `dxgi.dll` next to `sor.exe` when it is on, and removes that copy when it is off.

- `ReShade.ini`: where the effects are, the preset to load, Home opens the menu.
- `ShadowOfRome.ini`: the author's preset, the one loaded at first.
- `reshade-shaders/Shaders/SweetFX`: SweetFX by CeeJay.dk (MIT); FXAA left out (NVIDIA notice, not free to share). CRT.fx is GPL-2.0-or-later.
- `reshade-shaders/Shaders/ReShade.fxh`: crosire, CC0-1.0. `ReShadeUI.fxh`: written for this port (MIT).
