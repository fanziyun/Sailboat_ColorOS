# 移植计划

## 阶段

### P0 环境与可行性（已完成，2026-11-15）

1. 结论：**DDK `android13-5.15-20260313`（5.15.202）编出的 `.ko` 可以直接在 5.15.223-Sailboat 真机上 `insmod`**。冒烟测试 `tools/hello`：
   - vermagic `5.15.202-android13-5.15.202_r00-dirty`、带 `__versions`；
   - 设备 `CONFIG_MODVERSIONS=y`、`CONFIG_MODULE_SIG_FORCE` 与 `CONFIG_MODULE_FORCE_LOAD` 都未开启；
   - `insmod` 返回 0，`dmesg` 有 `sailboat_hello: loaded`，`lsmod` 可见，`rmmod` 干净。
2. 构建方式（WSL）：`scripts/wsl/hello_ddk.sh`，`make -C <DDK kdir> M=<模块> ARCH=arm64 LLVM=1 PAHOLE=true modules`（DDK 未带 pahole，`PAHOLE=true` 跳过 BTF；模块运行不需要 BTF）。
3. 风险收敛：跨 sublevel 可加载，但符号 CRC 仍以实际使用为准；后续每加一个符号都要过真机加载验证。若出现 CRC 不匹配，再按 ABK 构建产物对齐 `Module.symvers`。
4. 拉取 OPPO 6.18/5.15 源码（`scripts/fetch_upstream.sh`，后台执行）。

### P1 sched_assist

- 移植 `sched_assist`：`/proc/oplus_scheduler/sched_assist/{ux_task, sched_assist_scene, enabled, audio}`。
- 与 5.15 WALT 对接（ux_task 优先级、im_flag 输入加速、scene 切换）。
- 在设备上 insmod，核对节点与 `dumpsys` 行为，跑一轮 A/B 实测。

**进展（2026-11-15）**：缺口分析完成，结论与证据见 `port/sched_assist/README.md`。

- 依赖面全满足：26 个 hook 真机全有、113 个导入符号真机全导出、`task_struct`/`struct rq` 布局与 DDK 逐字段一致。
- 真机已跑通一版：`insmod` rc=0，9 个节点齐全，`ux_task` 写读回 `ux_state=0x2`，无内核告警。
- 两个硬约束改变了原方案：`task_struct.android_oem_data1[0]` 与 `[1]` 已被小米调度栈占用（0 号槽是毒值 `0xabcdabcdacbdadbc`），`OTS_IDX` 必须改；`rmmod` 不可用（无 `module_exit`，且 rvh hook 无法注销），「insmod/rmmod 迭代」不成立。
- 本机 `CONFIG_SCHED_WALT` 未置位，WALT 是独立模块 `sched_walt`，WALT 相关分支不会编入。

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
