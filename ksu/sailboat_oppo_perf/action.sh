#!/system/bin/sh
MODDIR=${0%/*}
cmd=${1:-status}
FB="$MODDIR/bin/Sail_FrameBoost"

list_status() {
  lsmod | grep -E "sched_assist|frame_boost|ua_cpu|task_" || echo "none loaded"
  echo "--- nodes"
  ls /proc/oplus_scheduler 2>/dev/null || echo "no /proc/oplus_scheduler"
  ls /proc/jank_info 2>/dev/null || echo "no /proc/jank_info"
  echo "--- frameboost"
  if pidof Sail_FrameBoost >/dev/null 2>&1; then echo "running"; else echo "stopped"; fi
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
  *)
    echo "usage: action.sh [status|load|fb-start|fb-stop]"
    ;;
esac
