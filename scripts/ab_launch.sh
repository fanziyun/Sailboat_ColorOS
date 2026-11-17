#!/usr/bin/env bash
# 用法: ab_launch.sh <app标签> <对数> [设备]
# 用法: ab_launch.sh <x> <y> <对数> [设备]

set -uo pipefail
export MSYS_NO_PATHCONV=1

LABEL=${1:?用法: ab_launch.sh <app标签> <对数> [设备]}
PAIRS=${2:-4}
DEV=${3:-$(adb devices | awk '/device$/{print $1; exit}')}
ADB="adb -s $DEV"
M=/data/adb/modules/sailboat_oppo_perf
TAG=ab_$(date +%H%M%S)

[ -n "$DEV" ] || { echo "找不到设备" >&2; exit 1; }
mkdir -p out/measure

fg() { $ADB shell 'dumpsys activity activities 2>/dev/null | grep -m1 "ResumedActivity:" | sed "s/.*u0 \([^/]*\)\/.*/\1/"' 2>/dev/null | tr -d '\r'; }

locate() {
  $ADB shell uiautomator dump /data/local/tmp/ui_ab.xml >/dev/null 2>&1
  $ADB exec-out su -c 'cat /data/local/tmp/ui_ab.xml' 2>/dev/null | python3 -c "
import re,sys
d=sys.stdin.read(); want=sys.argv[1]
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

one() {
  local tag=$1 i=$2 xy
  $ADB shell cmd statusbar collapse >/dev/null 2>&1
  $ADB shell input keyevent KEYCODE_HOME >/dev/null 2>&1
  sleep 2
  [ "$(fg)" = "com.android.launcher" ] || { echo "  [$tag#$i] 跳过（不在桌面）"; return 1; }
  xy=$(locate)
  if [ -z "${xy:-}" ]; then echo "  [$tag#$i] 跳过（桌面上没有「$LABEL」）"; return 1; fi
  $ADB shell "dumpsys SurfaceFlinger --timestats -clear" >/dev/null 2>&1
  $ADB shell su -c "/data/local/tmp/uinput_tap $(echo $xy | cut -d' ' -f1) $(echo $xy | cut -d' ' -f2) 1500" >/dev/null 2>&1
  sleep 4
  local pkg; pkg=$(fg)
  case "$pkg" in
    com.android.launcher|"") echo "  [$tag#$i] 跳过（点击后未启动，前台=$pkg）"; return 1 ;;
  esac
  $ADB shell "dumpsys SurfaceFlinger --timestats -dump" > "out/measure/${TAG}_${tag}_${i}" 2>&1
  python3 - "out/measure/${TAG}_${tag}_${i}" <<'PY'
import re,sys
d=open(sys.argv[1],encoding='utf-8',errors='replace').read()
m=re.search(r'presentToPresent histogram is as below:\n(.*?)\n',d)
if not m: print("    无直方图"); sys.exit(1)
h={}
for kv in m.group(1).split():
    k,v=kv.split('='); h[int(k[:-2])]=int(v)
n=sum(h.values()); g=sum(v for k,v in h.items() if k>=33)
m2=sum(v for k,v in h.items() if 16<=k<33)
on8=h.get(8,0)
open(sys.argv[1]+'.stat','w').write(f"{n} {g} {m2} {on8}\n")
print(f"    帧数={n} 准时(8ms)={on8} P2P>=33ms={g} ({100.0*g/n:.1f}%) 16-32ms={m2}")
PY
  return 0
}

echo "设备=$DEV 目标=$LABEL 交替 $PAIRS 对  TAG=$TAG"
$ADB shell input keyevent KEYCODE_HOME >/dev/null 2>&1; sleep 2

i=1
while [ "$i" -le "$PAIRS" ]; do
  echo "-- 第 $i 对 --"
  $ADB shell su -c "sh $M/boost.sh on standard" >/dev/null 2>&1
  echo "  [P0 ON ]"; one on "$i"
  $ADB shell su -c "sh $M/boost.sh off" >/dev/null 2>&1
  echo "  [P0 OFF]"; one off "$i"
  i=$((i+1))
done

$ADB shell su -c "sh $M/boost.sh on standard" >/dev/null 2>&1

echo
echo "=== 汇总 ==="
python3 - "out/measure" "$TAG" <<'PY'
import glob,os,sys
d,tag=sys.argv[1],sys.argv[2]
for t in ('on','off'):
    ns=[];gs=[];m2=[];o8=[]
    for f in sorted(glob.glob(os.path.join(d,f'{tag}_{t}_*.stat'))):
        try: a,b,c,e=[int(x) for x in open(f).read().split()]
        except Exception: continue
        ns.append(a);gs.append(b);m2.append(c);o8.append(e)
    if not ns: print(f"P0 {t.upper():3}: 无有效样本"); continue
    N=sum(ns);G=sum(gs);M=sum(m2);O=sum(o8)
    print(f"P0 {t.upper():3}: {len(ns)} 次  总帧={N}  准时(8ms)={O} ({100.0*O/N:.1f}%)  "
          f"P2P>=33ms={G} ({100.0*G/N:.2f}%)  16-32ms={M}  单次最高={max(100.0*b/a for a,b in zip(ns,gs)):.1f}%")
PY
