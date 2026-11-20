#!/system/bin/sh
MODDIR=${MODPATH:?}
[ -f "$MODDIR/ux.manifest" ] || abort 'Missing verified UX manifest'
[ -f "$MODDIR/lib/modules/sailboat_sched_assist.ko" ] || abort 'Missing UX kernel module'
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/uxctl.sh" 0 0 0755
ui_print 'Requires the matching no-SA test kernel; stock or built-in SA will be refused.'
ui_print 'Reboot loads the module once. UX policy remains OFF until explicitly enabled.'
ui_print 'Cannot hot-unload. Disable/remove takes effect on the next reboot.'
