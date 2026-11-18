#!/system/bin/sh
MODDIR=${0%/*}
PIDF="$MODDIR/unirender_rt.pid"
ETC="$MODDIR/enable_unirender_rt"
LOG="$MODDIR/unirender_rt.log"
PRIO=${UNIRENDER_RT_PRIO:-2}
INTERVAL=${UNIRENDER_RT_INTERVAL:-5}

sf_pid() {
  pidof surfaceflinger
}

tid_of() {
  _sf=$1
  for _d in /proc/$_sf/task/*; do
    [ "$(cat "$_d/comm" 2>/dev/null)" = UniRenderEngine ] && { echo "${_d##*/}"; return 0; }
  done
  return 1
}

set_fifo() {
  _sf=$(sf_pid)
  [ -n "$_sf" ] || return 1
  _tid=$(tid_of "$_sf") || return 1
  chrt -f -p "$_tid" "$PRIO" 2>/dev/null || return 1
  echo "$_tid"
}

set_other() {
  _sf=$(sf_pid)
  [ -n "$_sf" ] || return 0
  for _d in /proc/$_sf/task/*; do
    [ "$(cat "$_d/comm" 2>/dev/null)" = UniRenderEngine ] || continue
    chrt -o -p "${_d##*/}" 0 2>/dev/null
  done
}

status() {
  _sf=$(sf_pid)
  echo "surfaceflinger=${_sf:-none}"
  if [ -n "$_sf" ]; then
    _tid=$(tid_of "$_sf")
    if [ -n "$_tid" ]; then
      echo "tid=$_tid"
      chrt -p "$_tid" 2>&1 | sed 's/^/  /'
      grep -E '^policy|^prio' "/proc/$_sf/task/$_tid/sched" 2>/dev/null | sed 's/^/  /'
    else
      echo "tid=none"
    fi
  fi
  if [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; then
    echo "watcher=running pid=$(cat "$PIDF")"
  else
    echo "watcher=stopped"
  fi
}

watch() {
  echo $$ > "$PIDF"
  trap 'set_other; rm -f "$PIDF"; exit 0' TERM INT HUP
  last=
  while [ -f "$ETC" ]; do
    t=$(set_fifo)
    if [ -n "$t" ] && [ "$t" != "$last" ]; then
      echo "$(date +%H:%M:%S) fifo tid=$t prio=$PRIO" >> "$LOG"
      last=$t
    fi
    sleep "$INTERVAL"
  done
  set_other
  rm -f "$PIDF"
}

start() {
  touch "$ETC"
  if [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; then
    return 0
  fi
  rm -f "$PIDF"
  nohup sh "$0" watch >/dev/null 2>&1 &
}

stop() {
  rm -f "$ETC"
  if [ -f "$PIDF" ]; then
    kill "$(cat "$PIDF")" 2>/dev/null
    sleep 1
  fi
  pkill -f "unirender_rt.sh watch" 2>/dev/null
  rm -f "$PIDF"
  set_other
}

case "$1" in
  start) start ;;
  stop) stop ;;
  apply) set_fifo ;;
  watch) watch ;;
  status|"") status ;;
  *) echo "usage: unirender_rt.sh [start|stop|apply|status]" ;;
esac
