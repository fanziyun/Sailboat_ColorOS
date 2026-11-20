#!/system/bin/sh
MODDIR=${0%/*}
[ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ] || exit 0
exec sh "$MODDIR/uxctl.sh" load
