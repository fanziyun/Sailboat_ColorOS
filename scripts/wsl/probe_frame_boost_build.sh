#!/usr/bin/env bash
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KDIR="${1:?usage: probe_frame_boost_build.sh KDIR KSRC CLANG_BIN ADAPTED_SA OUT}"
KSRC="${2:?missing matching kernel source}"
CLANG_BIN="${3:?missing matching compiler}"
SA="${4:?missing adapted sched_assist directory}"
OUT="${5:?missing fresh output directory}"
case "$OUT" in "$REPO"/out/*) ;; *) printf 'OUT must be inside repo out/\n' >&2; exit 1 ;; esac
[ ! -e "$OUT" ] || { printf 'use a fresh output directory\n' >&2; exit 1; }
[ -x "$CLANG_BIN/clang" ] || { printf 'missing matching compiler\n' >&2; exit 1; }
python3 -I "$REPO/scripts/prepare_frame_boost_probe.py" \
  --upstream "$REPO/src_upstream/oplus_sm8550" --sched-assist "$SA" --output "$OUT"
python3 -I "$REPO/scripts/audit_frame_boost_target.py" \
  --source "$KSRC" --kernel-output "$KDIR" --report "$OUT/target-contract.json"
"$CLANG_BIN/clang" --version > "$OUT/compiler.txt"
export PATH="$CLANG_BIN:$PATH"
make -C "$KDIR" M="$OUT" ARCH=arm64 LLVM=1 PAHOLE=true \
  KBUILD_BUILTIN=1 KBUILD_MODULES= built-in.a 2>&1 | tee "$OUT/compile.log"
"$CLANG_BIN/llvm-nm" -u "$OUT/built-in.a" > "$OUT/unresolved-symbols.txt"
printf 'Compile-only archive: %s\nNo module or device-loadable package was produced.\n' "$OUT/built-in.a"
