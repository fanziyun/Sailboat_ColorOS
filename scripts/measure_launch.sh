#!/usr/bin/env bash
# 用法:
#   measure_launch.sh <app标签> <次数> [设备]     按桌面图标标签定位（布局变动时不可靠）
#   measure_launch.sh <x> <y> <次数> [设备]       直接给 evdev 坐标（x=px*100）
# 例: measure_launch.sh 22800 225400 6

set -uo pipefail

export MSYS_NO_PATHCONV=1

if [ "${1:-}" != "${1:-}" ] 2>/dev/null; then :; fi
case "${1:-}" in
  ''|*[!0-9]*) LABEL=${1:-设置}; N=${2:-4}; DEV=${3:-$(adb devices | awk '/device$/{print $1; exit}')}; XY="" ;;
  *) if [ -n "${2:-}" ] && [ -z "${2//[0-9]/}" ] && [ -n "${3:-}" ] && [ -z "${3//[0-9]/}" ]; then
       XY="$1 $2"; N=${3}; DEV=${4:-$(adb devices | awk '/device$/{print $1; exit}')}; LABEL=coord
     else
       LABEL=${1:-设置}; N=${2:-4}; DEV=${3:-$(adb devices | awk '/device$/{print $1; exit}')}; XY=""
     fi ;;
esac
ADB="adb -s $DEV"
REMOTE=/data/local/tmp/lm_run

[ -n "$DEV" ] || { echo "找不到设备" >&2; exit 1; }

norm() { tr -s ' \t' ' ' | sed 's/^ //;s/ $//'; }

icon_xy() {
  $ADB shell uiautomator dump /data/local/tmp/ui_m.xml >/dev/null 2>&1
  $ADB exec-out su -c 'cat /data/local/tmp/ui_m.xml' 2>/dev/null | python3 -c "
import re,sys
d=sys.stdin.read()
want=sys.argv[1]
for m in re.finditer(r'<node[^>]*>', d):
    t=m.group(0)
    txt=re.search(r'text=\"([^\"]*)\"',t); cd=re.search(r'content-desc=\"([^\"]*)\"',t)
    b=re.search(r'bounds=\"\[(\d+),(\d+)\]\[(\d+),(\d+)\]\"',t)
    if not b: continue
    x1,y1,x2,y2=map(int,b.groups())
    lab=(txt.group(1) if txt and txt.group(1) else '') or (cd.group(1) if cd and cd.group(1) else '')
    if lab==want and (x2-x1)>80 and (y2-y1)>80:
        print((x1+x2)//2*100, (y1+y2)//2*100); break
" "$LABEL"
}

fg_pkg() {
  $ADB shell 'dumpsys activity activities 2>/dev/null | grep -m1 "ResumedActivity:" | sed "s/.*u0 \([^/]*\)\/.*/\1/"' 2>/dev/null | tr -d '\r'
}

settle() {
  $ADB shell cmd statusbar collapse >/dev/null 2>&1
  $ADB shell input keyevent KEYCODE_HOME >/dev/null 2>&1
  sleep 2
  $ADB shell cmd statusbar collapse >/dev/null 2>&1
}

mkdir -p out/measure
$ADB shell "rm -rf $REMOTE" >/dev/null 2>&1
$ADB shell "mkdir -p $REMOTE" >/dev/null 2>&1

echo "设备=$DEV  目标=$LABEL  次数=$N"
echo "档位: $($ADB shell su -c 'sh /data/adb/modules/sailboat_oppo_perf/boost.sh status' 2>/dev/null | tr '\n' ' ')"
echo

SUM_F=0; SUM_33=0; SUM_16=0; SUM_N=0; VALID=0

for i in $(seq 1 "$N"); do
  settle
  if [ "$(fg_pkg)" != "com.android.launcher" ]; then
    echo "第 $i 次: 跳过（不在桌面，当前前台=$(fg_pkg)）"; continue
  fi
  if [ -z "$XY" ]; then
    XY=$(icon_xy)
    if [ -z "${XY:-}" ]; then
      echo "第 $i 次: 跳过（桌面上找不到「$LABEL」图标）"; continue
    fi
  fi
  X=$(echo "$XY" | cut -d' ' -f1); Y=$(echo "$XY" | cut -d' ' -f2)
  $ADB shell "dumpsys SurfaceFlinger --timestats -clear" >/dev/null 2>&1
  $ADB shell su -c "/data/local/tmp/uinput_tap $X $Y 1500" >/dev/null 2>&1
  sleep 4

  PKG=$(fg_pkg)
  if [ -z "$PKG" ] || [ "$PKG" = "com.android.launcher" ]; then
    echo "第 $i 次: 跳过（点击后没有启动应用，前台=$PKG）"; continue
  fi

  $ADB shell "dumpsys SurfaceFlinger --timestats -dump" > "out/measure/ts_${LABEL}_${i}" 2>&1
  python3 - "out/measure/ts_${LABEL}_${i}" "$PKG" <<'PY'
import re,sys
f,pkg=sys.argv[1],sys.argv[2]
d=open(f,encoding='utf-8',errors='replace').read()
m=re.search(r'presentToPresent histogram is as below:\n(.*?)\n',d)
if not m:
    print(f"    [{pkg}] 无 P2P 直方图"); sys.exit()
h={}
for kv in m.group(1).split():
    k,v=kv.split('='); h[int(k[:-2])]=int(v)
n=sum(h.values())
ge33=sum(v for k,v in h.items() if k>=33)
mid=sum(v for k,v in h.items() if 16<=k<33)
long=[(k,v) for k,v in h.items() if k>=100 and v]
print(f"    [{pkg}] 帧数={n}  P2P>=33ms={ge33} ({100.0*ge33/n:.1f}%)  16-32ms={mid}  长停顿={long}")
open(f+'.stat','w').write(f"{n} {ge33} {mid}\n")
PY
  VALID=$((VALID+1))
done

echo
echo "=== 汇总（有效 $VALID / $N 次）==="
for f in out/measure/ts_${LABEL}_*.stat; do
  [ -f "$f" ] || continue
  read a b c < <(tr -d '\r' < "$f")
  SUM_N=$((SUM_N+a)); SUM_33=$((SUM_33+b)); SUM_16=$((SUM_16+c))
done
if [ "$SUM_N" -gt 0 ]; then
  echo "总帧数=$SUM_N  P2P>=33ms=$SUM_33 ($(python3 -c "print(f'{100.0*$SUM_33/$SUM_N:.2f}')")%)  16-32ms=$SUM_16"
else
  echo "没有有效样本"
fi
