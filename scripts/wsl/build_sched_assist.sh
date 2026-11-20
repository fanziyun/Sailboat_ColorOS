#!/usr/bin/env bash
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KDIR="${1:?usage: build_sched_assist.sh KDIR CLANG_BIN [OUT] [KERNEL_SRC] [UPSTREAM]}"
CLANG_BIN="${2:?missing matching clang bin directory}"
OUT="${3:-$REPO/out/build/ux_min}"
KSRC="${SAILBOAT_KERNEL_SRC:-${4:-$KDIR}}"
UPSTREAM="${5:-$REPO/src_upstream/oplus_sm8550}"
[ -f "$KDIR/Makefile" ] && [ -f "$KDIR/Module.symvers" ] && [ -f "$KDIR/.config" ] || {
  printf 'missing prepared kernel directory, config or Module.symvers\n' >&2; exit 1;
}
[ -f "$KSRC/include/trace/hooks/sched.h" ] || { printf 'pass the matching kernel source directory\n' >&2; exit 1; }
[ -x "$CLANG_BIN/clang" ] || { printf 'missing clang\n' >&2; exit 1; }
case "$OUT" in "$REPO"/out/*) ;; *) printf 'OUT must be inside repo out/\n' >&2; exit 1 ;; esac
[ ! -e "$OUT/Makefile" ] || { printf 'use a fresh output directory\n' >&2; exit 1; }
mkdir -p "$OUT"
SRC="$UPSTREAM/vendor/oplus/kernel/cpu/sched"
SHA="$(python3 -I "$REPO/scripts/adapt_sched_assist.py" --verify-upstream "$UPSTREAM")"
cp "$SRC"/sched_assist/*.[ch] "$OUT"/
cp "$SRC/frame_boost/frame_boost_V1/frame_group.h" "$OUT"/frame_group.h
mode="${SAILBOAT_BUILTIN_CHECK:-0}"
adapt_args=()
case "$mode" in
  0) ;;
  1) adapt_args=(--builtin) ;;
  *) printf 'SAILBOAT_BUILTIN_CHECK must be 0 or 1\n' >&2; exit 1 ;;
esac
python3 -I "$REPO/scripts/adapt_sched_assist.py" --out-dir "$OUT" \
  --port-dir "$REPO/port/sched_assist/src" --kernel-dir "$KSRC" \
  --config "$KDIR/.config" --source-sha "$SHA" "${adapt_args[@]}"
export PATH="$CLANG_BIN:$PATH"
if [ "$mode" = 1 ]; then
  make -C "$KDIR" M="$OUT" ARCH=arm64 LLVM=1 PAHOLE=true \
    CONFIG_OPLUS_SCHED_ASSIST=y KBUILD_BUILTIN=1 KBUILD_MODULES= built-in.a
  "$CLANG_BIN/llvm-nm" "$OUT/built-in.a" > "$OUT/builtin-symbols.txt"
  cp "$KDIR/.config" "$OUT/build.config"
  sha256sum "$OUT/built-in.a" "$OUT/build.config" > "$OUT/sha256.txt"
  printf 'builtin_object_check=%s; not a boot image\n' "$OUT/built-in.a"
else
  modpost_args=()
  if [ -n "${SAILBOAT_MODPOST:-}" ]; then
    [ -x "$SAILBOAT_MODPOST" ] || { printf 'missing strict modpost executable\n' >&2; exit 1; }
    printf -v modpost_command '%q -m -E -e -i %q -o %q' \
      "$SAILBOAT_MODPOST" "$KDIR/Module.symvers" "$OUT/Module.symvers"
    modpost_args=("MODPOST=$modpost_command")
  fi
  make -C "$KDIR" M="$OUT" ARCH=arm64 LLVM=1 PAHOLE=true "${modpost_args[@]}" modules
  "$CLANG_BIN/llvm-nm" -u "$OUT/sail_sched_assist.ko" > "$OUT/imports.txt"
  python3 -I - "$OUT/sail_sched_assist.mod.c" "$KDIR/Module.symvers" "$OUT/imports.txt" <<'PY'
import pathlib
import re
import sys
text = pathlib.Path(sys.argv[1]).read_text()
versions = re.search(r'____versions\[\].*?=\s*\{(.*?)\};', text, re.S)
if not versions:
    raise SystemExit('missing symbol version table')
entries = {name: int(crc, 16) for crc, name in re.findall(r'\{\s*(0x[0-9a-fA-F]+),\s*"([^"]+)"\s*\}', versions[1])}
expected = {}
for line in pathlib.Path(sys.argv[2]).read_text().splitlines():
    fields = line.split()
    if len(fields) >= 2:
        expected[fields[1]] = int(fields[0], 16)
imports = {line.split()[-1] for line in pathlib.Path(sys.argv[3]).read_text().splitlines() if re.search(r'\bU\s+\S+$', line)}
required = imports | {'module_layout'}
missing = required - entries.keys()
bad = {name for name, crc in entries.items() if expected.get(name) != crc}
if missing or bad:
    raise SystemExit(f'invalid symbol CRC table: missing={sorted(missing)} mismatched={sorted(bad)}')
print(f'{len(entries)} symbol CRCs match the build kernel; {len(imports)} strong imports covered')
PY
  "$CLANG_BIN/llvm-readelf" -h "$OUT/sail_sched_assist.ko" > "$OUT/elf-header.txt"
  "$CLANG_BIN/llvm-readelf" -p .modinfo "$OUT/sail_sched_assist.ko" > "$OUT/modinfo.txt"
  cp "$KDIR/.config" "$OUT/build.config"
  sha256sum "$OUT/sail_sched_assist.ko" "$OUT/build.config" > "$OUT/sha256.txt"
  printf 'build_only=%s; init refuses MODULE loading; do not install\n' "$OUT/sail_sched_assist.ko"
fi
