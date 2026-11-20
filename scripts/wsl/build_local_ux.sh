#!/usr/bin/env bash
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MATERIALS="${1:?usage: build_local_ux.sh MATERIALS_TAR DIST_DIR [WORKSPACE]}"
DIST="${2:?usage: build_local_ux.sh MATERIALS_TAR DIST_DIR [WORKSPACE]}"
WS="${3:-}"
[ -f "$MATERIALS" ] || { printf 'missing kernel materials archive\n' >&2; exit 1; }
[ -f "$DIST/Image" ] || { printf 'missing dist/Image of the matching kernel\n' >&2; exit 1; }
CLANG_BIN="${SAILBOAT_CLANG_BIN:?set SAILBOAT_CLANG_BIN to the matching clang bin directory}"
[ -x "$CLANG_BIN/clang" ] || { printf 'missing clang\n' >&2; exit 1; }
[ -d "$REPO/src_upstream/oplus_sm8550" ] || { printf 'missing fetched upstream tree\n' >&2; exit 1; }
if [ -z "$WS" ]; then
  WS="$(mktemp -d "${TMPDIR:-/tmp}/sailboat_ux_local_XXXXXX")"
fi
case "$WS" in /*) ;; *) printf 'WORKSPACE must be an absolute path\n' >&2; exit 1 ;; esac
[ ! -e "$WS/target" ] || { printf 'workspace already holds a prepared target\n' >&2; exit 1; }
python3 -I "$REPO/scripts/prepare_ci_target.py" "$MATERIALS" "$WS/target"
KDIR="$WS/target/out/android13-5.15/common"
KSRC="$WS/target/common"
[ -f "$KDIR/Makefile" ] && [ -f "$KDIR/Module.symvers" ] && [ -f "$KDIR/vmlinux" ] || {
  printf 'prepared kernel directory is incomplete\n' >&2; exit 1; }
MODPOST="$KDIR/scripts/mod/modpost"
if [ ! -x "$MODPOST" ]; then
  MODPOST="${SAILBOAT_MODPOST:-}"
  [ -n "$MODPOST" ] && [ -x "$MODPOST" ] || { printf 'missing a strict modpost\n' >&2; exit 1; }
fi
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT="${SAILBOAT_UX_OUT:-$REPO/out/build/ux_modbase_$STAMP}"
case "$OUT" in "$REPO"/out/*) ;; *) printf 'OUT must be inside repo out/\n' >&2; exit 1 ;; esac
[ ! -e "$OUT" ] || { printf 'output directory already exists\n' >&2; exit 1; }
SAILBOAT_MODPOST="$MODPOST" "$REPO/scripts/wsl/build_sched_assist.sh" \
  "$KDIR" "$CLANG_BIN" "$OUT" "$KSRC" "$REPO/src_upstream/oplus_sm8550"
python3 -I "$REPO/scripts/collect_ux_build.py" \
  --kernel-out "$KDIR" --source-dir "$KSRC" --dist-dir "$DIST" \
  --clang-bin "$CLANG_BIN" --output "$OUT/ksu" --no-sdk \
  --module "$OUT/sailboat_sched_assist.ko"
printf 'local_ko=%s\n' "$OUT/sailboat_sched_assist.ko"
printf 'local_ksu_zip=%s\n' "$(ls "$OUT"/ksu/Sail_UX-*.zip)"
