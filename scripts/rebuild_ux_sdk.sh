#!/usr/bin/env bash
set -euo pipefail
SDK="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="${1:?usage: rebuild_ux_sdk.sh REPO_ROOT FRESH_OUTPUT}"
OUT="${2:?provide a fresh directory under REPO_ROOT/out/}"
python3 -I "$SDK/relocate_ux_sdk.py" "$SDK"
export PATH="$SDK/clang/bin:$PATH"
unset SAILBOAT_MODPOST SAILBOAT_BUILTIN_CHECK
bash "$REPO/scripts/wsl/build_sched_assist.sh" "$SDK/out" "$SDK/clang/bin" "$OUT" "$SDK/source"
python3 -I "$REPO/scripts/collect_ux_build.py" --kernel-out "$SDK/out" \
  --source-dir "$SDK/source" --dist-dir "$SDK/dist" --clang-bin "$SDK/clang/bin" \
  --module "$OUT/sailboat_sched_assist.ko" --output "$OUT/verified-package" --no-sdk
