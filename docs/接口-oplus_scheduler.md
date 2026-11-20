# /proc/oplus_scheduler 接口清单

日期：2026-11-15 的接口清单，2026-11-20 已增加最小 profile。

本文件保留早期节点/消费者记录。**当前 `sailboat_coloros_ux_min` 不再对未实现后端伪报成功**：audio/tpd/sched_prop返回`-EOPNOTSUPP`，disable_setting接真实门禁，并新增ux_status；2026-11-21 起默认ON且拒绝热关闭。当前控制协议和验证状态以 [UX 最小实现](UX最小实现-vermeer-20261120.md) 为准。下表中的“占位”表示后端缺失，不表示当前写入成功。

证据来源：

- 消费方：`/odm/bin/hw/vendor-oplus-hardware-performance-V1-service` 里的路径字符串（`strings … | grep -n "/proc/oplus_scheduler"`）。
- 语义：5.15 蓝本 `sa_sysfs.c` / 6.18 `sa_sysfs.c`、`sa_audio.c` 的实现。
- 真机 `/proc/sys/kernel/…`、`/sys/module/tpd/parameters/…` 的实测存在性。

## 一、总表

| # | 路径 | 权限 | 实现 | HAL 是否引用 |
|---|---|---|---|---|
| 1 | `/proc/oplus_scheduler/sched_assist/ux_task` | 0666 | 5.15 上游 | 是 |
| 2 | `/proc/oplus_scheduler/sched_assist/im_flag` | 0666 | 5.15 上游 | 是 |
| 3 | `/proc/oplus_scheduler/sched_assist/sched_assist_scene` | 0666 | 5.15 上游 | 是 |
| 4 | `/proc/oplus_scheduler/sched_assist/sched_impt_task` | 0666 | 5.15 上游 | 是 |
| 5 | `/proc/oplus_scheduler/sched_assist/audio/enable` | 0666 | **占位** | 是 |
| 6 | `/proc/oplus_scheduler/sched_assist/disable_setting` | 0666 | **占位** | 是 |
| 7 | `/proc/oplus_scheduler/sched_assist/sched_prop` | 0666 | **占位** | 是 |
| 8 | `/proc/oplus_scheduler/sched_assist/tpd_cmds` | 0666 | **占位** | 是 |
| 9 | `/proc/oplus_scheduler/sched_assist/tpd_id` | 0666 | **占位** | 是 |
| 10 | `/proc/sys/kernel/sched_assist_scene` | 0644 | 已实现（同一全局变量） | 是 |
| 11 | `/proc/oplus_scheduler/sched_assist/debug_enabled` | 0666 | 5.15 上游 | 否 |
| 12 | `/proc/oplus_scheduler/sched_assist/sched_assist_enabled` | 0666 | 5.15 上游 | 否 |
| 13 | `/proc/oplus_scheduler/sched_assist/ux_task_app` | 0666 | 5.15 上游 | 否 |
| 14 | `/proc/oplus_scheduler/sched_assist/im_flag_app` | 0666 | 5.15 上游 | 否 |
| 15 | `/proc/oplus_scheduler/sched_assist/lowend_plat_opt` | 0666 | 5.15 上游 | 否 |

## 二、节点语义

### 1. `ux_task` — 线程 ux 状态

写入 `<cmd> <pid> [val]`，三段或两段：

| 写入 | 含义 |
|---|---|
| `p <pid> <val>` | 设置线程 ux 状态。`val = 128 + 类型位`，`128` 是 `SA_OPT_SET` |
| `p <pid> 0` | 当前 5.15 实现清除其他类型但保留 ANIMATOR，不是无条件全清 |
| `r <pid>` | 选中该 pid，随后 `read()` 返回它的状态；当前实现即使选中成功也返回 `-EFAULT` |

类型位：`LIGHT 0x1`、`HEAVY 0x2`、`ANIMATOR 0x4`、`LISTPICK 0x8`、`SWIFT 0x4000`、`ONCE 0x8000`、`INHERIT 0x10000`；高位段 `0xXX000000` 是 ux 优先级（`SCHED_ASSIST_UX_PRIORITY_SHIFT = 24`）。

读回格式：`comm=<名> pid=<pid> tgid=<tgid> ux_state=0x%08x ux_prio=%d ux_nice=%d inherit=%lld(bi:%lld rw:%lld mu:%lld) im_flag=0x%08x`。

### 2. `im_flag` — 输入/场景标记

