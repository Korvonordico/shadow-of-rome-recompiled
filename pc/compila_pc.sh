#!/bin/bash
# Builds the PS2Recomp runtime and the translated code for Windows (64-bit).
# Build tree in _build, log in _build.log (both in the repository's folder).
# Usage (from WSL): pc/compila_pc.sh [configura]
# JOBS sets the parallel compile jobs (default 6; the build needs a lot of memory).
set -o pipefail
P="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
B="$P/_build"
LOG="$P/_build.log"
: > "$LOG"
# The movies need a small part of FFmpeg. It is built once, on the Linux disk (spaces in a Windows path would break it).
bash "$P/pc/compila_ffmpeg.sh" "$HOME/.cache/shadow-of-rome-ffmpeg" >> "$LOG" 2>&1 || { echo "FFMPEG FAILED, see the end of $LOG"; exit 1; }
if [ ! -f "$B/Makefile" ] || [ "$1" = configura ]; then
  cmake -S "$P/pc" -B "$B" -DCMAKE_TOOLCHAIN_FILE="$P/pc/mingw.cmake" -DCMAKE_BUILD_TYPE=Release \
    -DSOR_GENERATED_DIR="$P/gen/main" -DSOR_PS2RECOMP="$P/thirdparty/PS2Recomp" >> "$LOG" 2>&1 || { echo "CONFIGURE FAILED" >> "$LOG"; exit 1; }
fi
start=$(date +%s)
cmake --build "$B" -j"${JOBS:-6}" -- -k >> "$LOG" 2>&1
rc=$?
echo "FINE $rc dopo $(( $(date +%s) - start )) s" >> "$LOG"
exit $rc
