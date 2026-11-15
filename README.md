# Sailboat_ColorOS

把 ColorOS（一加 16 / OPPO）的性能调用内核模块 backport 到 vermeer 的 `android13-5.15`（Sailboat）内核，让 ColorOS 性能 HAL、Osense/UIFirst、AFS 帧加速真正落到内核上，提升应用启动与桌面转场表现。

## 现状

- 2026-11-15 开荒。设备：vermeer（SM8550/kalama），内核 `5.15.223-Sailboat-20261105`，系统为的一加 16 `PYB110` ColorOS 17 移植。
- P0 已通过：DDK `android13-5.15-20260313`（5.15.202）编出的 `.ko` 在 5.15.223 真机上可正常 `insmod`/`rmmod`（见 `docs/plan.md` P0），本地迭代路线成立。
- 一加 16 原版上的这些模块是 6.18（`6.18.21-android17-5-o`）形态，vermagic/modversions 与 5.15 不兼容，不能直接加载（结论见 `docs/sources.md` 与 c17 项目 `docs/一加16-vendor_dlkm内核模块核对-ColorOS17.md`）。
- 本仓库按阶段推进：**先 KSU 模块带预编译 .ko 试水（insmod/rmmod 迭代），稳定后转 ABK module_set 并入内核构建固化**。

## 首期范围

| 组件 | 说明 |
|---|---|
| `sched_assist` | `/proc/oplus_scheduler/sched_assist/*`（ux_task、im_flag、scene、audio），其余模块与 HAL 的主通道 |
| `frame_boost` + `uad` | AFS 帧加速与 `touch_boost`/`ua_ioctl` 用户态入口 |
| `task_cpustats` / `task_load` / `task_sched` | 任务统计与调度辅助，依赖 sched_assist |

`sched_info`（`/proc/jank_info`、`/proc/oplus_healthinfo` 只读指标）第二阶段再加。

## 用户态优化组件

按用户 2026-11-15 的决定：**只要是优化就迁移保留**，实测收益不作为去留门槛。

| 组件 | 状态 | 说明 |
|---|---|---|
| `components/Sail_FrameBoost` | 已迁移（源码 + 工具） | 触摸按下抬 kgsl `min_pwrlevel`、覆盖桌面开应用转场；用户态守护进程，随 KSU 模块装载 |

## 源码原则

以 OnePlusOSS 开源源码为主：主参照 6.18 一加 16 的 sm8850 vendor 模块树，5.15 一加 11 的 sm8550 树作为同内核代的可移植实现参考；闭源私有实现只能从 .ko 反编译补充。详见 [docs/sources.md](docs/sources.md)。

## 验收标准

1. 节点齐全：`/proc/oplus_scheduler` 等节点存在，性能 HAL 的 open 失败日志消失、写入生效。
2. `SurfaceFlinger` timestats 的 `presentToPresent ≥ 33ms` 占比在交替 A/B 下下降。
3. 冷启动（`am_proc_start` → 首帧）与转场时长有可测下降。

## 环境

- WSL `Ubuntu-26.04`（`/home/fanziyun`，900G 可用），DDK `android13-5.15-20260313`（5.15.202）。
- 设备 adb 无线 `192.168.31.15:40607`，root 走 KernelSU 3.3.1。
- 构建机与工具链见 [docs/plan.md](docs/plan.md)。

## 目录

```
docs/            方案、源码清单、移植策略
port/            每个特性一个目录：README + src（保留上游路径）
ksu/             KSU 模块（试水阶段的 .ko 装载与调参）
scripts/         拉源码、编译 .ko、打包
tools/           开发验证工具（hello .ko 等）
src_upstream/    拉下来的上游源码（.gitignore，不提交）
```

## 与 ABK 的关系

本仓库是独立的 ABK 外部模块，与 [`fanziyun/ABK_5.15_BACKPORT`](https://github.com/fanziyun/ABK_5.15_BACKPORT) 一起注入 ABK 构建。对上游不满意或需要重构的部分尽量在本仓库写覆盖；必要时向 ABK_5.15_BACKPORT 提兼容 PR。

## 回滚

- 试水阶段：`rmmod` 对应模块即可；KSU 模块在 KernelSU 管理器里禁用。
- 固化阶段：刷回上一版内核镜像（boot.img），内核内的 grafts 随镜像回退。
