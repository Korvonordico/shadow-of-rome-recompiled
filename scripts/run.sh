#!/bin/bash
# Starts the game in a window, from WSL.
# Usage: scripts/run.sh /mnt/c/path/to/game.iso [seconds]
# With a number of seconds it closes by itself after that time; without, close the window.
# The log is printed here: redirect it if you want to keep it.
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ISO="${1:-}"
[ -f "$ISO" ] || { echo "Usage: scripts/run.sh /mnt/c/path/to/game.iso [seconds]"; exit 1; }
WIN="$(wslpath -w "$(realpath "$ISO")")"
cd "$R/_build" || exit 1
export SOR_FINESTRA=${SOR_FINESTRA:-1}
# SOR_* variables reach sor.exe (a Windows program) only if listed in WSLENV.
export WSLENV=SOR_TRACCIA:SOR_FINESTRA:SOR_FOTO:SOR_FOTO_OGNI:SOR_TASTI:SOR_SENTINELLA:SOR_SENTINELLA_OFF:SOR_FOTO_FBP:SOR_FOTO_VRAM:SOR_PROFILO
exec ./sor.exe ../disco/SLES_529.50.pc "$WIN" "${2:-0}"
