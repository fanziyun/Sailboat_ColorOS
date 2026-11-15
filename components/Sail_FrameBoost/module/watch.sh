#!/system/bin/sh
L=/data/local/tmp/frameboost_watch.log
N=/sys/class/kgsl/kgsl-3d0/min_pwrlevel
P=com.android.systemui
echo "watch start $(date "+%m-%d %H:%M:%S")" >> $L
PJ=0
PT=0
first=1
while true; do
  S=$(dumpsys gfxinfo $P 2>/dev/null)
  J=$(echo "$S" | grep -o "Janky frames: [0-9]*" | head -1 | grep -o "[0-9][0-9]*$")
  T=$(echo "$S" | grep -o "Total frames rendered: [0-9]*" | head -1 | grep -o "[0-9][0-9]*$")
  [ -z "$J" ] && J=0
  [ -z "$T" ] && T=0
  if [ $first -eq 0 ]; then
    DJ=$((J - PJ))
    DT=$((T - PT))
    if [ $DT -ge 5 ] || [ $DJ -gt 0 ]; then
      F=$(dumpsys window displays 2>/dev/null | grep mCurrentFocus | head -1 | sed "s/.*u0 //" | cut -d} -f1 | cut -d/ -f1)
      echo "$(date "+%m-%d %H:%M:%S") jank+$DJ frames+$DT pwr=$(cat $N 2>/dev/null) focus=$F" >> $L
      Z=$(stat -c %s $L 2>/dev/null)
      if [ -n "$Z" ] && [ "$Z" -gt 1048576 ]; then
        tail -c 524288 $L > $L.t
        mv $L.t $L
      fi
    fi
  fi
  PJ=$J
  PT=$T
  first=0
  sleep 20
done
