#!/usr/bin/env bash
set -euo pipefail
PREFIX=sailboat_coloros
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STAGE="${CUSTOM_EXTERNAL_MODULE_STAGE:-}"
KROOT="${KERNEL_ROOT:-}"
DEFCONF="${DEFCONFIG:-}"
UX_MODE="${SAILBOAT_UX_BUILD_MODE:-builtin}"
case "$UX_MODE" in
  module) UX_CONFIG=m ;;
  builtin) UX_CONFIG=y ;;
  *) printf 'unsupported SAILBOAT_UX_BUILD_MODE\n' >&2; exit 1 ;;
esac
UPSTREAM_REPO=https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8550.git
UPSTREAM_SHA=0241347436d48df011e2a6f6fe2d8740c26a379c
SRC_CACHE="${SAILBOAT_UPSTREAM_DIR:-${GITHUB_WORKSPACE:-/tmp}/_sailboat_coloros_upstream}"
log() { printf '[%s] %s\n' "$PREFIX" "$*"; }

fetch_upstream() {
  if [ ! -e "$SRC_CACHE" ]; then
    git clone -q --depth 1 --filter=blob:none --sparse --no-checkout "$UPSTREAM_REPO" "$SRC_CACHE"
    git -C "$SRC_CACHE" fetch -q --depth 1 origin "$UPSTREAM_SHA"
    git -C "$SRC_CACHE" sparse-checkout set vendor/oplus/kernel/cpu/sched/sched_assist vendor/oplus/kernel/cpu/sched/frame_boost vendor/oplus/kernel/cpu/uad/ua_ioctl
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
  local adapt_args=(--source-sha "$UPSTREAM_SHA")
  [ "$UX_MODE" = builtin ] && adapt_args+=(--builtin)
  if ! python3 -I "$ROOT/scripts/adapt_sched_assist.py" --out-dir "$staging" \
      --port-dir "$ROOT/port/sched_assist/src" --kernel-dir "$KROOT/common" \
      "${adapt_args[@]}" "${config_args[@]}"; then
    rm -rf "$staging"
    return 1
  fi
  if [ "$UX_MODE" = builtin ]; then
    local combined
    combined="$(mktemp -d "$KROOT/common/drivers/.sailboat_coloros_ux.XXXXXX")"
    if ! python3 -I "$ROOT/scripts/prepare_animation_boost.py" --upstream "$SRC_CACHE" \
        --base-sa "$staging" --output "$combined" --builtin; then
      rm -rf "$staging" "$combined"
      return 1
    fi
    rm -rf "$staging"
    staging="$combined"
  fi
  local help_text='Explicit UX scheduling, default enabled; no hot disable.'
  [ "$UX_MODE" = builtin ] && help_text='Sailboat ColorOS UX and FrameBoost V1 built in; default enabled, no hot disable.'
  printf '%s\n' 'config OPLUS_SCHED_ASSIST' \
    '    tristate "Sailboat ColorOS UX"' '    depends on SMP && PROC_FS' \
    '    default n' '    help' \
    "      $help_text" > "$staging/Kconfig"
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
  log "injected ux profile mode=$UX_MODE upstream=$UPSTREAM_SHA"
}

enable_before_build() {
  [ -n "$DEFCONF" ] && [ -f "$DEFCONF" ] || { log 'missing DEFCONFIG'; return 1; }
  local drv="$KROOT/common/drivers/sailboat_sched_assist"
  [ -n "$KROOT" ] && { [ -f "$drv/sailboat_coloros_ux_profile.json" ] || \
    [ -f "$drv/sailboat_coloros_animation_profile.json" ]; } || {
    log 'missing after_patch UX profile'; return 1;
  }
  if [ "$UX_MODE" = builtin ] && [ -f "$drv/sailboat_coloros_animation_profile.json" ]; then
    log 'keeping after_patch animation profile sources'
  else
    local rebuild_args=(--out-dir "$drv"
      --port-dir "$ROOT/port/sched_assist/src" --kernel-dir "$KROOT/common"
      --source-sha "$UPSTREAM_SHA" --config "$DEFCONF")
    [ "$UX_MODE" = builtin ] && rebuild_args+=(--builtin)
    python3 -I "$ROOT/scripts/adapt_sched_assist.py" "${rebuild_args[@]}"
  fi
  python3 -I - "$DEFCONF" "$UX_CONFIG" <<'PY'
import pathlib
import sys
path = pathlib.Path(sys.argv[1])
lines = path.read_text().splitlines()
lines = [line for line in lines if not line.startswith('CONFIG_OPLUS_SCHED_ASSIST=')
         and line != '# CONFIG_OPLUS_SCHED_ASSIST is not set']
path.write_text('\n'.join(lines) + '\nCONFIG_OPLUS_SCHED_ASSIST=' + sys.argv[2] + '\n')
PY
  log "CONFIG_OPLUS_SCHED_ASSIST=$UX_CONFIG; runtime default from profile"
}

case "$STAGE" in
  after_patch) inject_after_patch ;;
  before_build) enable_before_build ;;
  *) log "unsupported stage: $STAGE"; exit 1 ;;
esac
