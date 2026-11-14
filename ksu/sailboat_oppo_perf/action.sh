#!/system/bin/sh
MODDIR=${0%/*}
cmd=${1:-status}

list_status() {
  lsmod | grep -E "sched_assist|frame_boost|ua_cpu|task_" || echo "none loaded"
  echo "--- nodes"
  ls /proc/oplus_scheduler 2>/dev/null || echo "no /proc/oplus_scheduler"
  ls /proc/jank_info 2>/dev/null || echo "no /proc/jank_info"
}

case "$cmd" in
  load)
    sh "$MODDIR/service.sh"
    list_status
    ;;
  status|"")
    list_status
    ;;
  *)
    echo "usage: action.sh [status|load]"
    ;;
esac
