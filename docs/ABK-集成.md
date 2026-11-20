# ABK 集成：sched_assist 随内核源码编译

日期：2026-11-18。参考构建：[fanziyun/ABK run 44232649792](https://github.com/fanziyun/ABK/actions/runs/44232649792)。

2026-11-20 状态更新：本文保留上述 **5.15.223 历史构建**的记录。当前真机已运行 `5.15.225-Sailboat-ColorOS-20261118`，只读证据支持 sched_assist 已内建，`enabled=0`；与版本串匹配的构建为 [fanziyun/ABK run 44276746591](https://github.com/fanziyun/ABK/actions/runs/44276746591)。这不等于 UX 调度已启用或验证稳定，完整边界见 [UX 兼容分析](UX兼容分析-vermeer-5.15.225-20261120.md)。

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

## 模块形态底座（ABK 只构建内核）

`sailboat_ux_build_mode=module` 时 ABK 只做一件事：产出**不带 sched_assist 的内核底座**，并把本地编译 `.ko` 所需的内核材料作为 artifact 上传。ABK 不编译 `.ko`、不打包 KSU、不验证模块——这两步在本地完成。

- `CONFIG_OPLUS_SCHED_ASSIST=m`，Image 内不含 sched_assist 符号。
- release 追加 `-UXmodBase-` 标记，例如 `5.15.225-Sailboat-ColorOS-UXmodBase-20261120`，与内建形态区分。
- 上传物为 `common` 源码树 + `out/android13-5.15/common`（剔除 `*.o/*.a/*.ko/*.cmd/*.d`），含 `.config`、`Module.symvers`、`vmlinux`、`System.map`、`abi_symbollist.raw`。

本地链路（见 `docs/UX最小实现-vermeer-20261120.md`）：

```bash
python -I scripts/fetch_ci_artifact.py <artifact_id> out/.../materials.zip   # 需要 gh 或 SAILBOAT_GH_TOKEN
unzip -o out/.../materials.zip -d out/.../materials                          # 取出内层 kernel-build-materials.tar.gz
SAILBOAT_CLANG_BIN=<clang bin> bash scripts/wsl/build_local_ux.sh \
  out/.../materials/kernel-build-materials.tar.gz out/.../dist <workspace>
```

`build_local_ux.sh` 依次调用 `scripts/prepare_ci_target.py`（解包并只重定位构建路径）、`scripts/wsl/build_sched_assist.sh`（编译 + 严格 modpost + 逐项 CRC 核对）、`scripts/collect_ux_build.py --no-sdk`（核对底座并打包 KSU）。

## 未验证与风险

- 产物未刷机、未在真机启动验证；`sched_assist_enabled` 默认 0，节点存在但调度路径未启用。
- ABK 的 EEVDF 回移会改写 `fair.c` 的 pick 路径；`sched_assist` 依赖的 vendor hook 调用点是否在回移后语义不变，需要真机验证（见 `docs/待办-sched_assist启用.md`）。
- 上游分支固定为 `oneplus/sm8550_v_15.0.0_oneplus11`；上游更新不会自动跟进。
- `port/` 里 frame_boost、uad、task_sched 仍是 README，只有 sched_assist 实际编入。

## 内建形态（2026-11-21）

`sailboat_ux_build_mode=builtin` 时，`setup.sh` 在 after_patch 把最小 UX 适配结果作为基线，再调用 `scripts/prepare_animation_boost.py --builtin` 生成 combined profile（sched_assist + FrameBoost V1 + UAD ioctl），Makefile 用 `obj-$(CONFIG_OPLUS_SCHED_ASSIST)`。before_build 保留该源码，只把 `CONFIG_OPLUS_SCHED_ASSIST=y` 写入 defconfig。上游稀疏检出增加 `vendor/oplus/kernel/cpu/uad/ua_ioctl`，供 frame ioctl 源码核对。运行期默认开启，仍拒绝热关。

模块形态（`module`）保持原有最小 UX 流程，combined profile 的 ko 由本地脚本构建。
