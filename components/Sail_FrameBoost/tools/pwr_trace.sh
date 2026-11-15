#!/system/bin/sh
N=/sys/class/kgsl/kgsl-3d0/min_pwrlevel
L=/data/local/tmp/pwr_trace.log
echo "trace start $(date '+%m-%d %H:%M:%S')" >> $L
last=x
while true; do
  v=$(cat $N 2>/dev/null)
  if [ "$v" != "$last" ]; then
    echo "$(date '+%m-%d %H:%M:%S') $v" >> $L
    last=$v
  fi
  sleep 0.2
done
