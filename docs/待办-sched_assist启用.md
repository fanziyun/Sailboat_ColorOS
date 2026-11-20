# 待办：启用 sched_assist

更新：2026-11-20。状态：**已实现最小核心；`-UXmodBase-` 底座与加固版 ko 已打包，并已用系统内 `dd` 刷入 `boot_a`、insmod 常驻、启用 UX；初版 ko 曾触发内核 panic，当前设备跑的是加同 `cfs_rq` 约束的加固版，回滚依赖重启或写回 boot 备份**。

新增 [最小 UX 实现与控制协议](UX最小实现-vermeer-20261120.md)：2026-11-21 起默认ON，首版拒绝热关闭，离线ko不可加载。原兼容分析的缺口已开始在暂存源码适配层修复，目标内核运行验证尚未完成。本地 ko 编译与 KSU 打包链路见同文「本地 ko 编译与 KSU 打包」一节；底座只由 ABK 构建，`.ko` 与 KSU 包全部在本地完成。

最新结论与源码证据以 [UX 兼容分析：vermeer / 5.15.225](UX兼容分析-vermeer-5.15.225-20261120.md) 为准。本文保留历史故障和启用门槛；此前把 EEVDF deadline stash 写成确定根因的说法已撤回。

## 一、当前状态

- 真机只读快照为 `5.15.225-Sailboat-ColorOS-20261118`、SELinux Enforcing，sched_assist 已内建，`enabled=0`。
- HAL 已有 UX 写入链路，`ux_task` 能存取状态，不等于调度策略已经安全生效。
- `enabled=0` 不是完整卸载或无副作用基线：退出 boost、新任务 util 改写等路径不统一受总开关控制。
- 当前配置未定义 `CONFIG_SCHED_WALT` / `CONFIG_OPLUS_SYSTEM_KERNEL_QCOM`；独立 `sched_walt` 模块存在不能推出这些宏已定义。

## 二、历史启用故障

2026-11-15 置 1 后约 80 秒、一次应用启动时内核 panic：

```text
kernel BUG at kernel/sched/walt/walt.c:4488!
pc : android_rvh_dequeue_task+0x40c/0x43c [sched_walt]
Call trace:
 android_rvh_dequeue_task [sched_walt]
 __traceiter_android_rvh_after_dequeue_task
 dequeue_task → __schedule → schedule → do_epoll_wait
（CPU2 PID 8748 system_server）
```

日志来源为 `/data/vendor/diag/last_kmsg`，此前本机 pstore 为空。该记录证明历史开启后发生过故障，**没有证明 2026-11-20 当前构建上重现了相同故障，也未锁定唯一根因**。

当时 cmdline 有 hung-task、softlockup、RCU-stall panic 设置；不能因此把所有重启或画面卡住都归为同一个故障，需以实际日志收口。

## 三、当前更直接的源码缺口

当前非 Oplus-QCOM 分支注册了 UX pick/wakeup，却没有注册维护 UX 树的任务 enqueue/dequeue；entity hook 在当前配置下为空体，不能代替任务 hook。若没有额外 vendor 桥接，已标记线程睡眠后可能仍留在 UX 树，而 picker 没有可运行性校验。该路径必须优先补齐和测试，但尚未实机复现旧 panic。

此外要修复逐次分配的任务数据初始化、关闭/排空/重启用协议，并核对 ROM 实际清除 ABI、OEM 槽位所有权及 CFI 类型。详见兼容分析，不能只屏蔽一个 hook 后把“不再崩”当作根因证明。

## 四、EEVDF 契约纠正

5.15 的 simple 路径中，hook 置 `repick` 后核心直接结束选任务，不再调用 `set_next_entity()`。因此 Oplus handler 的对应切换循环是在履行当前契约，**不可单删**。

固定 ABK EEVDF 脚本仍保留 vendor 选择优先权；`abk_pick_deadline` 记录选中实体的 deadline，不能据此认定 Oplus 调用是越界，也不应绕过该记账。17 个 EEVDF pick-logic 替换的静态套用没有改动 `pick_next_task_fair()` 函数体；此检查不等于重建完整最终内核。

6.18 handler 签名与核心配套不同，不是可以直接套用的删除模板。当前推荐保留 5.15 契约、先补完整接入；若以后迁移切换职责，必须核心与 handler 成对修改。

## 五、启用前门槛

- [ ] 固定上游源码 SHA、保存实际配置和最终源码差异，确认 task/rq OEM 区所有权。
- [ ] 补齐任务入出队、候选合法性与引用/权重守恒，不能靠设置 QCOM 宏整体切换分支。
- [ ] 每次分配初始化 OTS，覆盖 fork、退出、释放与 slab 复用。
- [ ] 明确定义 off 状态；实现关闭时结算和排空，禁止带残留状态反复切开关。
- [ ] 按 ROM 实际入口验证 UX 设置/清除/查询；5.15 文本写 0 会保留 ANIMATOR。
- [ ] 用匹配目标环境编译，核对 CFI/KMI/配置分支；禁止内建版与旧 `.ko` 重复加载。
- [ ] 准备已知可启动的回滚 boot；设备受控验证单独安排，不在分析阶段直接开开关。
- [ ] 稳定后做至少四对交替 A/B 和长期稳定性检查，不以节点存在代替收益验收。

## 六、回滚边界

现有 sched_assist 没有 `module_exit`，rvh hook 也不是可热卸载的迭代路径。内建代码按 boot 镜像/重启回滚；禁用 KSU 包只影响下次加载，不能撤销本次启动已经注册的 hook。不要 force-rmmod，也不要把总开关写 0 等同于完整热回滚。
