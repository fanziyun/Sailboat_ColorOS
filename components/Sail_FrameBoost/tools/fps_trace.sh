#!/system/bin/sh
N=/sys/class/drm/sde-crtc-0/measured_fps
L=/data/local/tmp/fps_trace.log
echo "fps trace start $(date '+%m-%d %H:%M:%S')" > $L
while true; do
  read up rest < /proc/uptime
  v=$(cat $N 2>/dev/null)
  echo "$up $v" >> $L
  sleep 0.25
done
