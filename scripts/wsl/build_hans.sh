#!/usr/bin/env bash
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KDIR="${1:?usage: build_hans.sh KDIR CLANG_BIN [OUT]}"
CLANG_BIN="${2:?missing matching clang bin directory}"
OUT="${3:-$REPO/out/build/hans}"
[ -f "$KDIR/Makefile" ] && [ -f "$KDIR/Module.symvers" ] && [ -f "$KDIR/.config" ] || {
  printf 'missing prepared KDIR, .config or Module.symvers\n' >&2
  exit 1
}
[ -x "$CLANG_BIN/clang" ] || { printf 'missing clang\n' >&2; exit 1; }
case "$OUT" in "$REPO"/out/*) ;; *) printf 'OUT must be inside repo out/\n' >&2; exit 1 ;; esac
mkdir -p "$OUT"
[ ! -e "$OUT/Makefile" ] || { printf 'use a new output directory\n' >&2; exit 1; }
cp "$REPO"/port/hans/src/* "$OUT"/
export PATH="$CLANG_BIN:$PATH"
make -C "$KDIR" M="$OUT" ARCH=arm64 LLVM=1 PAHOLE=true modules
"$CLANG_BIN/llvm-readelf" -h "$OUT/sailboat_coloros_hans.ko"
"$CLANG_BIN/llvm-nm" -u "$OUT/sailboat_coloros_hans.ko" > "$OUT/imports.txt"
"$CLANG_BIN/llvm-readelf" -p .modinfo "$OUT/sailboat_coloros_hans.ko" > "$OUT/modinfo.txt"
sha256sum "$OUT/sailboat_coloros_hans.ko" > "$OUT/sha256.txt"
printf 'built=%s\n' "$OUT/sailboat_coloros_hans.ko"
