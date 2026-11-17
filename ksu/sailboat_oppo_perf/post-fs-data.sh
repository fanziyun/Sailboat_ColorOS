#!/system/bin/sh
MODDIR=${0%/*}
chmod 0644 "$MODDIR"/lib/modules/*.ko 2>/dev/null
chcon u:object_r:vendor_file:s0 "$MODDIR"/lib/modules/*.ko 2>/dev/null
chmod 0755 "$MODDIR"/*.sh "$MODDIR"/boost.sh 2>/dev/null
chmod 0644 "$MODDIR"/tunables.conf 2>/dev/null
exit 0
