#!/system/bin/sh
MODDIR=${0%/*}
KO="$MODDIR/lib/modules/sailboat_coloros_hans.ko"
MANIFEST="$MODDIR/prototype.manifest"
STATUS=/proc/sailboat_coloros_hans/status
NAME=sailboat_coloros_hans

field() {
  while IFS='=' read -r key value; do
    if [ "$key" = "$2" ]; then
      printf '%s\n' "$value"
      return 0
    fi
  done < "$1"
  return 1
}

fail() {
  printf 'sailboat_coloros_hans: %s\n' "$1" >&2
  return 1
}

preflight() {
  [ "$(id -u)" = 0 ] || { fail 'root required'; return 1; }
  [ -f "$MANIFEST" ] && [ -f "$KO" ] || { fail 'missing manifest or ko'; return 1; }
  [ "$(uname -m)" = aarch64 ] || { fail 'wrong architecture'; return 1; }
  [ "$(uname -r)" = "$(field "$MANIFEST" target_kernel)" ] || { fail 'target kernel mismatch'; return 1; }
  [ "$(field "$MANIFEST" target_abi_verified)" = 1 ] || {
    fail 'target ABI is not verified; loading is blocked'
    return 1
  }
  set -- $(sha256sum "$KO")
  [ "$1" = "$(field "$MANIFEST" ko_sha256)" ] || { fail 'ko checksum mismatch'; return 1; }
  return 0
}

case "${1:-status}" in
  status)
    printf 'prototype=1\nautoload=0\nnative_load_blocked=1\n'
    if [ -r "$STATUS" ]; then
      cat "$STATUS"
    else
      printf 'loaded=0\n'
    fi
    ;;
  preflight)
    preflight
    ;;
  probe)
    preflight || exit 1
    [ ! -d "/sys/module/$NAME" ] && [ ! -d /sys/module/oplus_sys_hans ] && [ ! -d /sys/module/oplus_hans ] || {
      fail 'an HANS module is already present'
      exit 1
    }
    insmod "$KO" native_enabled=0 allow_test_client=1 || { fail 'insmod failed'; exit 1; }
    [ -r "$STATUS" ] || { fail 'loaded but status is missing; do not retry'; exit 1; }
    cat "$STATUS"
    ;;
  native)
    fail 'native mode blocked: reply-filter ABI and freeze-policy detach are not verified'
    exit 1
    ;;
  unload)
    [ "$(id -u)" = 0 ] || { fail 'root required'; exit 1; }
    [ -r "$STATUS" ] || { fail 'module status unavailable'; exit 1; }
    [ "$(field "$STATUS" native_enabled)" = 0 ] && [ "$(field "$STATUS" native_pinned)" = 0 ] || {
      fail 'native session cannot be safely detached; disable the KSU module and use a controlled reboot'
      exit 1
    }
    rmmod "$NAME" || { fail 'rmmod failed; no force unload'; exit 1; }
    ;;
  *)
    fail 'usage: action.sh [status|preflight|probe|native|unload]'
    exit 2
    ;;
esac
