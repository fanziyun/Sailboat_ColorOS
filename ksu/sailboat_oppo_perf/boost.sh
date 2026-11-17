#!/system/bin/sh
MODDIR=${0%/*}
. "$MODDIR/tunables.conf"

IB=/proc/sys/walt/input_boost
STAMP="$MODDIR/current_profile"
CPU=/sys/devices/system/cpu

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

apply() {
  f=$(profile_freqs "$1")
  ms=$(profile_ms "$1")
  if [ -z "$f" ]; then
    echo 0 > "$IB/sched_boost_on_input" 2>/dev/null
    echo off > "$STAMP"
    return 0
  fi
  echo "$f" > "$IB/input_boost_freq" 2>/dev/null
  echo "$ms" > "$IB/input_boost_ms" 2>/dev/null
  echo "$POWERKEY_MS" > "$IB/powerkey_input_boost_ms" 2>/dev/null
  echo "$POWERKEY_ON_INPUT" > "$IB/powerkey_sched_boost_on_input" 2>/dev/null
  echo 1 > "$IB/sched_boost_on_input" 2>/dev/null
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
  echo "min_freq cpu0=$(cat "$CPU/cpu0/cpufreq/scaling_min_freq" 2>/dev/null) cpu4=$(cat "$CPU/cpu4/cpufreq/scaling_min_freq" 2>/dev/null) cpu7=$(cat "$CPU/cpu7/cpufreq/scaling_min_freq" 2>/dev/null)"
}

case "$1" in
  on) apply "${2:-$PROFILE_DEFAULT}" ;;
  off) apply off ;;
  apply) apply "$2" ;;
  test) boost_once "$2" ;;
  status|"") status ;;
  *) echo "usage: boost.sh [on <light|standard|aggressive>|off|apply <profile>|test [ms]|status]" ;;
esac