写入 `<cmd> <pid> <flag>`；当前 5.15 的 `flag` 是线程类别编号或对应清除操作编码，内核用 `set_bit` / `clear_bit` 更新 `im_flag` 位集，**不是直接写入整个掩码**。读回同 `ux_task` 的格式。不同版本的设置/清除差异见 [UX 兼容分析](UX兼容分析-vermeer-5.15.225-20261120.md)。

### 3. `sched_assist_scene` — 场景位

写入整数：`0` 清除；带 `SA_SCENE_OPT_SET(0x80)` 表示按位或；否则按位与非。读回 `scene=%d`。

场景位：`LAUNCH 0x1`、`SLIDE 0x2`、`CAMERA 0x4`、`ANIM_START 0x8`、`ANIM 0x10`、`INPUT 0x20`、`LAUNCHER_SI 0x40`。

### 4. `sched_impt_task` — 重要进程登记

写入 `<key> <tgid>`，`key` 取 `fg`（前台应用）、`fgLauncher`（桌面）、`au`（音频应用）。读回 `top(<tgid> <type>) au(<tgid>)`。

### 5. `audio/enable` — 音频优化开关（占位）

写入整数，读回 `enabled=%d`。6.18 里它控制 `sa_audio` 的音频线程 ux 逻辑；本移植未编入 `sa_audio.c`（`CONFIG_OPLUS_CPU_AUDIO_PERF`），所以只存值。注意 6.18 的 `audio/` 目录下还有 `debug`、`status`、`threshold_util` 三个节点，HAL 不引用，**本轮没有建**。

### 6. `disable_setting` — 写权限门禁（占位）

写入整数，读回 `%d\n`，默认 1。6.18 的语义是：`disable_setting != 0` 时，非 `system(1000)` 非 `root(0)` 的进程写 `ux_task` 会被拒。**当前只存值，门禁规则还没接到 `ux_task` 的写路径上**。

### 7. `sched_prop` — 每任务调度属性（占位）

5.15 / 6.18 的 `sa_sysfs.c` 都没有这个节点。6.18 的 `struct oplus_task_struct` 里有 `unsigned long sched_prop` 字段，`game_opt` 的 `gts` 结构里也有同名字段，推测是"每任务调度属性位（uclamp 类）"。格式未知，**当前按原文回显**：写入内容原样保存（截断到 127 字节、去掉首尾空白），读取返回保存的内容。

### 8. `tpd_cmds` / 9. `tpd_id` — tpd 接口（占位）

5.15 与 6.18 的开源源码里都没有。HAL 同时引用了两条路径：

```
/sys/module/tpd/parameters/tpd_cmds
/sys/module/tpd/parameters/tpd_enable
/sys/module/tpd/parameters/tpd_id
/proc/oplus_scheduler/sched_assist/tpd_cmds
/proc/oplus_scheduler/sched_assist/tpd_id
```

即"独立模块 `tpd` 的 sysfs 参数"和"proc 节点"两套形态，HAL 两边都试。本机没有 `tpd` 模块（`/sys/module/tpd` 不存在），所以只建 proc 节点。格式未知，**按原文回显**，同 `sched_prop`。

### 10. `/proc/sys/kernel/sched_assist_scene` — 场景位 sysctl

与第 3 项是同一个全局变量 `global_sched_assist_scene` 的两种入口，`0644`，`proc_dointvec` 处理。HAL 在 proc 节点不可用时可能回落到这里。

## 三、占位节点的行为约定

- 一律 `0666`，open 永不失败。
- `write()` 返回写入字节数（成功），不返回 `-EINVAL`；解析失败也不报错，避免 HAL 打日志。
- `read()` 返回上次写入内容的回显（字符串型）或当前值（整型）。
- 不进任何调度决策路径：这些节点的值当前不影响 `ux_task` 以外的行为。

## 四、还没提供的路径

HAL 里还有 `/proc/sys/kernel/sched_impt_tgid`（与第 4 项配对的另一种形态），5.15 / 6.18 源码都没有对应实现，本轮未建。是否要建，等确认 HAL 在没有它的时候是否报错再定。

## 五、实现位置

- 5.15 上游节点：`sa_sysfs.c`，由构建脚本从 `src_upstream/oplus_sm8550/…/sched_assist/` 取。
- 新增的 5 个节点 + sysctl：`port/sched_assist/src/sailboat_coloros_nodes.c`，在 `oplus_sched_assist_proc_init()` 创完目录后调用（构建脚本用锚点插入调用）。

后续补后端时，只需要改 `sailboat_coloros_nodes.c` 里对应的 read/write 实现，节点路径与权限不动。
