# ABK 集成：sched_assist 随内核源码编译

日期：2026-11-18。参考构建：[fanziyun/ABK run 44232649792](https://github.com/fanziyun/ABK/actions/runs/44232649792)。

## 结论

`sched_assist` 已以源码形式注入 ABK 的 `kernel-custom` GKI 构建，编进内核（`built-in.a`），内核名带当天日期。产物：

- 内核版本串：`5.15.223-Sailboat-20261118`
- 构建 artifact：`None_kernel-android13-5.15-X`，其中 `android13-5.15.X-lts-AnyKernel3.zip`
- Image 中可检出 `sched_assist_enabled`、`ux_task_app`、`sailboat_sched_assist` 字符串

## 为什么能进 GKI

`REGISTER_TRACE_VH` 不是 OPLUS 私有框架，它在 `sched_assist.h` 里展开为 `register_trace_##hook(...)`，注册的是 Android 官方 vendor hook：

`android_vh_build_sched_domains`、`android_vh_cgroup_set_task`、`android_vh_dup_task_struct`、`android_vh_exit_signal`、`android_vh_free_task`、`android_vh_sched_setaffinity_early`、`android_rvh_wake_up_new_task`，另外用到 `sched_stat_runtime`、`task_rename` tracepoint。这些在 android13-5.15 GKI 树里都有，所以能直接编译，不需要移植 OPLUS 私有 hook 框架。

## 注入方式（setup.sh）

仓库根目录的 `setup.sh` 是 ABK 外部模块入口，由 `custom_external_modules` 的两个阶段调用：

1. `after_patch`
   - `git clone --depth 1 --filter=blob:none --sparse -b oneplus/sm8550_v_15.0.0_oneplus11` 拉 `OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8550`，只 checkout `vendor/oplus/kernel/cpu/sched/sched_assist` 与 `frame_boost`
   - 拷入内核树 `common/drivers/sailboat_sched_assist/`，附上 `frame_boost/frame_boost_V1/frame_group.h`
   - 覆盖 `port/sched_assist/src/*`（我们的 proc 节点）
   - 套用 `scripts/wsl/build_sched_assist.sh` 里的同一组替换：`frame_group.h` 引用、`OTS_IDX=2`、`sa_sysfs.c` 的 include 与 `sailboat_coloros_nodes_proc_init()` 调用
   - 写 `Makefile` / `Kconfig`，接入 `common/drivers/Kconfig`、`common/drivers/Makefile`
2. `before_build`
   - `CONFIG_OPLUS_SCHED_ASSIST=y` 写进 `gki_defconfig`

只编 8 个 `.c`（`sched_assist.c`、`sa_common.c`、`sa_oemdata.c`、`sa_sysfs.c`、`sa_priority.c`、`sa_fair.c`、`sa_exec.c`、`sa_jankinfo.c`）加我们的 `sailboat_coloros_nodes.c`；`sa_audio.c`、`sa_balance.c`、`sched_assist_locking.c`、`sa_pipeline.c` 受配置开关排除，未编入。

## 构建证据

```text
[sailboat_coloros] stage=after_patch kernel_root=.../android13-5.15-X
[sailboat_coloros] upstream fetched: oneplus/sm8550_v_15.0.0_oneplus11
[sailboat_coloros] injected: 13 c files -> .../common/drivers/sailboat_sched_assist
[sailboat_coloros] stage=before_build ...
[sailboat_coloros] defconfig updated: CONFIG_OPLUS_SCHED_ASSIST=y
  CC      drivers/sailboat_sched_assist/sched_assist.o
  ... 9 个对象 ...
  AR      drivers/sailboat_sched_assist/built-in.a
Linux version 5.15.223-Sailboat-20261118 (build-user@build-host) ... #1 SMP PREEMPT Wed Nov 18 03:49:43 UTC 2026
```

## 未验证与风险

- 产物未刷机、未在真机启动验证；`sched_assist_enabled` 默认 0，节点存在但调度路径未启用。
- ABK 的 EEVDF 回移会改写 `fair.c` 的 pick 路径；`sched_assist` 依赖的 vendor hook 调用点是否在回移后语义不变，需要真机验证（见 `docs/待办-sched_assist启用.md`）。
- 上游分支固定为 `oneplus/sm8550_v_15.0.0_oneplus11`；上游更新不会自动跟进。
- `port/` 里 frame_boost、uad、task_sched 仍是 README，只有 sched_assist 实际编入。
