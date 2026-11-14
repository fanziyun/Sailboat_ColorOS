# sched_assist

适用：vermeer（SM8550）`5.15.223-Sailboat-20261105`，ColorOS 17 移植（Android 17 / SDK 37）。

目标节点：`/proc/oplus_scheduler/sched_assist/*`。

| 节点 | 权限 | 作用 |
|---|---|---|
| `ux_task` | 0666 | `p <pid> <val>` 设置 ux 状态（`val = 128 + type`，`128`=SA_OPT_SET、类型位见下）；`r <pid>` 读取；`p <pid> 0` 清除 |
| `ux_task_app` | 0666 | 同上，按 tgid 生效 |
| `im_flag` / `im_flag_app` | 0666 | 输入加速标记（音频、滑动等），供 uad / frame_boost 联动 |
| `sched_assist_enabled` | 0666 | 总开关，默认 0 |
| `sched_assist_scene` | 0666 | 场景位（launch/slide/camera/anim/input） |
| `sched_impt_task` | 0666 | 重要任务名单 |
| `lowend_plat_opt` | 0666 | 低端平台优化开关 |
| `debug_enabled` | 0666 | 调试位 |

ux 类型位：`SA_TYPE_LIGHT 0x1`、`SA_TYPE_HEAVY 0x2`、`SA_TYPE_ANIMATOR 0x4`、`SA_TYPE_LISTPICK 0x8`、`SA_TYPE_SWIFT 0x4000`、`SA_TYPE_ONCE 0x8000`、`SA_TYPE_INHERIT 0x10000`；`SA_OPT_SET 0x80`、`SA_OPT_RESET 0x100`、`SA_OPT_SET_PRIORITY 0x200`。

## 上游来源

- 主参照（行为）：`OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850`（6.18，一加 16）`vendor/oplus/kernel/cpu/sched/sched_assist/`，33 文件。
- 移植蓝本（代码）：`OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8550`（5.15，一加 11）`vendor/oplus/kernel/cpu/sched/sched_assist/`，27 个文件，`.c` 合计约 11.1k 行，其中约 6.4k 行在四个配置开关后面。
- 反编译补充：c17 `out/op16_vdlkm_vendor_boot/`（`sched_assist.ko` 所在 vendor_boot 拆包），仅用于校正 ioctl 号与参数值域，本轮未用到。

5.15 蓝本按配置开关分块，编译门槛如下：

| 文件 | 行数 | 开关 | 本轮是否编入 |
|---|---|---|---|
| `sched_assist.c` | 205 | — | 是 |
| `sa_common.c` | 1702 | — | 是 |
| `sa_sysfs.c` | 1114 | — | 是 |
| `sa_fair.c` | 822 | — | 是 |
| `sa_priority.c` | 543 | — | 是 |
| `sa_oemdata.c` | 251 | — | 是 |
| `sa_jankinfo.c` | 91 | — | 是 |
| `sa_exec.c` | 14 | — | 是 |
| `sched_assist_locking.c` | 604 | `CONFIG_LOCKING_PROTECT` | 否 |
| `sa_audio.c` | 623 | `CONFIG_OPLUS_CPU_AUDIO_PERF` | 否 |
| `sa_balance.c` | 4493 | `CONFIG_OPLUS_FEATURE_LOADBALANCE` | 否 |
| `sa_pipeline.c` | 668 | `CONFIG_OPLUS_FEATURE_PIPELINE` | 否 |

## 缺口分析（2026-11-15 实测）

结论：**依赖面全部满足**。26 个 hook、113 个导入符号、`task_struct`/`struct rq` 布局在 DDK 与真机之间完全对齐；唯一必须改的是 `OTS_IDX`（见下）。

### 一、vendor hook：26 个，真机全有

`sched_assist` 用 `REGISTER_TRACE_VH/RVH` 注册 26 个；以真机 `/proc/kallsyms` 的 `__ksymtab___tracepoint_*` 为准，逐个核对：

