#!/system/bin/sh
MODDIR=${0%/*}
. "$MODDIR/tunables.conf"

IB=/proc/sys/walt/input_boost
STAMP="$MODDIR/current_profile"
CPU=/sys/devices/system/cpu
KGSL=${KGSL:-/sys/class/kgsl/kgsl-3d0}
GORIG="$MODDIR/${GPU_ORIG_FILE:-gpu_orig_pwrlevel}"

profile_freqs() {
  case "$1" in
    light) echo "$LIGHT_FREQS" ;;
    standard) echo "$STANDARD_FREQS" ;;
    aggressive) echo "$AGGRESSIVE_FREQS" ;;
    *) echo "" ;;
  esac
}

profile_ms() {
  case "$1" in
    light) echo "$LIGHT_MS" ;;
    standard) echo "$STANDARD_MS" ;;
    aggressive) echo "$AGGRESSIVE_MS" ;;
    *) echo 0 ;;
  esac
}

apply_gpu() {
  [ -d "$KGSL" ] || return 0
  [ -w "$KGSL/min_pwrlevel" ] || return 0
  if [ "$1" = "off" ]; then
    if [ -f "$GORIG" ]; then
      cat "$GORIG" > "$KGSL/min_pwrlevel" 2>/dev/null
      rm -f "$GORIG"
    fi
    return 0
  fi
  [ -f "$GORIG" ] || cat "$KGSL/min_pwrlevel" > "$GORIG" 2>/dev/null
  case "${GPU_PWRLEVEL:-x}" in
    ""|x|*[!0-9]*) return 0 ;;
  esac
  echo "$GPU_PWRLEVEL" > "$KGSL/min_pwrlevel" 2>/dev/null
}

apply() {
  f=$(profile_freqs "$1")
  ms=$(profile_ms "$1")
  if [ -z "$f" ]; then
    echo 0 > "$IB/sched_boost_on_input" 2>/dev/null
    apply_gpu off
    echo off > "$STAMP"
    return 0
  fi
  echo "$f" > "$IB/input_boost_freq" 2>/dev/null
  echo "$ms" > "$IB/input_boost_ms" 2>/dev/null
  echo "$POWERKEY_MS" > "$IB/powerkey_input_boost_ms" 2>/dev/null
  echo "$POWERKEY_ON_INPUT" > "$IB/powerkey_sched_boost_on_input" 2>/dev/null
  echo 1 > "$IB/sched_boost_on_input" 2>/dev/null
  apply_gpu "$1"
  echo "$1" > "$STAMP"
}

boost_once() {
  ms=${1:-$MANUAL_MS}
  for c in 0 4 7; do
    cat "$CPU/cpu$c/cpufreq/scaling_min_freq" > "/data/local/tmp/.sailboat_min_$c" 2>/dev/null
  done
  echo "$MANUAL_FREQ_0" > "$CPU/cpu0/cpufreq/scaling_min_freq" 2>/dev/null
  echo "$MANUAL_FREQ_4" > "$CPU/cpu4/cpufreq/scaling_min_freq" 2>/dev/null
  echo "$MANUAL_FREQ_7" > "$CPU/cpu7/cpufreq/scaling_min_freq" 2>/dev/null
  if [ "$ms" -ge 1000 ] 2>/dev/null; then
    sleep $((ms / 1000))
  else
    sleep "0.$ms"
  fi
  for c in 0 4 7; do
    v=$(cat "/data/local/tmp/.sailboat_min_$c" 2>/dev/null)
    [ -n "$v" ] && echo "$v" > "$CPU/cpu$c/cpufreq/scaling_min_freq" 2>/dev/null
  done
}

status() {
  echo "profile=$(cat "$STAMP" 2>/dev/null || echo unknown)"
  echo "input_boost_freq=$(cat "$IB/input_boost_freq" 2>/dev/null)"
  echo "input_boost_ms=$(cat "$IB/input_boost_ms" 2>/dev/null)"
  echo "sched_boost_on_input=$(cat "$IB/sched_boost_on_input" 2>/dev/null)"
  echo "powerkey_boost=$(cat "$IB/powerkey_sched_boost_on_input" 2>/dev/null)"
  echo "gpu_min_pwrlevel=$(cat "$KGSL/min_pwrlevel" 2>/dev/null || echo n/a)  want=${GPU_PWRLEVEL:-n/a}  orig=$(cat "$GORIG" 2>/dev/null || echo none)"
  echo "min_freq cpu0=$(cat "$CPU/cpu0/cpufreq/scaling_min_freq" 2>/dev/null) cpu4=$(cat "$CPU/cpu4/cpufreq/scaling_min_freq" 2>/dev/null) cpu7=$(cat "$CPU/cpu7/cpufreq/scaling_min_freq" 2>/dev/null)"
}

case "$1" in
  on) apply "${2:-$PROFILE_DEFAULT}" ;;
  off) apply off ;;
  apply) apply "$2" ;;
  guard)
    echo $$ > "$MODDIR/guard.pid"
    trap 'rm -f "$MODDIR/guard.pid"; exit 0' TERM INT
    while true; do
      cur=$(cat "$IB/input_boost_freq" 2>/dev/null | tr -s ' \t' ' ')
      want=$(profile_freqs "$(cat "$STAMP" 2>/dev/null)")
      if [ -n "$want" ] && [ "$cur" != "$want" ]; then
        apply "$(cat "$STAMP" 2>/dev/null)"
        echo "$(date +%H:%M:%S) guard: 频率值被外部改回，已重新套用" >> "$MODDIR/load.log"
      fi
      g=$(cat "$KGSL/min_pwrlevel" 2>/dev/null)
      if [ -f "$GORIG" ] && [ -n "$g" ] && [ "$g" != "${GPU_PWRLEVEL:-1}" ]; then
        apply_gpu on
        echo "$(date +%H:%M:%S) guard: GPU 档位被外部改回($g)，已重新套用" >> "$MODDIR/load.log"
      fi
      sleep "${GUARD_INTERVAL:-10}"
    done
    ;;
  gpu) apply_gpu "$2" ;;
  test) boost_once "$2" ;;
  status|"") status ;;
  *) echo "usage: boost.sh [on <light|standard|aggressive>|off|apply <profile>|guard|gpu <on|off>|test [ms]|status]" ;;
esac
