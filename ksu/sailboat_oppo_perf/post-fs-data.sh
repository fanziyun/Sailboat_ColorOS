#!/system/bin/sh
MODDIR=${0%/*}
chmod 0644 "$MODDIR"/lib/modules/*.ko 2>/dev/null
chcon u:object_r:vendor_file:s0 "$MODDIR"/lib/modules/*.ko 2>/dev/null
exit 0