- 24 个 `android_vh_*` / `android_rvh_*` 全部导出，包括 Qualcomm 在 ACK 之上补的 rvh：`place_entity`、`check_preempt_tick`、`enqueue_entity`、`dequeue_entity`、`check_preempt_wakeup`、`find_lowest_rq`、`post_init_entity_util_avg`、`replace_next_task_fair`。
- 另外 2 个是普通 tracepoint，同样导出：`sched_stat_runtime`、`task_rename`。
- 真机共导出 556 个 `android_*` vendor hook，`android_rvh_probe_register` 本身也是 `EXPORT_SYMBOL_GPL`，DDK 实现里**没有模块白名单**，外部 .ko 可以注册。

### 二、导入符号：113 个，真机全导出，0 缺失

用 DDK 5.15.202 编出 `sa_probe.ko` 后取 `nm -u`，与真机 `__ksymtab_*`（11646 项）求差集，结果为空。其中有几个是内核内部调度函数，属于「能编过就能跑」的关键项：`task_rq_lock`、`set_next_entity`、`resched_curr`、`raw_spin_rq_unlock`、`runqueues`、`init_task`、`cpu_scale`、`cpu_topology`。

### 三、结构体布局：DDK 与真机完全一致

DDK `vmlinux` 的 BTF 与真机 `/sys/kernel/btf/vmlinux` 逐字段比对（`pahole`）：

| 项 | DDK 5.15.202 | 真机 5.15.223 | 一致 |
|---|---|---|---|
| `sizeof(struct task_struct)` | 4608 | 4608 | 是 |
| `task_struct.android_oem_data1` | 3392（6 槽） | 3392（6 槽） | 是 |
| `sizeof(struct rq)` | 4544 | 4544 | 是 |
| `rq.android_oem_data1` | 4368（16 槽） | 4368（16 槽） | 是 |

正是这层对齐让 `.ko` 的 `__versions` 校验逐条通过：真机 `insmod` 返回 0，没有出现 `disagrees about version of symbol`。

### 四、必须改：`OTS_IDX` 不能用 0

`sa_common.h` 的 `OTS_IDX` 默认 0，即 `task_struct->android_oem_data1[0]` 存 `oplus_task_struct` 指针，`get_oplus_task_struct()` 只做 `IS_ERR_OR_NULL()` 判断。真机实测（`tools/slotprobe`，7530 个线程 / 8 个 CPU）：

| 槽位 | 非零线程数 | 内容 |
|---|---|---|
| `task android_oem_data1[0]` | 7530/7530 | 恒为 `0xabcdabcdacbdadbc`（毒值），不是 ERR_PTR，会被当成合法指针解引用 |
| `task android_oem_data1[1]` | 7530/7530 | 每线程不同的内核指针（`0xffffff88...`），小米侧调度栈（`sched_walt` / `metis` / `mi_schedule`）在用 |
| `task android_oem_data1[2..5]` | 0/7530 | 空闲 |
| `rq android_oem_data1[0..15]` | 0/8 | 全部空闲，`struct oplus_rq` 可放在 0 号槽 |

所以 5.15 蓝本原样加载会在第一次 `get_oplus_task_struct()` 时解引用 `0xabcdabcdacbdadbc`。接收侧改动：`#define OTS_IDX 2`（2–5 任选，本轮取 2），并保留 `rq->android_oem_data1[0]` 给 `struct oplus_rq`。

### 五、配置差异：本机没有 `CONFIG_SCHED_WALT`

真机 `/proc/config.gz` 里 `CONFIG_SCHED_WALT` 未置位，WALT 是以独立模块 `sched_walt` 存在的（`/proc/kallsyms` 中 `walt_update_task_ravg` 落在 `[sched_walt]`）。影响：

- `sched_assist` 里 `IS_ENABLED(CONFIG_SCHED_WALT)` 的分支不会编进去，少一条依赖，本轮也就不用处理 WALT 结构体。
- 代价是 WALT 侧的联动（例如按 ravg 判定重任务）在本机不可用，ux 的效果只剩 CFS 优先级/抢占/迁移这几条路径。

## 改动文件表

| 文件 | 改动 |
|---|---|
| `sa_common.h` | `OTS_IDX` 0 → 2 |
| `sa_common.c` | `frame_group.h` 的 `<>` 包含改为同目录 `""` 包含（上游用 `<../kernel/oplus_cpu/...>`，指向 OnePlus 树内相对路径） |
| `sa_fair.c` | 同上 |
| 新增 `Makefile` | 8 个对象、`-I$(srctree)/ -I$(srctree)/kernel/sched -I$(src)` |

