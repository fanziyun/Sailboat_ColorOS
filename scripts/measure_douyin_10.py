#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import subprocess
import time
import re
import sys
import os

DEV = "14d93093"
X = 88400
Y = 225400

def adb(cmd, check=True):
    full_cmd = ["adb", "-s", DEV] + cmd
    res = subprocess.run(full_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding='utf-8', errors='replace')
    if check and res.returncode != 0:
        pass
    return res.stdout.strip()

def adb_su(shell_cmd):
    return adb(["shell", "su", "-c", shell_cmd])

def parse_timestats(raw):
    m = re.search(r'presentToPresent histogram is as below:\n(.*?)\n', raw)
    if not m:
        return None
    h = {}
    for kv in m.group(1).split():
        if '=' in kv:
            k, v = kv.split('=')
            h[int(k[:-2])] = int(v)
    total = sum(h.values())
    ge33 = sum(v for k, v in h.items() if k >= 33)
    mid = sum(v for k, v in h.items() if 16 <= k < 33)
    on8 = h.get(8, 0)

    # check janky frames
    jank_blocks = re.findall(r'totalTimelineFrames = (\d+)\njankyFrames = (\d+)', raw)
    timeline_total = sum(int(b[0]) for b in jank_blocks)
    timeline_jank = sum(int(b[1]) for b in jank_blocks)

    # jank types
    stuffing = sum(int(x) for x in re.findall(r'appBufferStuffingJankyFrames = (\d+)', raw))
    sf_sched = sum(int(x) for x in re.findall(r'sfSchedulingJankyFrames = (\d+)', raw))
    sf_cpu = sum(int(x) for x in re.findall(r'sfLongCpuJankyFrames = (\d+)', raw))
    sf_gpu = sum(int(x) for x in re.findall(r'sfLongGpuJankyFrames = (\d+)', raw))
    app_unattr = sum(int(x) for x in re.findall(r'appUnattributedJankyFrames = (\d+)', raw))

    return {
        "total": total,
        "on8": on8,
        "mid": mid,
        "ge33": ge33,
        "ge33_pct": 100.0 * ge33 / total if total > 0 else 0.0,
        "on8_pct": 100.0 * on8 / total if total > 0 else 0.0,
        "hist": {k: v for k, v in sorted(h.items()) if v > 0},
        "timeline_total": timeline_total,
        "timeline_jank": timeline_jank,
        "stuffing": stuffing,
        "sf_sched": sf_sched,
        "sf_cpu": sf_cpu,
        "sf_gpu": sf_gpu,
        "app_unattr": app_unattr
    }

def settle_launcher():
    adb(["shell", "cmd", "statusbar", "collapse"])
    adb(["shell", "input", "keyevent", "KEYCODE_HOME"])
    time.sleep(1.8)
    adb(["shell", "cmd", "statusbar", "collapse"])

