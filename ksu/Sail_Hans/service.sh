#!/system/bin/sh
MODDIR=${0%/*}
LOG=/data/local/tmp/sail_hans_boot.log
echo "$(date +%H:%M:%S) svc before=$(getprop persist.vendor.enable.hans)" >> "$LOG"
if command -v resetprop >/dev/null 2>&1; then
  resetprop -n persist.vendor.enable.hans true
  rc=$?
  echo "$(date +%H:%M:%S) svc after=$(getprop persist.vendor.enable.hans) resetprop_rc=$rc" >> "$LOG"
else
  echo "$(date +%H:%M:%S) svc resetprop missing" >> "$LOG"
fi
log -t sailboat_coloros_hans 'prototype installed; automatic loading and native policy are disabled'
exit 0
