#!/usr/bin/env bash
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$REPO/out/hans_host_tests}"
case "$OUT" in "$REPO"/out/*) ;; *) exit 1 ;; esac
mkdir -p "$OUT"
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$REPO/port/hans/src" "$REPO/tests/hans/test_protocol.c" -o "$OUT/test_protocol"
"$OUT/test_protocol"
python3 -I "$REPO/tests/hans/test_loader.py"
python3 -I "$REPO/tests/hans/test_tooling.py"
for script in "$REPO"/ksu/Sail_Hans/*.sh; do sh -n "$script"; done
bash -n "$REPO/scripts/wsl/build_hans.sh"
