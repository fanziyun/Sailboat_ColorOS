#!/system/bin/sh
L=/data/local/tmp/sel_trace.log
echo "sel trace start $(date '+%m-%d %H:%M:%S')" > $L
while true; do
  read up rest < /proc/uptime
  v=$(dumpsys SurfaceFlinger 2>/dev/null | grep -m2 -E "ConfigId:|Current Policy" | tr -d ' ' | tr '\n' '|')
  echo "$up $v" >> $L
done
