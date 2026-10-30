#!/bin/bash
# Applies patches/ps2recomp to the thirdparty/PS2Recomp submodule, in name order.
# A patch that is already applied is skipped, so running this twice is safe.
# To undo them: git -C thirdparty/PS2Recomp checkout -- .
set -e
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for p in "$R"/patches/ps2recomp/*.patch; do
  n=$(basename "$p")
  if git -C "$R/thirdparty/PS2Recomp" apply --reverse --check "$p" 2>/dev/null; then
    echo "already applied: $n"
  else
    git -C "$R/thirdparty/PS2Recomp" apply "$p"
    echo "applied: $n"
  fi
done
