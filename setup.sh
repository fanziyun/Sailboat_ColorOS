#!/usr/bin/env bash
set -euo pipefail

PREFIX="sailboat_coloros"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STAGE="${CUSTOM_EXTERNAL_MODULE_STAGE:-}"
KROOT="${KERNEL_ROOT:-}"
DEFCONF="${DEFCONFIG:-}"
UPSTREAM_REPO="https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8550.git"
UPSTREAM_REF="oneplus/sm8550_v_15.0.0_oneplus11"
SRC_CACHE="${SAILBOAT_UPSTREAM_DIR:-${GITHUB_WORKSPACE:-/tmp}/_sailboat_coloros_upstream}"
DRV="${KROOT:+$KROOT/common/drivers/sailboat_sched_assist}"

log() { echo "[$PREFIX] $*"; }

fetch_upstream() {
  if [ -d "$SRC_CACHE/vendor/oplus/kernel/cpu/sched/sched_assist" ]; then
    log "upstream cache hit: $SRC_CACHE"
    return 0
  fi
  rm -rf "$SRC_CACHE"
  git clone -q --depth 1 --filter=blob:none --sparse -b "$UPSTREAM_REF" "$UPSTREAM_REPO" "$SRC_CACHE"
  git -C "$SRC_CACHE" sparse-checkout set vendor/oplus/kernel/cpu/sched/sched_assist vendor/oplus/kernel/cpu/sched/frame_boost
  log "upstream fetched: $UPSTREAM_REF"
}

adapt_sources() {
  python3 - "$DRV" <<'PY'
import pathlib
import sys

drv = pathlib.Path(sys.argv[1])


def sub(path, old, new):
    p = drv / path
    text = p.read_text(encoding="utf-8")
    if old not in text:
        raise SystemExit(f"{path}: anchor not found: {old!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


sub("sa_fair.c", "#include <../kernel/oplus_cpu/sched/frame_boost/frame_group.h>", '#include "frame_group.h"')
sub("sa_common.c", "#include <../kernel/oplus_cpu/sched/frame_boost/frame_group.h>", '#include "frame_group.h"')
sub("sa_common.h", "#define OTS_IDX\t\t\t0", "#define OTS_IDX\t\t\t2")
sub("sa_sysfs.c", '#include "sa_sysfs.h"', '#include "sa_sysfs.h"\n#include "sailboat_coloros_nodes.h"')
sub("sa_sysfs.c", "\tgoto err_creat_d_sched_assist;\n\t}\n",
    "\tgoto err_creat_d_sched_assist;\n\t}\n\n\tsailboat_coloros_nodes_proc_init(d_sched_assist);\n")
PY
}

inject_after_patch() {
  local up="$SRC_CACHE/vendor/oplus/kernel/cpu/sched"
  local fg

  [ -n "$KROOT" ] || { log "warn: KERNEL_ROOT unset"; return 0; }
  [ -d "$KROOT/common/drivers" ] || { log "warn: $KROOT/common/drivers missing"; return 0; }

  fetch_upstream
  rm -rf "$DRV"
  mkdir -p "$DRV"
  cp "$up"/sched_assist/*.c "$up"/sched_assist/*.h "$DRV"/
  fg="$up/frame_boost/frame_boost_V1/frame_group.h"
  [ -f "$fg" ] || fg="$(find "$up/frame_boost" -name frame_group.h -print -quit)"
  [ -n "$fg" ] || { log "error: frame_group.h not found"; exit 1; }
  cp "$fg" "$DRV"/frame_group.h
  cp "$ROOT"/port/sched_assist/src/*.c "$ROOT"/port/sched_assist/src/*.h "$DRV"/
  adapt_sources

  cat > "$DRV/Makefile" <<'EOF'
obj-$(CONFIG_OPLUS_SCHED_ASSIST) += sailboat_sched_assist.o

sailboat_sched_assist-y += sched_assist.o
sailboat_sched_assist-y += sa_common.o
sailboat_sched_assist-y += sa_oemdata.o
sailboat_sched_assist-y += sa_sysfs.o
sailboat_sched_assist-y += sa_priority.o
sailboat_sched_assist-y += sa_fair.o
sailboat_sched_assist-y += sa_exec.o
sailboat_sched_assist-y += sa_jankinfo.o
sailboat_sched_assist-y += sailboat_coloros_nodes.o

ccflags-y += -I$(srctree)/ -I$(srctree)/kernel/sched -I$(src)
EOF

  cat > "$DRV/Kconfig" <<'EOF'
config OPLUS_SCHED_ASSIST
	bool "Sailboat ColorOS sched_assist"
	default n
	help
	  ColorOS sched_assist backport (ux priority, jank info, frame boost hooks).
EOF

  if ! grep -q "sailboat_sched_assist/Kconfig" "$KROOT/common/drivers/Kconfig"; then
    printf '\nsource "drivers/sailboat_sched_assist/Kconfig"\n' >> "$KROOT/common/drivers/Kconfig"
  fi
  if ! grep -q "sailboat_sched_assist/" "$KROOT/common/drivers/Makefile"; then
    printf '\nobj-$(CONFIG_OPLUS_SCHED_ASSIST) += sailboat_sched_assist/\n' >> "$KROOT/common/drivers/Makefile"
  fi
  log "injected: $(find "$DRV" -maxdepth 1 -name '*.c' | wc -l) c files -> $DRV"
}

enable_before_build() {
  [ -n "$DEFCONF" ] && [ -f "$DEFCONF" ] || { log "warn: DEFCONFIG unset"; return 0; }
  grep -q "^CONFIG_OPLUS_SCHED_ASSIST=y$" "$DEFCONF" || echo "CONFIG_OPLUS_SCHED_ASSIST=y" >> "$DEFCONF"
  log "defconfig updated: CONFIG_OPLUS_SCHED_ASSIST=y"
}

case "$STAGE" in
  after_patch)
    log "stage=after_patch kernel_root=${KROOT:-<unset>}"
    inject_after_patch
    ;;
  before_build)
    log "stage=before_build kernel_root=${KROOT:-<unset>}"
    enable_before_build
    ;;
  *)
    log "unknown stage: ${STAGE:-<unset>}"
    exit 1
    ;;
esac

log "done"
