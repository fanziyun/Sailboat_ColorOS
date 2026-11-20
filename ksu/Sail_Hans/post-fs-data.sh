#!/system/bin/sh
MODDIR=${0%/*}
chmod 0755 "$MODDIR/action.sh" "$MODDIR/service.sh" "$MODDIR/post-fs-data.sh"
chmod 0644 "$MODDIR/module.prop" "$MODDIR/prototype.manifest" "$MODDIR/lib/modules/sailboat_coloros_hans.ko"
LOG=/data/local/tmp/sail_hans_boot.log
echo "$(date +%H:%M:%S) pfs before=$(getprop persist.vendor.enable.hans)" >> "$LOG"
if command -v resetprop >/dev/null 2>&1; then
  resetprop -n persist.vendor.enable.hans true
  rc=$?
  echo "$(date +%H:%M:%S) pfs after=$(getprop persist.vendor.enable.hans) resetprop_rc=$rc" >> "$LOG"
else
  echo "$(date +%H:%M:%S) pfs resetprop missing" >> "$LOG"
fi
exit 0