以上四处在 `scripts/wsl/probe_sched_assist.sh` 里以脚本方式固化，可直接复现。

## 构建

```bash
MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu-26.04 -- bash /mnt/h/Sailboat_ColorOS/scripts/wsl/probe_sched_assist.sh
```

脚本从 `src_upstream/oplus_sm8550` 取源码、落 `out/probe/sa/`、打上面两个补丁，再用 DDK 5.15.202 + clang r450784e 编：

```
make -C <ddk>/opt/ddk/kdir/android13-5.15 M=<repo>/out/probe/sa ARCH=arm64 LLVM=1 PAHOLE=true modules
```

产物 `sa_probe.ko`：2.9 MB，vermagic `5.15.202-android13-5.15.202_r00-dirty`，LTO thin。

## 验证记录

2026-11-15，真机（vermeer / 5.15.223-Sailboat）：

| 项 | 结果 |
|---|---|
| `insmod` | `rc=0`，无 `disagrees about version of symbol`、无 `Unknown symbol` |
| 模块日志 | `[sched_assist][oplus_sched_assist_init]sched assist init succeed!` |
| proc 节点 | `/proc/oplus_scheduler/sched_assist/` 下 9 个节点全部出现：`debug_enabled`、`im_flag`、`im_flag_app`、`lowend_plat_opt`、`sched_assist_enabled`、`sched_assist_scene`、`sched_impt_task`、`ux_task`、`ux_task_app` |
| 写路径 | `echo "p <pid> 130" > ux_task` 后 `r <pid>` 读回 `ux_state=0x00000002`、`ux_prio=0`、`ux_nice=7`；`p <pid> 0` 清除成功 |
| 内核告警 | 加载及操作期间 `dmesg` 中 `WARNING:` / `BUG:` / `Unable to handle` 计数为 0 |
| `rmmod` | **失败**（`Device or resource busy`），见下节 |

## 关键限制

1. **不能 `rmmod`**。上游 `sched_assist.c` 只有 `module_init`，没有 `module_exit`；且 `include/trace/hooks/vendor_hooks.h` 明确写了 `/* vendor hooks cannot be unregistered */` —— `android_rvh_*` 注册后没有注销接口，模块一旦卸载，hook 里留下的函数指针就会指向已释放的内存。因此交接文档里「KSU 模块 insmod/rmmod 迭代」对 sched_assist 不成立：一轮加载就是一次重启，或者在每个 hook 入口加自己的开关闸门。
2. 目前挂着是**空转**状态：`sched_assist_enabled=0`、没有 ux 任务，`oplus_replace_next_task_fair`、`should_ux_task_skip_cpu`、`oplus_check_preempt_wakeup` 都在开头 `if (unlikely(!global_sched_assist_enabled)) return;`；`enqueue_entity` / `dequeue_entity` / `place_entity` 等 handler 在未开 `CONFIG_LOCKING_PROTECT`、`CONFIG_OPLUS_FEATURE_FAKE_CAP` 时是空函数体。
3. 本轮**没有**碰 ROM 侧。`vendor-oplus-hardware-performance` HAL 是否会因为这些节点出现而停止报 open 失败，尚未观察；`sched_assist_enabled` 也尚未置 1。

## 未完成

- [ ] `OTS_IDX` 的取值写进 `src/`（2–5 的最终选择）；`get_oplus_task_struct()` 增加毒值/回指校验，不依赖 `IS_ERR_OR_NULL`。
- [ ] 加 `module_exit` + 每个 hook 入口的 `sa_module_loaded` 闸门，让「卸载后空转」可行。
- [ ] `sched_assist_enabled=1` 后的稳定性与 A/B（先只读一轮，再开 ux）。
- [ ] `sa_audio` / `sched_assist_locking` / `sa_pipeline` / `sa_balance` 四个配置块逐个接入（`sa_balance` 4.5k 行，风险最高，放最后）。
- [ ] HAL 侧核对：`/odm/bin/hw/vendor-oplus-hardware-performance-V1-service` 日志里 open 失败是否消失。
- [ ] 与 `sched_walt`/`metis` 的交互：确认 `tti`、`migt` 等同机调度模块不读 `android_oem_data1[2..5]`。
