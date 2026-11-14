# Changelog

## 0.2.0 — 2026-11-15

- sched_assist 缺口分析完成（`port/sched_assist/README.md`）：26 个 hook、113 个导入符号、结构体布局全部对齐 5.15.223-Sailboat。
- 新增 `tools/slotprobe`：只读统计 `task_struct`/`rq` 的 `android_oem_data1` 槽位占用；实测 0/1 号槽被小米调度栈占用，`OTS_IDX` 改为 2。
- 新增 `scripts/wsl/probe_sched_assist.sh`：从 `src_upstream/` 复现 sched_assist 探针构建。
- 真机跑通一版 sched_assist：`insmod` rc=0、9 个 proc 节点齐全、`ux_task` 读写生效、无内核告警；`rmmod` 不可用（无 `module_exit`）。

## 0.1.0 — 2026-11-15

- 开荒：仓库骨架、源码清单、移植计划。
- 首期范围：sched_assist、frame_boost + uad、task_cpustats/task_load/task_sched。
- P0 冒烟通过：DDK 5.15.202 的 hello.ko 在 5.15.223-Sailboat 上 insmod/rmmod 正常。
