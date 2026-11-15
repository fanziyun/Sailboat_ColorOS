#!/system/bin/sh
D=/data/adb/modules/Sail_FrameBoost
N=/sys/class/kgsl/kgsl-3d0/min_pwrlevel
pkill -x Sail_FrameBoost 2>/dev/null
pkill -f "Sail_FrameBoost/watch.sh" 2>/dev/null
sleep 8
nohup sh $D/watch.sh >/dev/null 2>&1 &
while true; do
  $D/bin/Sail_FrameBoost
  echo 7 > $N 2>/dev/null
  sleep 2
done
