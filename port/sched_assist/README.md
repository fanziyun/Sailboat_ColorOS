# sched_assist

目标：`/proc/oplus_scheduler/sched_assist/*`（`ux_task`、`sched_assist_scene`、`enabled`、`audio`）。

上游来源：

- 6.18：`OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850` `vendor/oplus/kernel/cpu/sched/sched_assist/`
- 5.15：`OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8550` `vendor/oplus/kernel/cpu/sched/sched_assist/`

状态：待拉源码、列符号缺口。

验证：

- 节点存在、读写格式与 ROM 端调用一致。
- HAL 出现 `/proc/oplus_scheduler/sched_assist/*` 的 open 不再失败。

未完成：全部。
