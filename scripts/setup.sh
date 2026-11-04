#!/bin/bash
# Builds the game from your own disc image, from start to finish.
# Usage (from WSL, in the repository's folder): scripts/setup.sh /mnt/c/path/to/game.iso
#
# What it does:
#   1. patches PS2Recomp and builds its tools (ps2_recomp, ps2_analyzer) in _tools
#   2. reads from your image, into disco/: the executable, SYSTEM.CNF and MODULES
#   3. builds the function map, into mappa/ and analisi/, from the executable's own
#      debug information and from what ps2_analyzer recognizes
#   4. translates the executable into C++, in gen/main
#   5. builds _build/sor.exe for Windows (needs llvm-mingw, see the README)
#   6. builds the launcher, _build/ShadowOfRome.exe, with the C# compiler that ships with Windows
#
# Everything it writes is ignored by git and stays on your PC. JOBS sets the compile jobs.
set -euo pipefail
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ISO="${1:-}"
[ -f "$ISO" ] || { echo "Usage: scripts/setup.sh /mnt/c/path/to/game.iso"; exit 1; }
ISO="$(realpath "$ISO")"
ELF_SHA1=e56561c4b73fd6e01f63c1157bb7ce457fdb65d0  # SLES_529.50 of the PAL disc this was made on
cd "$R"

echo "== 1/6 PS2Recomp and its tools"
[ -f thirdparty/PS2Recomp/CMakeLists.txt ] || git submodule update --init thirdparty/PS2Recomp
scripts/apply_patches.sh
cmake -S thirdparty/PS2Recomp -B _tools -DCMAKE_BUILD_TYPE=Release \
  -DPS2X_BUILD_RECOMP=ON -DPS2X_BUILD_ANALYZER=ON -DPS2X_BUILD_RUNTIME=OFF \
  -DPS2X_BUILD_STUDIO=OFF -DPS2X_BUILD_TEST=OFF > _tools.log 2>&1
cmake --build _tools -j"${JOBS:-6}" --target ps2_recomp ps2_analyzer >> _tools.log 2>&1

echo "== 2/6 Files from your disc"
mkdir -p disco/MODULES
python3 strumenti/iso_estrai.py "$ISO" estrai SLES_529.50 disco/SLES_529.50
python3 strumenti/iso_estrai.py "$ISO" estrai SYSTEM.CNF disco/SYSTEM.CNF
python3 strumenti/iso_estrai.py "$ISO" elenca | cut -f1 | grep '^/MODULES/' | while read -r f; do
  python3 strumenti/iso_estrai.py "$ISO" estrai "$f" "disco/MODULES/$(basename "$f")"
done
if [ "$(sha1sum disco/SLES_529.50 | cut -d' ' -f1)" != "$ELF_SHA1" ]; then
  echo "WARNING: this is not the executable of the PAL disc SLES-52950 this port was made on."
  echo "The addresses in the code will not match: it will probably not work."
fi
python3 strumenti/elf_per_pc.py disco/SLES_529.50 disco/SLES_529.50.pc

echo "== 3/6 Function map"
mkdir -p mappa analisi gen/main
python3 strumenti/dwarf1.py disco/SLES_529.50 mappa
python3 strumenti/buchi.py disco/SLES_529.50 mappa/funzioni.tsv mappa
_tools/ps2xAnalyzer/ps2_analyzer disco/SLES_529.50 analisi/analyzer.toml > analisi/analyzer.log 2>&1
python3 strumenti/funzioni_extra.py disco/SLES_529.50 analisi/analyzer.log mappa/funzioni_extra.tsv
cp config/main.toml analisi/main.toml
python3 strumenti/stubs_da_analyzer.py analisi/analyzer.toml mappa/sdk_nomi.tsv analisi/main.toml
python3 strumenti/crea_csv.py mappa mappa/funzioni.csv mappa/sdk_nomi.tsv

echo "== 4/6 Translation to C++"
_tools/ps2xRecomp/ps2_recomp analisi/main.toml > analisi/recomp.log 2>&1
grep -E "Functions processed|Unhandled|Warnings" analisi/recomp.log || true

echo "== 5/6 Build for Windows (5 to 15 minutes, log in _build.log; the first time it also downloads and builds FFmpeg)"
pc/compila_pc.sh configura || { tail -20 _build.log; echo "BUILD FAILED, see _build.log"; exit 1; }
[ -f _build/sor.exe ] || { echo "BUILD FAILED, see _build.log"; exit 1; }

echo "== 6/6 The launcher (_build/ShadowOfRome.exe)"
# .\build.bat, not build.bat: with NoDefaultCurrentDirectoryInExePath set, cmd does not look in the current folder.
if (cd launcher && cmd.exe /c '.\build.bat' nopause); then
  echo "Done. Start the game with _build/ShadowOfRome.exe (Play), run.bat or scripts/run.sh."
else
  echo "Done: _build/sor.exe. The launcher was not built: double-click launcher\\build.bat in Windows."
  echo "You can also start the game with run.bat or scripts/run.sh."
fi