def main():
    print(f"==================================================")
    print(f"  连续 10 次模拟人开关抖音实测 (设备: {DEV})")
    print(f"==================================================")

    # Enable SurfaceFlinger timestats
    adb_su("dumpsys SurfaceFlinger --timestats -enable")
    time.sleep(0.5)

    # Initial settle
    settle_launcher()

    open_stats_list = []
    close_stats_list = []

    for cycle in range(1, 11):
        print(f"\n>>> [第 {cycle}/10 次循环] <<<")
        settle_launcher()

        # 1. 测量打开阶段
        adb(["shell", "dumpsys", "SurfaceFlinger", "--timestats", "-clear"])
        adb_su(f"/data/local/tmp/uinput_tap {X} {Y} 1500")
        time.sleep(3.5) # Wait for open transition + initial video play
        raw_open = adb(["shell", "dumpsys", "SurfaceFlinger", "--timestats", "-dump"])
        st_open = parse_timestats(raw_open)
        open_stats_list.append(st_open)

        # 2. 测量关闭阶段 (回到桌面)
        adb(["shell", "dumpsys", "SurfaceFlinger", "--timestats", "-clear"])
        adb(["shell", "input", "keyevent", "KEYCODE_HOME"])
        time.sleep(2.0) # Wait for launcher return transition
        raw_close = adb(["shell", "dumpsys", "SurfaceFlinger", "--timestats", "-dump"])
        st_close = parse_timestats(raw_close)
        close_stats_list.append(st_close)

        # Print cycle brief
        if st_open:
            print(f"  [打开] 总帧={st_open['total']:3d} | 8ms={st_open['on8']:3d} ({st_open['on8_pct']:4.1f}%) | 16-32ms={st_open['mid']:2d} | >=33ms={st_open['ge33']:2d} ({st_open['ge33_pct']:4.1f}%) | 掉帧分布: {[(k,v) for k,v in st_open['hist'].items() if k>=33]}")
        if st_close:
            print(f"  [关闭] 总帧={st_close['total']:3d} | 8ms={st_close['on8']:3d} ({st_close['on8_pct']:4.1f}%) | 16-32ms={st_close['mid']:2d} | >=33ms={st_close['ge33']:2d} ({st_close['ge33_pct']:4.1f}%) | 掉帧分布: {[(k,v) for k,v in st_close['hist'].items() if k>=33]}")

        # Short pause between human operations
        time.sleep(1.0)

    # Settle
    settle_launcher()

    # Aggregate Analysis
    print(f"\n==================================================")
    print(f"               10 次循环汇总统计报告")
    print(f"==================================================")

    def summarize(s_list, title):
        valid = [s for s in s_list if s and s['total'] > 0]
        if not valid:
            print(f"{title}: 无有效数据")
            return
        tot = sum(s['total'] for s in valid)
        on8 = sum(s['on8'] for s in valid)
        mid = sum(s['mid'] for s in valid)
        ge33 = sum(s['ge33'] for s in valid)
        tl_tot = sum(s['timeline_total'] for s in valid)
        tl_jank = sum(s['timeline_jank'] for s in valid)
        stuff = sum(s['stuffing'] for s in valid)
        sf_sc = sum(s['sf_sched'] for s in valid)
        sf_cpu = sum(s['sf_cpu'] for s in valid)
        sf_gpu = sum(s['sf_gpu'] for s in valid)
        app_un = sum(s['app_unattr'] for s in valid)

        print(f"\n【{title}】 (共 {len(valid)} 次有效测试)")
        print(f"  - 呈现总帧数 (P2P Total)  : {tot} 帧")
        print(f"  - 满帧准时帧 (8ms, 120Hz) : {on8} 帧 ({100.0*on8/tot:.2f}%)")
        print(f"  - 视频/常规帧 (16-32ms)   : {mid} 帧 ({100.0*mid/tot:.2f}%)")
        print(f"  - 严重超时帧 (P2P >= 33ms): {ge33} 帧 ({100.0*ge33/tot:.2f}%)")
        print(f"  - 单次最大掉帧率          : {max(s['ge33_pct'] for s in valid):.2f}%")
        print(f"  - 单次最小掉帧率          : {min(s['ge33_pct'] for s in valid):.2f}%")
        print(f"  - Timeline 图层 Jank 分析 : {tl_jank} / {tl_tot} 帧 ({100.0*tl_jank/tl_tot if tl_tot>0 else 0:.2f}%)")
        print(f"      * appBufferStuffing (应用排队) : {stuff} 帧")
        print(f"      * sfScheduling (SF 调度时延)   : {sf_sc} 帧")
        print(f"      * sfLongGpu / sfLongCpu (渲染) : GPU={sf_gpu} 帧, CPU={sf_cpu} 帧")
        print(f"      * appUnattributed (应用自身)   : {app_un} 帧")

    summarize(open_stats_list, "打开应用阶段 (Launcher -> 抖音视频首帧)")
    summarize(close_stats_list, "关闭应用阶段 (抖音 -> 桌面动画归位)")

    # Overall combined
    comb = []
    for o, c in zip(open_stats_list, close_stats_list):
        if o and c:
            comb.append({
                "total": o['total'] + c['total'],
                "on8": o['on8'] + c['on8'],
                "mid": o['mid'] + c['mid'],
                "ge33": o['ge33'] + c['ge33'],
                "ge33_pct": 100.0 * (o['ge33'] + c['ge33']) / (o['total'] + c['total']),
                "on8_pct": 100.0 * (o['on8'] + c['on8']) / (o['total'] + c['total']),
                "timeline_total": o['timeline_total'] + c['timeline_total'],
                "timeline_jank": o['timeline_jank'] + c['timeline_jank'],
                "stuffing": o['stuffing'] + c['stuffing'],
                "sf_sched": o['sf_sched'] + c['sf_sched'],
                "sf_cpu": o['sf_cpu'] + c['sf_cpu'],
                "sf_gpu": o['sf_gpu'] + c['sf_gpu'],
                "app_unattr": o['app_unattr'] + c['app_unattr'],
            })
    summarize(comb, "整周期全流程 (打开 + 关闭 10 次总计)")

if __name__ == '__main__':
    main()
