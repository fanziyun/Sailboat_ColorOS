#!/system/bin/sh
L=/data/local/tmp/resume_trace.log
echo "trace start $(date '+%m-%d %H:%M:%S')" >> $L
logcat -b events -s wm_on_resume_called:I >> $L 2>&1
