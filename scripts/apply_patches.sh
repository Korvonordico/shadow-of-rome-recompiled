#!/bin/bash
# Applies patches/ps2recomp to the thirdparty/PS2Recomp submodule, in name order.
# The names of the applied patches are kept in thirdparty/PS2Recomp/.applied-patches,
# so running this twice is safe, and after a git pull only the new patches are applied.
# To undo them: git -C thirdparty/PS2Recomp checkout -- . && git -C thirdparty/PS2Recomp clean -fd
set -e
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
S="$R/thirdparty/PS2Recomp"
M="$S/.applied-patches"
touch "$M"
for p in "$R"/patches/ps2recomp/*.patch; do
  n=$(basename "$p")
  if grep -qxF "$n" "$M"; then
    echo "already applied: $n"
  elif git -C "$S" apply "$p" 2>/dev/null; then
    echo "$n" >> "$M"
    echo "applied: $n"
  else
    echo "Could not apply $n. To start again from a clean copy of PS2Recomp, run:"
    echo "  git -C thirdparty/PS2Recomp checkout -- . && git -C thirdparty/PS2Recomp clean -fd"
    echo "then run this again."
    exit 1
  fi
done
