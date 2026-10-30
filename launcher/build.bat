@echo off
rem Builds the launcher with the C# compiler that ships with Windows (.NET Framework 4).
rem Output: _build\ShadowOfRome.exe, next to sor.exe, plus the logo it shows.
set CSC=%WINDIR%\Microsoft.NET\Framework644.0.30319\csc.exe
if not exist "%CSC%" (echo The .NET Framework 4 compiler was not found. & pause & exit /b 1)
if not exist "%~dp0..\_build" mkdir "%~dp0..\_build"
pushd "%~dp0"
"%CSC%" -nologo -target:winexe -out:..\_build\ShadowOfRome.exe Launcher.cs
copy /y ..\docs\logo.png ..\_build\logo.png >nul
popd
