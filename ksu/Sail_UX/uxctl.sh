#!/system/bin/sh
MODDIR=${0%/*}
PROC=/proc/oplus_scheduler/sched_assist
PARAM=/sys/module/sailboat_sched_assist/parameters
STATE="$MODDIR/state"
KO="$MODDIR/lib/modules/sailboat_sched_assist.ko"

say() {
  printf '%s\n' "$*"
  if [ -d "$STATE" ]; then
    printf '%s\n' "$*" >> "$STATE/last.log"
  fi
}
fail() { say "Sail_UX: $*"; return 1; }
manifest() {
  format= target_release= ko_sha256= notes_sha256= source_id= target_build_verified=
  seen='|'
  [ -f "$MODDIR/ux.manifest" ] || { fail 'manifest missing'; return 1; }
  while IFS='=' read -r key value; do
    case "$seen" in *"|$key|"*) fail 'duplicate manifest key'; return 1 ;; esac
    seen="$seen$key|"
    case "$key" in
      format) format=$value ;;
      target_release) target_release=$value ;;
      ko_sha256) ko_sha256=$value ;;
      notes_sha256) notes_sha256=$value ;;
      source_id) source_id=$value ;;
      target_build_verified) target_build_verified=$value ;;
      *) fail 'unknown manifest key'; return 1 ;;
    esac
  done < "$MODDIR/ux.manifest"
  [ "$format" = 1 ] && [ "$target_build_verified" = 1 ] || {
    fail 'module is not from a verified target build'; return 1;
  }
  case "$target_release" in ''|*[!A-Za-z0-9._+-]*) fail 'bad target release'; return 1 ;; esac
  for value in "$ko_sha256" "$notes_sha256" "$source_id"; do
    case "$value" in ''|*[!0-9a-f]*) fail 'bad fingerprint'; return 1 ;; esac
    [ "${#value}" = 64 ] || { fail 'bad fingerprint length'; return 1; }
  done
}
identity() {
  manifest || return 1
  [ "$(uname -m)" = aarch64 ] && [ "$(uname -r)" = "$target_release" ] || {
    fail 'kernel mismatch; install the matching no-SA base first'; return 1;
  }
  actual=$(sha256sum /sys/kernel/notes 2>/dev/null) || { fail 'cannot fingerprint kernel notes'; return 1; }
  [ "${actual%% *}" = "$notes_sha256" ] || { fail 'kernel notes mismatch'; return 1; }
  actual=$(sha256sum "$KO" 2>/dev/null) || { fail 'module file missing'; return 1; }
  [ "${actual%% *}" = "$ko_sha256" ] || { fail 'module hash mismatch'; return 1; }
}
ready() {
  [ -d "$PARAM" ] && [ -f "$PROC/ux_status" ] || { fail 'module not ready; inspect init state/log'; return 1; }
  [ "$(cat "$PARAM/init_state" 2>/dev/null)" = 1 ] &&
  [ "$(cat "$PARAM/init_errno" 2>/dev/null)" = 0 ] || {
    fail 'resident module FAILED; reboot before retrying'; return 1;
  }
  [ "$(cat "$PARAM/source_id" 2>/dev/null)" = "$source_id" ] || {
    fail 'running module differs from installed package; reboot'; return 1;
  }
  grep -q '^profile=sailboat_coloros_ux_min ready=1 ' "$PROC/ux_status" || {
    fail 'UX readiness check failed'; return 1;
  }
}
load() {
  [ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ] || { fail 'package disabled/removed for next boot'; return 1; }
  identity || return 1
  if [ -d "$PROC" ] || [ -d /sys/module/sailboat_sched_assist ] || [ -d /sys/module/sail_sched_assist ]; then
    fail 'sched_assist already exists; never overlay an existing implementation'; return 1
  fi
  mkdir -p "$STATE" || return 1
  boot=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null) || return 1
  case "$boot" in ''|*[!0-9a-f-]*) fail 'invalid boot ID'; return 1 ;; esac
  mkdir "$STATE/attempt.$boot" 2>/dev/null || { fail 'already attempted this boot; reboot'; return 1; }
  : > "$STATE/last.log"
  say "Sail_UX: single load attempt, kernel=$target_release"
  insmod "$KO" >> "$STATE/last.log" 2>&1
  rc=$?
  [ "$rc" = 0 ] || { fail "insmod failed rc=$rc; no forced retry"; return 1; }
  ready || return 1
  say 'Sail_UX: module READY; policy defaults OFF; cannot hot-unload'
  if [ -f "$MODDIR/enable_ux_on_boot" ]; then
    enable || return 1
  fi
}
enable() {
  identity && ready || return 1
  if ! printf '1\n' > "$PROC/sched_assist_enabled"; then
    fail 'enable failed'; return 1
  fi
  grep -q '^enabled=1$' "$PROC/sched_assist_enabled" || { fail 'enable readback mismatch'; return 1; }
  say 'Sail_UX: UX ACTIVE; reboot is required to turn it off'
}
status() {
  printf 'Sail_UX: update/disable/remove requires reboot; Action only displays status\n'
  uname -r
  if [ -d "$PARAM" ]; then
    for key in init_state init_errno init_stage source_id; do
      printf '%s=' "$key"
      cat "$PARAM/$key" 2>/dev/null || true
    done
  fi
  if [ -f "$PROC/ux_status" ]; then
    cat "$PROC/ux_status"
  elif [ -d "$PROC" ]; then
    printf 'Existing sched_assist is not this UX test module; it will not be replaced.\n'
  else
    printf 'No sched_assist loaded.\n'
  fi
  [ ! -f "$STATE/last.log" ] || cat "$STATE/last.log"
}
[ "$(id -u)" = 0 ] || { printf 'Root required\n' >&2; exit 1; }
case "${1:-status}" in
  load) load ;;
  enable) enable ;;
  status) status ;;
  arm)
    identity && ready || exit 1
    : > "$MODDIR/enable_ux_on_boot" || exit 1
    say 'Sail_UX: explicit UX activation armed for future matching boots'
    ;;
  disarm)
    rm -f "$MODDIR/enable_ux_on_boot"
    say 'Sail_UX: future automatic activation cleared; current ACTIVE policy still needs reboot to stop'
    ;;
  *) printf 'usage: uxctl.sh status|load|enable|arm|disarm\n' >&2; exit 1 ;;
esac
