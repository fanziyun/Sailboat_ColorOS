#!/usr/bin/env bash
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
python3 -I "$REPO/tests/frame_boost/test_target_contract.py"
python3 -I "$REPO/tests/frame_boost/test_compile_probe.py"
for tree in oplus_sm8550 oplus_sm8850; do
  include="$REPO/src_upstream/$tree/vendor/oplus/kernel/cpu/uad/ua_ioctl"
  [ -f "$include/frame_ioctl.h" ] && [ -f "$include/ua_ioctl_common.h" ] || {
    printf 'missing upstream ABI headers: %s\n' "$include" >&2; exit 1;
  }
  for multi in 0 1; do
    flags=()
    [ "$multi" = 0 ] || flags=(-DCONFIG_OPLUS_FEATURE_MULTI_FBG=1)
    "${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsyntax-only \
      "${flags[@]}" -I"$include" "$REPO/tests/frame_boost/test_ioctl_abi.c"
    printf '%s multi_fbg=%s: LP64 layout, ioctl numbers and animation stages match\n' "$tree" "$multi"
  done
done
printf 'Source-header contract only; not proof of ROM ioctl use or 32-bit compat.\n'
OUT="$REPO/out/frame_boost_host_tests"
mkdir -p "$OUT"
python3 -I - "$REPO" "$OUT" <<'PY'
import importlib.util
from pathlib import Path
import sys
root, out = map(Path, sys.argv[1:])
spec = importlib.util.spec_from_file_location('ux', root / 'scripts/adapt_sched_assist.py')
ux = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ux)
source = (root / 'port/frame_boost/src/sailboat_coloros_fbg_ioctl.c').read_text()
start, _, end = ux.span(source, 'sailboat_coloros_fbg_dispatch')
(out / 'extracted_fbg_ioctl.h').write_text(source[start:end] + '\n')
PY
for name in policy runtime; do
  "${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$REPO/port/frame_boost/src" -I"$OUT" \
    -I"$REPO/src_upstream/oplus_sm8550/vendor/oplus/kernel/cpu/uad/ua_ioctl" \
    "$REPO/tests/frame_boost/test_$name.c" -o "$OUT/test_$name"
  "$OUT/test_$name"
done
