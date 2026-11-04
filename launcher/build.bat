@echo off
rem Builds the launcher with the C# compiler that ships with Windows (.NET Framework 4).
rem Output: _build\ShadowOfRome.exe, next to sor.exe, plus the logo it shows.
rem "build.bat nopause" does not wait for a key at the end (scripts/setup.sh uses it).
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%CSC%" set "CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
if not exist "%CSC%" (
  echo The .NET Framework 4 compiler was not found.
  if not "%~1"=="nopause" pause
  exit /b 1
)
if not exist "%~dp0..\_build" mkdir "%~dp0..\_build"
pushd "%~dp0"
"%CSC%" -nologo -target:winexe -win32icon:ShadowOfRome.ico -out:..\_build\ShadowOfRome.exe Launcher.cs
if errorlevel 1 (
  popd
  echo The launcher was not built.
  if not "%~1"=="nopause" pause
  exit /b 1
)
copy /y ..\docs\logo.png ..\_build\logo.png >nul
popd
echo Done: _build\ShadowOfRome.exe
if not "%~1"=="nopause" pause
