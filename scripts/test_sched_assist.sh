#!/usr/bin/env bash
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$REPO/out/ux_host_tests}"
case "$OUT" in "$REPO"/out/*) ;; *) exit 1 ;; esac
mkdir -p "$OUT"
export SAILBOAT_TEST_OUT="$OUT"
python3 -I "$REPO/tests/sched_assist/test_adapter.py"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
  -fsanitize=address,undefined -I"$REPO/port/sched_assist/src" -I"$OUT" \
  "$REPO/tests/sched_assist/test_ux_core.c" -o "$OUT/test_ux_core"
"$OUT/test_ux_core"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
  -fsanitize=address,undefined -pthread -I"$REPO/port/sched_assist/src" -I"$OUT" \
  "$REPO/tests/sched_assist/test_ux_paths.c" -o "$OUT/test_ux_paths"
"$OUT/test_ux_paths"
python3 -I -m py_compile "$REPO/scripts/adapt_sched_assist.py" "$REPO/tests/sched_assist/test_adapter.py"
bash -n "$REPO/setup.sh" "$REPO/scripts/wsl/build_sched_assist.sh"
sh -n "$REPO/ksu/sailboat_oppo_perf/service.sh"
