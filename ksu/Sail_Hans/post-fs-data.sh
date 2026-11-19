#!/system/bin/sh
MODDIR=${0%/*}
chmod 0755 "$MODDIR/action.sh" "$MODDIR/service.sh" "$MODDIR/post-fs-data.sh"
chmod 0644 "$MODDIR/module.prop" "$MODDIR/prototype.manifest" "$MODDIR/lib/modules/sailboat_coloros_hans.ko"
exit 0
