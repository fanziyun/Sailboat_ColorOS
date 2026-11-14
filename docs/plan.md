# 移植计划

## 阶段

### P0 环境与可行性（进行中）

1. WSL 复现与 Sailboat 内核同源的构建环境：`android13-5.15-lts` @ 5.15.223 + `kernel/prebuilts/build-tools@main-kernel-build-2024` 的 clang。
2. modversions 冒烟测试：编译 `tools/hello` 的 .ko，insmod 进真机。
   - 通过 → 本地迭代路线成立。
   - 不通过 → 先对齐 config/补丁（ABK 构建时的 graft 与 SUSFS/KSU 补丁），或用 ABK Actions 产出的同树产物。
3. 拉取 OPPO 6.18/5.15 源码（`scripts/fetch_upstream.sh`）。

### P1 sched_assist

- 移植 `sched_assist`：`/proc/oplus_scheduler/sched_assist/{ux_task, sched_assist_scene, enabled, audio}`。
- 与 5.15 WALT 对接（ux_task 优先级、im_flag 输入加速、scene 切换）。
- 在设备上 insmod，核对节点与 `dumpsys` 行为，跑一轮 A/B 实测。

### P2 frame_boost + uad

- `frame_boost`（AFS）与 `uad`（`touch_boost`、`ua_ioctl`）。
- 与 SurfaceFlinger 的动画状态（transact 21016/21020/25000 系列）联动验证。

### P3 task_cpustats / task_load / task_sched

- 任务统计与调度辅助模块；依赖 P1。

### P4 sched_info（第二阶段）

- `/proc/jank_info`、`/proc/oplus_healthinfo` 只读指标，消掉 HAL 的 open 失败日志。

### P5 固化

- 把验证过的代码并入 ABK module_set（`setup.sh` 两阶段注入），随内核构建出镜像。
- 与 `ABK_5.15_BACKPORT` 同批注入；需要的覆盖写在 `port/`，必要时向上游提 PR。

## 验收与测量

- 功能：节点存在、写入生效、HAL 无 open 失败日志。
- 流畅度：SurfaceFlinger timestats `presentToPresent ≥ 33ms` 占比，交替 A/B（同一次开机内，至少 4 对）。
- 速度：应用冷启动 `am_proc_start` → 首帧耗时、桌面转场时长。
- 稳定性：连续 24 小时使用无 panic/重启；`getenforce` 下策略干净。

## 风险

| 风险 | 缓解 |
|---|---|
| modversions CRC 不匹配 | 用同树同 config 同工具链；必要时从 ABK 构建产物取 `Module.symvers` 对齐 |
| 5.15 缺少 OPPO 依赖的内核 hook/导出符号 | 先做符号缺口清单，缺失部分用小的 in-tree graft 补齐（module_set 阶段） |
| CFI（`CONFIG_CFI_CLANG=y`） | .ko 必须用同版本 clang 与同配置编译 |
| sched 相关模块可能 panic/死机 | 单模块逐个加载、先 `rmmod` 回滚路径验证；KSU 模块可一键禁用 |
| 与厂商调度器冲突（WALT/FAS） | 先只读、后写入；每一步做交替 A/B |
