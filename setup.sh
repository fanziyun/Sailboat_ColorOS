#!/usr/bin/env bash
set -euo pipefail
PREFIX=sailboat_coloros
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STAGE="${CUSTOM_EXTERNAL_MODULE_STAGE:-}"
KROOT="${KERNEL_ROOT:-}"
DEFCONF="${DEFCONFIG:-}"
UPSTREAM_REPO=https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8550.git
UPSTREAM_SHA=0241347436d48df011e2a6f6fe2d8740c26a379c
SRC_CACHE="${SAILBOAT_UPSTREAM_DIR:-${GITHUB_WORKSPACE:-/tmp}/_sailboat_coloros_upstream}"
log() { printf '[%s] %s\n' "$PREFIX" "$*"; }

fetch_upstream() {
  if [ ! -e "$SRC_CACHE" ]; then
    git clone -q --depth 1 --filter=blob:none --sparse --no-checkout "$UPSTREAM_REPO" "$SRC_CACHE"
    git -C "$SRC_CACHE" fetch -q --depth 1 origin "$UPSTREAM_SHA"
    git -C "$SRC_CACHE" sparse-checkout set vendor/oplus/kernel/cpu/sched/sched_assist vendor/oplus/kernel/cpu/sched/frame_boost
    git -C "$SRC_CACHE" checkout -q --detach "$UPSTREAM_SHA"
  fi
  [ "$(git -C "$SRC_CACHE" rev-parse HEAD)" = "$UPSTREAM_SHA" ] || {
    log 'upstream cache has a different SHA; provide a separate audited cache'; return 1;
  }
  python3 -I "$ROOT/scripts/adapt_sched_assist.py" --verify-upstream "$SRC_CACHE" >/dev/null
  [ -f "$SRC_CACHE/vendor/oplus/kernel/cpu/sched/sched_assist/sa_fair.c" ]
}

inject_after_patch() {
  [ -n "$KROOT" ] && [ -d "$KROOT/common/drivers" ] || { log 'missing kernel tree'; return 1; }
  fetch_upstream
  local up="$SRC_CACHE/vendor/oplus/kernel/cpu/sched"
  local drv="$KROOT/common/drivers/sailboat_sched_assist"
  local staging
  staging="$(mktemp -d "$KROOT/common/drivers/.sailboat_coloros_ux.XXXXXX")"
  cp "$up"/sched_assist/*.[ch] "$staging"/
  cp "$up/frame_boost/frame_boost_V1/frame_group.h" "$staging"/frame_group.h
  local config_args=()
  [ -z "$DEFCONF" ] || config_args=(--config "$DEFCONF")
  if ! python3 -I "$ROOT/scripts/adapt_sched_assist.py" --out-dir "$staging" \
      --port-dir "$ROOT/port/sched_assist/src" --kernel-dir "$KROOT/common" \
      --source-sha "$UPSTREAM_SHA" --builtin "${config_args[@]}"; then
    rm -rf "$staging"
    return 1
  fi
  printf '%s\n' 'config OPLUS_SCHED_ASSIST' \
    '    bool "Sailboat minimal ColorOS UX"' '    depends on SMP && PROC_FS' \
    '    default n' '    help' \
    '      Explicit UX scheduling, default off; no hot disable or frequency boost.' > "$staging/Kconfig"
  if [ -e "$drv" ]; then
    [ -f "$drv/Makefile" ] && grep -q sailboat_sched_assist "$drv/Makefile" || {
      log 'refusing to replace an unrecognized driver directory'; return 1;
    }
    rm -rf "$drv"
  fi
  mv "$staging" "$drv"
  if ! grep -q 'sailboat_sched_assist/Kconfig' "$KROOT/common/drivers/Kconfig"; then
    printf '\nsource "drivers/sailboat_sched_assist/Kconfig"\n' >> "$KROOT/common/drivers/Kconfig"
  fi
  if ! grep -q 'sailboat_sched_assist/' "$KROOT/common/drivers/Makefile"; then
    printf '\nobj-$(CONFIG_OPLUS_SCHED_ASSIST) += sailboat_sched_assist/\n' >> "$KROOT/common/drivers/Makefile"
  fi
  log "injected ux_min upstream=$UPSTREAM_SHA default_off=1"
}

enable_before_build() {
  [ -n "$DEFCONF" ] && [ -f "$DEFCONF" ] || { log 'missing DEFCONFIG'; return 1; }
  [ -n "$KROOT" ] && [ -f "$KROOT/common/drivers/sailboat_sched_assist/sailboat_coloros_ux_profile.json" ] || {
    log 'missing after_patch UX profile'; return 1;
  }
  python3 -I "$ROOT/scripts/adapt_sched_assist.py" \
    --out-dir "$KROOT/common/drivers/sailboat_sched_assist" \
    --port-dir "$ROOT/port/sched_assist/src" --kernel-dir "$KROOT/common" \
    --source-sha "$UPSTREAM_SHA" --builtin --config "$DEFCONF"
  grep -q '^CONFIG_OPLUS_SCHED_ASSIST=y$' "$DEFCONF" || printf '\nCONFIG_OPLUS_SCHED_ASSIST=y\n' >> "$DEFCONF"
  log 'CONFIG_OPLUS_SCHED_ASSIST=y; runtime UX remains off'
}

case "$STAGE" in
  after_patch) inject_after_patch ;;
  before_build) enable_before_build ;;
  *) log "unsupported stage: $STAGE"; exit 1 ;;
esac
