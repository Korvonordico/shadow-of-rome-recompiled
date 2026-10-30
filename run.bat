@echo off
rem Starts Shadow of Rome in a window. Drag your disc image onto this file, or:
rem   run.bat "C:\path\to\game.iso" [seconds]
rem With a number of seconds it closes by itself after that time.
if "%~1"=="" goto usage
pushd "%~dp0_build"
set SOR_FINESTRA=1
set SECONDS_LIMIT=%~2
if "%SECONDS_LIMIT%"=="" set SECONDS_LIMIT=0
sor.exe ..\disco\SLES_529.50.pc "%~1" %SECONDS_LIMIT%
popd
goto :eof
:usage
echo Usage: run.bat "C:\path\to\game.iso" [seconds]
echo Or drag your disc image onto this file.
pause
