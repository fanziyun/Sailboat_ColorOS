#!/system/bin/sh
[ "$ARCH" = arm64 ] || abort 'Sail_Hans requires arm64'
KO="$MODPATH/lib/modules/sailboat_coloros_hans.ko"
[ -f "$KO" ] || abort 'missing HANS kernel module'
EXPECTED=
while IFS='=' read -r key value; do
  [ "$key" = ko_sha256 ] && EXPECTED=$value
done < "$MODPATH/prototype.manifest"
set -- $(sha256sum "$KO")
[ -n "$EXPECTED" ] && [ "$1" = "$EXPECTED" ] || abort 'HANS module SHA256 mismatch'
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/post-fs-data.sh" 0 0 0755
ui_print 'Prototype only: no automatic loading, no native HANS policy.'
ui_print 'Target ABI verification is required before even audit-mode loading.'
