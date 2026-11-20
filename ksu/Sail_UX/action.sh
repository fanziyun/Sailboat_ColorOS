#!/system/bin/sh
MODDIR=${0%/*}
exec sh "$MODDIR/uxctl.sh" status
