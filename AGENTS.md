# AGENTS.md

本仓库把 ColorOS 的性能内核模块 backport 到 `android13-5.15`（vermeer / SM8550 / Sailboat 内核）。改动内核源码与模块源码前先读 `docs/porting_policy.md` 与 `docs/sources.md`。

## 源码原则（用户 2026-11-15 明确要求）

1. 以 OnePlusOSS 开源源码为主：6.18 一加 16 的 sm8850 vendor 模块树是行为与接口的主参照，5.15 一加 11 的 sm8550 树是同内核代的可移植实现参考。
2. 闭源/私有实现只能从 .ko 反编译补充，且仅用于补源码缺失的 ABI 细节（ioctl 号、proc 格式、参数值域）。
3. 移植进 5.15 的代码以 5.15 上游形态为准，不把 6.18 的新框架（sched_ext、新版 WALT 内部结构）硬搬。

## 代码规则

- 源码里不写叙述性注释。允许的注释只有两类：记录代码无法表达的约束，且不超过两行。历史、设计说明、实测数据一律写进 `docs/`。
- 新引入的标记一律用 `sailboat_coloros_<group>` 前缀，方便在抓取树、调试日志和 bug 报告里定位。
- KMI：不新增/不移动 `task_struct` 等导出结构字段；确需字段时复用 `ANDROID_KABI_RESERVE` 槽位并记录。改动导出符号签名会破坏 modversions 兼容，禁止。
- 模块名、日志 tag、启动项沿用 `Sail_*` / `sailboat_*` 命名；与 ROM 对接的接口名（`/proc/oplus_*`、action 字符串）保持原样，不改。

## 提交与验证

- 每个特性一个提交或一组提交，提交信息写清「为什么」与验证方式。
- 涉及内核行为的改动必须先过本地编译，再在设备上 insmod/rmmod 验证，最后按 `docs/plan.md` 的验收口径测数据。
- 不把编译产物（.ko、out/、src_upstream/）提交进仓库。
