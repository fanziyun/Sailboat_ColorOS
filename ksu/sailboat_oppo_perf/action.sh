#!/system/bin/sh
MODDIR=${0%/*}
cmd=${1:-status}
shift 2>/dev/null
FB="$MODDIR/bin/Sail_FrameBoost"

list_status() {
  echo "--- modules"
  lsmod | grep -E "sched_assist|frame_boost|ua_cpu|task_" || echo "none loaded"
  echo "--- nodes"
  ls /proc/oplus_scheduler/sched_assist 2>/dev/null || echo "no /proc/oplus_scheduler"
  echo "--- frameboost"
  if pidof Sail_FrameBoost >/dev/null 2>&1; then echo "running"; else echo "stopped"; fi
  echo "--- boost"
  sh "$MODDIR/boost.sh" status
}

case "$cmd" in
  load)
    sh "$MODDIR/service.sh"
    list_status
    ;;
  status|"")
    list_status
    ;;
  fb-start)
    [ -x "$FB" ] || { echo "missing $FB"; exit 1; }
    "$FB" >/dev/null 2>&1 &
    echo started
    ;;
  fb-stop)
    pkill -x Sail_FrameBoost 2>/dev/null
    echo 7 > /sys/class/kgsl/kgsl-3d0/min_pwrlevel 2>/dev/null
    echo stopped
    ;;
  boost-on)
    sh "$MODDIR/boost.sh" on "$1"
    sh "$MODDIR/boost.sh" status
    ;;
  boost-off)
    sh "$MODDIR/boost.sh" off
    sh "$MODDIR/boost.sh" status
    ;;
  boost-test)
    sh "$MODDIR/boost.sh" test "$1"
    echo "tested"
    ;;
  *)
    echo "usage: action.sh [status|load|fb-start|fb-stop|boost-on <light|standard|aggressive>|boost-off|boost-test [ms]]"
    ;;
esac
