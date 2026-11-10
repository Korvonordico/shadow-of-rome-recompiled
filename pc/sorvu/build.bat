@echo off
rem sor-vu.dll (pc/sorvu + pc/kzvu, PCSX2's microVU), MSVC x64, against the PCSX2 checkout used for sor-gs
rem (sor-gs/README.md). Run from "x64 Native Tools Command Prompt for VS 2022":  build.bat <PCSX2 folder>
if "%~1"=="" (echo usage: build.bat ^<PCSX2 folder^> & exit /b 1)
cd /d "%~dp0"
if not exist build\build.ninja cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DKZVU_PCSX2_DIR="%~1" || exit /b 1
cmake --build build --target sor-vu || exit /b 1
echo sor-vu.dll is in %~dp0build
