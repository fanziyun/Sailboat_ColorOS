#!/system/bin/sh
L=/data/local/tmp/cpu_trace.log
echo "cpu trace start $(date '+%m-%d %H:%M:%S')" > $L
while true; do
  read up r < /proc/uptime
  a=$(cat /sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq 2>/dev/null)
  b=$(cat /sys/devices/system/cpu/cpufreq/policy3/scaling_cur_freq 2>/dev/null)
  c=$(cat /sys/devices/system/cpu/cpufreq/policy7/scaling_cur_freq 2>/dev/null)
  echo "$up $a $b $c" >> $L
  sleep 0.1
done
