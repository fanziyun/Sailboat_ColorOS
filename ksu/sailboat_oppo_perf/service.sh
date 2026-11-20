#!/system/bin/sh
MODDIR=${0%/*}
LOG="$MODDIR/load.log"
. "$MODDIR/tunables.conf"

if [ "$(wc -l < "$LOG" 2>/dev/null || echo 0)" -gt 40 ]; then
  mv -f "$LOG" "$LOG.1" 2>/dev/null
fi
: > "$LOG" 2>/dev/null

insmod_one() {
  local ko="$1"
  local name
  name=$(basename "$ko")
  case "$name" in
    *sched_assist*.ko)
      if [ -d /proc/oplus_scheduler/sched_assist ]; then
        echo "skip $name: sched_assist already present" >>"$LOG"
        return 0
      fi
      ;;
  esac
  if insmod "$ko" 2>>"$LOG"; then
    echo "ok $name" >>"$LOG"
  else
    echo "fail $name" >>"$LOG"
  fi
}

insmod_dir() {
  local dir="$1"
  local ko
  for ko in "$dir"/*.ko; do
    [ -f "$ko" ] || continue
    insmod_one "$ko"
  done
}

mkdir -p "$MODDIR/lib/modules"
insmod_dir "$MODDIR/lib/modules"

if [ -f "$MODDIR/enable_frameboost" ] && [ -x "$MODDIR/bin/Sail_FrameBoost" ]; then
  "$MODDIR/bin/Sail_FrameBoost" >/dev/null 2>&1 &
fi

(
  i=0
  while [ "$(getprop sys.boot_completed)" != "1" ]; do
    sleep 1
    i=$((i + 1))
    [ "$i" -ge 120 ] && break
  done
  sh "$MODDIR/boost.sh" on "${PROFILE_DEFAULT:-standard}" >>"$LOG" 2>&1
  [ "${GUARD_INTERVAL:-0}" -gt 0 ] 2>/dev/null && sh "$MODDIR/boost.sh" guard >>"$LOG" 2>&1 &
  [ -f "$MODDIR/enable_unirender_rt" ] && sh "$MODDIR/unirender_rt.sh" start >>"$LOG" 2>&1
) &
