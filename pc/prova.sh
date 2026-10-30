#!/bin/bash
# A quick development round, after scripts/setup.sh has run once:
# stubs -> function map -> translation into gen/nuovo -> copy into gen/main of only
# the changed files -> build (only the touched groups) -> run sor.exe for N seconds
# -> summary of the log, with the function names.
# Usage (from WSL): SOR_ISO=/path/to/game.iso pc/prova.sh NAME SECONDS [notrad]
# With "notrad" the translation is skipped (build and run only).
# CONFIGURA=1 configures CMake again; RIGHE is how many log lines to show.
# Log of the run: _NAME.log in the repository's folder.
P="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NOME=${1:-run}
SEC=${2:-60}
cd "$P" || exit 1
[ -n "$SOR_ISO" ] || { echo "set SOR_ISO to your disc image"; exit 1; }
if [ "$3" != notrad ]; then
  python3 strumenti/stubs_da_analyzer.py analisi/analyzer.toml mappa/sdk_nomi.tsv analisi/main.toml || exit 1
  python3 strumenti/crea_csv.py mappa mappa/funzioni.csv mappa/sdk_nomi.tsv || exit 1
  sed 's#^output = "gen/main"#output = "gen/nuovo"#' analisi/main.toml > analisi/_nuovo.toml
  mkdir -p gen/nuovo
  da=$(date +%s)
  _tools/ps2xRecomp/ps2_recomp analisi/_nuovo.toml > analisi/recomp.log 2>&1 || { echo "RECOMP FALLITO"; tail -5 analisi/recomp.log; exit 1; }
  grep -E "Functions processed|Unhandled|Warnings" analisi/recomp.log
  python3 strumenti/copia_cambiati.py gen/nuovo gen/main gen/vecchi $da || exit 1
fi
pc/compila_pc.sh ${CONFIGURA:+configura}
grep "^FINE" _build.log; echo "errori di compilazione: $(grep -c 'error:' _build.log)"
grep "error:" _build.log | head -5
[ -f _build/sor.exe ] || exit 1
cd _build
# SOR_* variables reach sor.exe (a Windows program) only if listed in WSLENV.
export WSLENV=SOR_TRACCIA:SOR_FINESTRA:SOR_FOTO:SOR_FOTO_OGNI:SOR_TASTI:SOR_SENTINELLA:SOR_SENTINELLA_OFF:SOR_FOTO_FBP:SOR_FOTO_VRAM:SOR_PROFILO
./sor.exe ../disco/SLES_529.50.pc "$(wslpath -w "$SOR_ISO")" $SEC < /dev/null > ../_$NOME.log 2>&1
echo "sor.exe uscito con $?"
cd ..
echo "righe del registro: $(wc -l < _$NOME.log)"
grep -v "^INFO" _$NOME.log | cut -c1-260 | awk '!seen[$0]++' | python3 strumenti/dove.py mappa/funzioni.csv | head -${RIGHE:-90}
