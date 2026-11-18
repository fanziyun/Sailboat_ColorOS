#!/usr/bin/env bash
set -euo pipefail

PREFIX="sailboat_coloros"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STAGE="${CUSTOM_EXTERNAL_MODULE_STAGE:-}"
KROOT="${KERNEL_ROOT:-}"
DEFCONF="${DEFCONFIG:-}"
ANYKERNEL_DIR="${ANYKERNEL3:-${GITHUB_WORKSPACE:+$GITHUB_WORKSPACE/AnyKernel3}}"
MODULE_SRC="$ROOT/ksu/sailboat_oppo_perf"
MODULE_ID="sailboat_oppo_perf"
BUNDLE_DIR="abk-ksu-modules"
MARK_BEGIN="### >>> Sailboat ColorOS KernelSU module installer"
MARK_END="### <<< Sailboat ColorOS KernelSU module installer"

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
    log "overlay applied: port/sched_assist/src -> $dst"
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

bundle_ksu_module() {
  local target ak zip
  [ -n "$ANYKERNEL_DIR" ] && [ -d "$ANYKERNEL_DIR" ] || { log "warn: AnyKernel3 dir not found"; return 0; }
  [ -d "$MODULE_SRC" ] || { log "warn: $MODULE_SRC missing"; return 0; }
  [ -f "$MODULE_SRC/module.prop" ] || { log "warn: module.prop missing"; return 0; }
  compgen -G "$MODULE_SRC/lib/modules/*.ko" >/dev/null || { log "warn: no prebuilt .ko under $MODULE_SRC/lib/modules"; return 0; }

  target="$ANYKERNEL_DIR/$BUNDLE_DIR"
  mkdir -p "$target"
  zip="$target/$MODULE_ID.zip"
  rm -f "$zip"
  ( cd "$MODULE_SRC" && zip -qrX "$zip" . )
  log "bundled KernelSU module: $BUNDLE_DIR/$MODULE_ID.zip ($(stat -c %s "$zip") bytes)"

  ak="$ANYKERNEL_DIR/anykernel.sh"
  [ -f "$ak" ] || { log "warn: anykernel.sh missing"; return 0; }
  if ! grep -qF "$MARK_BEGIN" "$ak"; then
    cat >> "$ak" <<EOF

$MARK_BEGIN >>>
sailboat_coloros_install_ksu_module() {
  SAILBOAT_HOME="\${AKHOME:-\$PWD}"
  SAILBOAT_SRC="\$SAILBOAT_HOME/$BUNDLE_DIR"
  [ -d "\$SAILBOAT_SRC" ] || return 0
  [ -z "\$(ls "\$SAILBOAT_SRC"/*.zip 2>/dev/null)" ] && return 0
  SAILBOAT_UNZIP="unzip"
  [ -x "\$SAILBOAT_HOME/tools/busybox" ] && SAILBOAT_UNZIP="\$SAILBOAT_HOME/tools/busybox unzip"
  for SAILBOAT_ZIP in "\$SAILBOAT_SRC"/*.zip; do
    SAILBOAT_ID="\$(basename "\$SAILBOAT_ZIP" .zip)"
    SAILBOAT_VIA=""
    for SAILBOAT_KSUD in /data/adb/ksud /data/adb/ksu/bin/ksud /data/adb/ap/bin/apd; do
      [ -x "\$SAILBOAT_KSUD" ] || continue
      if "\$SAILBOAT_KSUD" module install "\$SAILBOAT_ZIP" >/dev/null 2>&1; then
        SAILBOAT_VIA="\$(basename "\$SAILBOAT_KSUD")"
        break
      fi
    done
    if [ -z "\$SAILBOAT_VIA" ] && command -v magisk >/dev/null 2>&1; then
      magisk --install-module "\$SAILBOAT_ZIP" >/dev/null 2>&1 && SAILBOAT_VIA="magisk"
    fi
    if [ -z "\$SAILBOAT_VIA" ] && [ -d /data/adb ]; then
      SAILBOAT_TARGET="/data/adb/modules/\$SAILBOAT_ID"
      rm -rf "\$SAILBOAT_TARGET"
      mkdir -p "\$SAILBOAT_TARGET" 2>/dev/null
      if \$SAILBOAT_UNZIP -o "\$SAILBOAT_ZIP" -d "\$SAILBOAT_TARGET" >/dev/null 2>&1; then
        chmod 0755 "\$SAILBOAT_TARGET"/*.sh 2>/dev/null
        SAILBOAT_VIA="unpacked to /data/adb/modules"
      fi
    fi
    if [ -n "\$SAILBOAT_VIA" ]; then
      ui_print "  -> Sailboat ColorOS: module \$SAILBOAT_ID installed via \$SAILBOAT_VIA"
    else
      ui_print "  -> Sailboat ColorOS: WARNING could not install \$SAILBOAT_ID; flash that module zip manually"
    fi
  done
}
sailboat_coloros_install_ksu_module || ui_print "  -> Sailboat ColorOS: install step skipped"
$MARK_END
EOF
    log "anykernel.sh installer injected"
  else
    log "anykernel.sh installer already present"
  fi
}

case "$STAGE" in
  after_patch)
    log "stage=after_patch kernel_root=${KROOT:-<unset>}"
    if vendor_hooks_present; then
      inject_after_patch
    else
      log "skip kernel overlay: OPLUS vendor hooks absent"
    fi
    ;;
  before_build)
    log "stage=before_build kernel_root=${KROOT:-<unset>}"
    if vendor_hooks_present; then
      enable_before_build
    else
      log "skip kernel overlay: OPLUS vendor hooks absent"
    fi
    bundle_ksu_module
    ;;
  *)
    log "unknown stage: ${STAGE:-<unset>}"
    exit 1
    ;;
esac

log "done"
