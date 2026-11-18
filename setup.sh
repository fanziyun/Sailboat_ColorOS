#!/usr/bin/env bash
set -euo pipefail

PREFIX="sailboat_coloros"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STAGE="${CUSTOM_EXTERNAL_MODULE_STAGE:-}"
KROOT="${KERNEL_ROOT:-}"
DEFCONF="${DEFCONFIG:-}"

log() { echo "[$PREFIX] $*"; }

vendor_hooks_present() {
  [ -n "$KROOT" ] || return 1
  [ -d "$KROOT/common/vendor/oplus/kernel/cpu/sched" ] || return 1
  grep -rqs "REGISTER_TRACE_VH" "$KROOT/common/include/trace/hooks" 2>/dev/null || return 1
}

inject_after_patch() {
  local dst="$KROOT/common/vendor/oplus/kernel/cpu/sched/sched_assist"
  local src="$ROOT/port/sched_assist/src"

  if [ -d "$dst" ]; then
    cp -f "$src"/*.c "$src"/*.h "$dst"/
    log "overlay applied: $(basename "$src") -> sched_assist"
  else
    log "warn: $dst missing; sched_assist not present in this kernel tree"
  fi
}

enable_before_build() {
  [ -n "$DEFCONF" ] && [ -f "$DEFCONF" ] || return 0
  local sym
  for sym in CONFIG_OPLUS_SCHED_ASSIST CONFIG_OPLUS_FEATURE_FRAME_BOOST; do
    grep -q "^${sym}=y$" "$DEFCONF" || echo "${sym}=y" >> "$DEFCONF"
  done
  log "defconfig updated: $DEFCONF"
}

run_stage() {
  case "$1" in
    after_patch)
      if vendor_hooks_present; then
        inject_after_patch
      else
        log "skip after_patch: OPLUS vendor hooks absent under ${KROOT:-<unset KERNEL_ROOT>}"
      fi
      ;;
    before_build)
      if vendor_hooks_present; then
        enable_before_build
      else
        log "skip before_build: OPLUS vendor hooks absent under ${KROOT:-<unset KERNEL_ROOT>}"
      fi
      ;;
    *)
      log "unknown stage: $1"
      exit 1
      ;;
  esac
}

log "stage=${STAGE:-<unset>} kernel_root=${KROOT:-<unset>} config=${CONFIG:-<unset>}"
run_stage "$STAGE"
log "done"
