#!/usr/bin/env bash
set -euo pipefail
export MSYS_NO_PATHCONV=1

DEV="14d93093"
ADB="adb -s $DEV"
X=88400
Y=225400

echo "=== 预热与测试 1 次循环 ==="
$ADB shell input keyevent KEYCODE_HOME
sleep 2
$ADB shell su -c "dumpsys SurfaceFlinger --timestats -enable" >/dev/null 2>&1
$ADB shell "dumpsys SurfaceFlinger --timestats -clear" >/dev/null 2>&1

echo "1. 点击打开抖音..."
$ADB shell su -c "/data/local/tmp/uinput_tap $X $Y 1500" >/dev/null 2>&1
sleep 4

echo "2. 回到桌面..."
$ADB shell input keyevent KEYCODE_HOME >/dev/null 2>&1
sleep 2

$ADB shell "dumpsys SurfaceFlinger --timestats -dump" > out/test_dy_cycle.txt 2>&1

python3 - <<'PY'
import re
d = open('out/test_dy_cycle.txt', encoding='utf-8', errors='replace').read()
m = re.search(r'presentToPresent histogram is as below:\n(.*?)\n', d)
if m:
    h = {}
    for kv in m.group(1).split():
        k, v = kv.split('=')
        h[int(k[:-2])] = int(v)
    n = sum(h.values())
    ge33 = sum(v for k, v in h.items() if k >= 33)
    mid = sum(v for k, v in h.items() if 16 <= k < 33)
    on8 = h.get(8, 0)
    print(f'全流程 P2P: 窗口总帧={n} 8ms(120Hz)={on8} ({100.0*on8/n:.1f}%) 16-32ms={mid} ({100.0*mid/n:.1f}%) >=33ms={ge33} ({100.0*ge33/n:.1f}%)')
    print('非零分布:', {k: v for k, v in sorted(h.items()) if v})

print('\n--- 各图层 Timeline 帧与 Jank 统计 ---')
blocks = re.split(r'layerName = ', d)
for b in blocks[1:]:
    name = b.split('\n')[0]
    pkg_m = re.search(r'packageName = (\S*)', b)
    pkg = pkg_m.group(1) if pkg_m else ''
    tot_m = re.search(r'totalTimelineFrames = (\d+)', b)
    jank_m = re.search(r'jankyFrames = (\d+)', b)
    if tot_m and int(tot_m.group(1)) > 0:
        tot = int(tot_m.group(1))
        jank = int(jank_m.group(1))
        # 只关注相关的层
        if any(x in name.lower() for x in ['aweme', 'launcher', 'splash', 'navigation', 'status', 'wallpaper']) or jank > 0:
            print(f'[{name[:48]:48}] 总帧={tot:4} jank={jank:3} ({100.0*jank/tot:4.1f}%)')
            for k in ['sfLongCpuJankyFrames','sfLongGpuJankyFrames','sfSchedulingJankyFrames','appBufferStuffingJankyFrames','appUnattributedJankyFrames']:
                mm = re.search(r'^%s = (\d+)' % k, b, re.M)
                if mm and int(mm.group(1)) > 0:
                    print(f'      {k}={mm.group(1)}')
PY
