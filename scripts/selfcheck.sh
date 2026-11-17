#!/system/bin/sh
# sailboat 性能模块自检（只读为主，仅对 ux_task 做一次安全读写）
M=/data/adb/modules/sailboat_oppo_perf
IB=/proc/sys/walt/input_boost
SA=/proc/oplus_scheduler/sched_assist
CPU=/sys/devices/system/cpu
FAIL=0

ok()   { echo "[ OK ] $1"; }
bad()  { echo "[FAIL] $1"; FAIL=$((FAIL+1)); }
warn() { echo "[WARN] $1"; }
off()  { echo "[ -- ] $1"; }

echo "=== A 内核模块 ==="
lsmod | grep -q sail_sched_assist && ok "sail_sched_assist 已加载" || bad "sail_sched_assist 未加载"
[ -d /proc/oplus_scheduler ] && ok "/proc/oplus_scheduler 存在" || bad "/proc/oplus_scheduler 不存在"

NODES="debug_enabled disable_setting im_flag im_flag_app lowend_plat_opt sched_assist_enabled sched_assist_scene sched_impt_task sched_prop tpd_cmds tpd_id ux_task ux_task_app"
MISS=""
for n in $NODES; do [ -e "$SA/$n" ] || MISS="$MISS $n"; done
[ -e "$SA/audio/enable" ] || MISS="$MISS audio/enable"
[ -z "$MISS" ] && ok "14 个节点齐全" || bad "缺节点:$MISS"

E=$(cat "$SA/sched_assist_enabled" 2>/dev/null)
[ "$E" = "enabled=1" ] && ok "sched_assist_enabled=1（ux 调度生效）" || off "sched_assist_enabled=0（ux 调度空转，见 docs/待办-sched_assist启用.md）"

echo "=== B 性能档位 ==="
[ -f "$M/tunables.conf" ] && ok "tunables.conf 存在" || bad "tunables.conf 缺失"
PROF=$(cat "$M/current_profile" 2>/dev/null)
FREQ=$(cat "$IB/input_boost_freq" 2>/dev/null | tr -s ' \t' ' ' | sed 's/^ //;s/ $//')
. "$M/tunables.conf" 2>/dev/null
WANT=""
case "$PROF" in
  light) WANT="$LIGHT_FREQS" ;;
  standard) WANT="$STANDARD_FREQS" ;;
  aggressive) WANT="$AGGRESSIVE_FREQS" ;;
esac
if [ "$PROF" = "off" ] || [ -z "$PROF" ]; then
  off "档位未设置（$PROF）"
elif [ -z "$WANT" ]; then
  bad "档位 $PROF 在 tunables.conf 里没有对应参数"
elif [ "$FREQ" = "$WANT" ]; then
  ok "档位 $PROF 的频率值与 tunables.conf 一致"
else
  bad "档位 $PROF 的频率值被改掉（当前 $FREQ）"
fi
ON=$(cat "$IB/sched_boost_on_input" 2>/dev/null)
[ "$ON" = "1" ] && ok "sched_boost_on_input=1" || warn "sched_boost_on_input=$ON（触摸提频关闭）"
MS=$(cat "$IB/input_boost_ms" 2>/dev/null)
[ "$MS" = "$STANDARD_MS" ] && ok "input_boost_ms=$MS" || warn "input_boost_ms=$MS（档位参数未生效或档位非 standard）"

echo "=== C FrameBoost ==="
[ -x "$M/bin/Sail_FrameBoost" ] && ok "Sail_FrameBoost 二进制在模块里" || warn "模块里没有 Sail_FrameBoost"
if pidof Sail_FrameBoost >/dev/null 2>&1; then ok "Sail_FrameBoost 正在运行"; else off "Sail_FrameBoost 未运行（action.sh fb-start 可手动启）"; fi

echo "=== D KSU 模块 ==="
[ -e "$M/disable" ] && bad "模块被 disable 标记停用" || ok "无 disable 标记"
for s in service.sh post-fs-data.sh action.sh boost.sh; do
  [ -x "$M/$s" ] && ok "$s 可执行" || bad "$s 不可执行或缺失"
done
[ -s "$M/lib/modules/sail_sched_assist.ko" ] && ok ".ko 已打包" || bad ".ko 缺失"
echo "       .ko md5=$(md5sum "$M/lib/modules/sail_sched_assist.ko" 2>/dev/null | cut -c1-12) size=$(stat -c%s "$M/lib/modules/sail_sched_assist.ko" 2>/dev/null)"
LINES=$(wc -l < "$M/load.log" 2>/dev/null || echo 0)
[ "$LINES" -gt 40 ] && warn "load.log 已堆积 $LINES 行（从不清理）" || ok "load.log $LINES 行"

echo "=== E HAL 对接 ==="
if pidof vendor-oplus-hardware-performance-V1-service >/dev/null 2>&1; then ok "性能 HAL 进程存活"; else warn "性能 HAL 进程未找到"; fi
sleep 60 > /dev/null 2>&1 &
P=$!
echo "p $P 130" > "$SA/ux_task" 2>/dev/null
R=$( { echo "r $P" > "$SA/ux_task"; } 2>/dev/null; cat "$SA/ux_task" 2>/dev/null )
case "$R" in
  *ux_state=0x00000002*) ok "ux_task 读写自检通过（$R）" ;;
  *) bad "ux_task 读写异常: $R" ;;
esac
echo "p $P 0" > "$SA/ux_task" 2>/dev/null
kill $P 2>/dev/null
echo 0 > /proc/sys/kernel/sched_assist_scene 2>/dev/null
[ "$(cat /proc/sys/kernel/sched_assist_scene 2>/dev/null)" = "0" ] && ok "/proc/sys/kernel/sched_assist_scene 可写" || warn "sched_assist_scene sysctl 异常"

echo "=== F 环境 ==="
echo "       内核 $(uname -r)  SELinux $(getenforce)  开机 $(cut -d' ' -f1 /proc/uptime)s"
echo "       governor=$(cat $CPU/cpu0/cpufreq/scaling_governor 2>/dev/null)"
echo "       min_freq cpu0=$(cat $CPU/cpu0/cpufreq/scaling_min_freq) cpu4=$(cat $CPU/cpu4/cpufreq/scaling_min_freq) cpu7=$(cat $CPU/cpu7/cpufreq/scaling_min_freq)"

echo "=== 结果 ==="
[ "$FAIL" -eq 0 ] && echo "无 FAIL 项" || echo "共 $FAIL 项 FAIL"
