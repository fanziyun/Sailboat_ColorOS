# Changelog

## 0.5.0 — 2026-11-16

- KSU 模块新增性能档位与点击提频：`boost.sh` + `tunables.conf`，三档 `light/standard/aggressive`，走 WALT 内核 `input_boost`，不另建用户态守护进程。
- `action.sh` 增加 `boost-on` / `boost-off` / `boost-test`，保留原有 `fb-start` / `fb-stop`；`service.sh` 开机等 `sys.boot_completed` 后再套用默认档位。
- 新增 `tools/uinput_tap`：`/dev/uinput` 真实触摸注入的最小实现，配 NDK 构建脚本。
- 真机验证：kprobe 打在 `sched_walt` 的 `do_input_boost` 上，一次点击命中 1 次，确认走的是内核提频路径。
- 新增 `docs/KSU-性能档位.md`。

## 0.4.0 — 2026-11-15

- 迁入用户态优化组件 `components/Sail_FrameBoost`（源码、工具、KSU 模块配置）；按用户决定：优化类组件即使实测无可测收益也迁移保留。
- README 增加「用户态优化组件」一节；`.gitignore` 忽略 `*.stackdump`。

## 0.3.0 — 2026-11-15

- 补齐 ColorOS 性能 HAL 需要的 5 个节点：`audio/enable`、`disable_setting`、`sched_prop`、`tpd_cmds`、`tpd_id`，以及 `/proc/sys/kernel/sched_assist_scene`。
- 新增 `docs/接口-oplus_scheduler.md`：15 个节点与 1 个 sysctl 的路径、权限、格式、语义与实现状态。
- 新增 `port/sched_assist/src/sailboat_coloros_nodes.c`：占位实现，接受写入并回显，保证 HAL 的 open/write 不再失败。
- `scripts/wsl/probe_sched_assist.sh` 改为 `build_sched_assist.sh`，产物更名 `sail_sched_assist.ko`；新增 `scripts/package_ksu.sh`。
- 真机验证：15 个节点齐全、5 个新节点读写正常、sysctl 与 proc 节点共用状态；KSU 模块已安装到 `/data/adb/modules/sailboat_oppo_perf/`。

## 0.2.0 — 2026-11-15

- sched_assist 缺口分析完成（`port/sched_assist/README.md`）：26 个 hook、113 个导入符号、结构体布局全部对齐 5.15.223-Sailboat。
- 新增 `tools/slotprobe`：只读统计 `task_struct`/`rq` 的 `android_oem_data1` 槽位占用；实测 0/1 号槽被小米调度栈占用，`OTS_IDX` 改为 2。
- 新增 `scripts/wsl/probe_sched_assist.sh`：从 `src_upstream/` 复现 sched_assist 探针构建。
- 真机跑通一版 sched_assist：`insmod` rc=0、9 个 proc 节点齐全、`ux_task` 读写生效、无内核告警；`rmmod` 不可用（无 `module_exit`）。

## 0.1.0 — 2026-11-15

- 开荒：仓库骨架、源码清单、移植计划。
- 首期范围：sched_assist、frame_boost + uad、task_cpustats/task_load/task_sched。
- P0 冒烟通过：DDK 5.15.202 的 hello.ko 在 5.15.223-Sailboat 上 insmod/rmmod 正常。
