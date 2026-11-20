#!/usr/bin/env bash
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KDIR="${1:?usage: build_animation_boost.sh KDIR KSRC CLANG_BIN ADAPTED_SA OUT}"
KSRC="${2:?missing matching source}"
CLANG_BIN="${3:?missing matching clang}"
BASE="${4:?missing adapted minimal SA}"
OUT="${5:?missing fresh output}"
case "$OUT" in "$REPO"/out/*) ;; *) printf 'OUT must be inside repo out/\n' >&2; exit 1 ;; esac
[ ! -e "$OUT" ] || { printf 'use a fresh output directory\n' >&2; exit 1; }
[ -x "$CLANG_BIN/clang" ] || { printf 'missing compiler\n' >&2; exit 1; }
python3 -I "$REPO/scripts/prepare_animation_boost.py" \
  --upstream "$REPO/src_upstream/oplus_sm8550" --base-sa "$BASE" --output "$OUT"
python3 -I "$REPO/scripts/audit_frame_boost_target.py" \
  --source "$KSRC" --kernel-output "$KDIR" --report "$OUT/target-contract.json"
export PATH="$CLANG_BIN:$PATH"
make -C "$KDIR" M="$OUT" ARCH=arm64 LLVM=1 PAHOLE=true modules 2>&1 | tee "$OUT/compile.log"
"$CLANG_BIN/llvm-nm" -u "$OUT/sailboat_sched_assist.ko" > "$OUT/imports.txt"
"$CLANG_BIN/llvm-readelf" -p .modinfo "$OUT/sailboat_sched_assist.ko" > "$OUT/modinfo.txt"
printf 'Candidate module: %s\nNot verified for loading until kernel/image/CRC checks and device preflight pass.\n' "$OUT/sailboat_sched_assist.ko"
