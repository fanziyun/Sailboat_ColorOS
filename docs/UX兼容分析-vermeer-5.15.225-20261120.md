# UX 兼容分析：vermeer / 5.15.225 / 2026-11-20

## 结论与边界

**推荐保留 Oplus 5.15 的调度 hook 契约，先修完整的任务入出队、生命周期和开关语义，再受控启用 UX；目前没有证据支持先重写 EEVDF 或删除 `set_next_entity()`。**

本轮为静态兼容分析，未改内核/模块实现、未刷机、未加载模块、未写设备开关。设备取证仅为只读；后续 ADB 离线后未继续设备操作。源码推导的故障路径不等于已在真机重现，更不等于已解释 2026-11-15 的全部 panic。

| 项目 | 判断 |
|---|---|
| 当前 sched_assist 的形态 | 真机只读证据支持已内建，`enabled=0`；不再是“仅有未刷入的内建产物” |
| 首要接入缺口 | 当前配置下注册了选任务 hook，却未注册维护 UX 树的任务 enqueue/dequeue hook |
| 旧 EEVDF 根因说法 | `abk_pick_deadline` 赋值不能证明越界调用；不能据此删代码或绕开记账 |
| 额外源码问题 | slab 对象复用未逐次全初始化、关闭后仍有调度副作用、热关闭后可能破坏 UX 权重记账 |
| 用户态 ABI | 5.15 的“写 0”不一定清完 UX，尤其保留 ANIMATOR；不能照旧接口文档做验收 |
| 上机条件 | 修复后先完成同目标配置编译及离线测试；设备启用和回滚验证另行安排 |

## 一、目标版本与证据链

### 1. 设备只读快照

- `uname -a`：`5.15.225-Sailboat-ColorOS-20261118`，`#1 SMP PREEMPT Sun Dec 01 08:10:00 UTC 2024`。
- SELinux：Enforcing；`sched_assist_enabled`：`enabled=0`。
- `/proc/config.gz`：`CONFIG_OPLUS_SCHED_ASSIST=y`、`CONFIG_FAIR_GROUP_SCHED=y`、`CONFIG_CFI_CLANG=y`、`CONFIG_MODVERSIONS=y`。
- `/proc/modules` 有独立且 permanent 的 `sched_walt`，没有外部 `sail_sched_assist`；`kallsyms` 中 Oplus replace handler 和 `sailboat_coloros_nodes_proc_init` 均无模块后缀。结合内建配置，支持 sched_assist 已随内核运行的判断。

`lsmod` 中有 WALT **不代表**编译 sched_assist 时定义了 `CONFIG_SCHED_WALT`，也不代表定义了 `CONFIG_OPLUS_SYSTEM_KERNEL_QCOM`。

### 2. 匹配构建

[fanziyun/ABK 构建 44276746591][build] 的版本串、构建时间、clang r450784e 与设备记录一致。日志记录：

| 来源 | 提交/状态 |
|---|---|
| ACK 分支当时的 remote HEAD | `0d108e1e8c7b51616fafc1e0c9bef100c9d97d86` |
| ABK_5.15_BACKPORT | `06be1d8906bfa7476964f5089be1d399e0d42fde`，模块版本 0.77.0 |
| Sailboat_ColorOS | `ec8008edd358b233f5c19c29ba711a797f26fa3f` |
| 本地 sm8550 参照树 | `0241347436d48df011e2a6f6fe2d8740c26a379c`，无未提交变更 |
| 本地 sm8850 参照树 | `4c03af1d6d462b9573a7a9f85b983c6bae49497a` |

下载归档中没有独立 `.config`、最终源码或补丁报告；实际配置从其 `Image` 的 IKCONFIG 提取。该配置中 QCOM/WALT 两个符号均缺席，`COMPAT=y`、`PROFILING=y`、CFI 开启且不为 permissive；Oplus 配置只出现 `CONFIG_OPLUS_SCHED_ASSIST=y`。

- 归档 payload SHA256 与其 manifest 一致；此检查不是签名验证。
- `Image` SHA256：`8a8866e7b8e4be28a1175259b0d8793011251233d28bf90b1c40a7ec699c6aed`。
- 提取配置 SHA256：`ef92a1ec55f293e6f6ca8da77f434003538c434facbc1d7a9a1ee5c15acaa11d`。

**证据限制：**未读取设备 boot 分区作哈希比对；remote HEAD 不是最终修改树；`setup.sh:17–24` 接受已有缓存、按分支取上游而未固定 SHA。因此本报告核实的是匹配构建记录、归档配置及本地源码/适配规则，不声称还原了运行镜像的逐字节完整源码。下次构建应固定上游 SHA 并归档最终源码差异与配置。

## 二、首要问题：UX 树的任务入出队没有接完整

### 1. 当前配置实际注册什么

[sched_assist.c:65–108][sa-register] 的条件不是“检测手机是否高通”，而是检查 `CONFIG_OPLUS_SYSTEM_KERNEL_QCOM`：

| hook | 当前非 QCOM 配置下 |
|---|---|
| `android_rvh_replace_next_task_fair` | 注册 |
| `android_rvh_check_preempt_wakeup` | 注册 |
| `android_rvh_enqueue_task` / `android_rvh_dequeue_task` | **不注册**，被 QCOM 宏包住 |
| `android_rvh_enqueue_entity` / `android_rvh_dequeue_entity` | 注册，但 handler 中只有 LOCKING_PROTECT 代码；当前配置下为空体 |

`entity` hook 不能代替任务生命周期 hook：调度实体还包括 cgroup，且选中/放回实体树也不等于任务唤醒/睡眠。真正的 UX 插入/摘除实现为 `queue_ux_thread()` → `enqueue_ux_thread()` / `dequeue_ux_thread()`（`sa_common.c:881–990`），两个任务 handler 在 `sa_common.c:1516–1537` 调用它。

本轮取得的 ACK 核心文件、固定 ABK backport 脚本和本仓适配没有其他 `queue_ux_thread` 调用接入。第三方 vendor 模块是否另有桥接未取到完整源码/调用证据，不能默认它会补齐。

### 2. 可构造的故障路径

在没有额外 vendor 桥接的前提下：

1. UX 开启后，HAL 给一个正在 rq 上的普通线程设置 UX。`oplus_set_ux_state_lock()` 会把它插入 UX 树并持有任务引用（`sa_common.c:453–468`）。
2. 线程在尚未耗尽 UX 执行额度前睡眠。核心执行 task dequeue，但本集成没有调用 `dequeue_ux_thread()`，UX 节点可能仍 linked。
3. 下一次 fair 选任务时，`oplus_replace_next_task_fair()` 只检查 CPU 和 `test_task_ux()`，没有验证该任务仍在 rq、`se.on_rq` 或其 cgroup 是否被 throttle（`sa_fair.c:313–351`）。
4. `test_task_ux()` 本身只看开关、fair 类、标记和额度，不检查可运行性（`sa_common.c:760–791`）。同 CPU 的睡眠线程可以通过。
5. hook 将该任务作为 `p` 返回，破坏调度器“候选必须仍为合法可运行任务”的前提。

这是比 deadline stash 猜测更直接、必须先补的接入缺口。它与历史 WALT 出队断言有可解释的关联，但**本轮未复现旧 panic，未把 WALT 的具体断言定位到匹配 vendor 源码，不能宣布唯一根因**。

### 3. 修复方向

- 为 vermeer 的非 Oplus-QCOM 集成显式接上任务 enqueue/dequeue，保留所需 pick/wakeup hook；按 rq 锁及 UX 锁顺序维护两个 UX 树、权重与任务引用。
- dequeue 清理不能因为总开关关闭就跳过，必须能回收已经 linked 的状态。
- picker 增加合法候选校验和可观测的拒绝原因；迁移、调度类切换、cgroup throttle 也要覆盖。防御校验不能代替正确的入出队。
- **不能仅开启 `CONFIG_OPLUS_SYSTEM_KERNEL_QCOM`：**它同时会关闭当前 pick/wakeup 的注册分支，并在 init 自动开启 UX，切换的是整套契约而不是单个补丁。

## 三、EEVDF 核对：旧“删 set_next_entity”路线不成立

对固定提交 [batch15_perf_eevdf.py][eevdf] 的 17 个 pick-logic 文本替换，以 AST 读取字面量、应用到固定 ACK `fair.c` 的隔离副本；没有执行或导入下载的补丁脚本。

结果：17 个旧锚点全部唯一命中；这些替换**没有改变 `pick_next_task_fair()` 函数体**。此检查只证明这组 EEVDF 替换的形态，不是完整 ABK 补丁栈的编译/运行证明。

### 1. simple 与 group 路径的职责不同

固定 ACK `fair.c:7646–7693`：

- group 路径：hook 改 `p/se` 后，核心按新旧任务的实体层级调用 `put_prev_entity()` / `set_next_entity()`。
- simple 路径：核心先 put prev，再调用 hook；hook 置 `repick` 后直接 `goto done`，核心不再执行实体切换循环。
- 因此 5.15 handler 中 `simple && *repick` 时的 `set_next_entity()` 循环是在履行该 hook 契约（`sa_fair.c:752–775`）。**单独删除会漏掉 `cfs_rq->curr` 等状态维护。**

### 2. `abk_pick_deadline` 不是已证实的错误来源

固定脚本 `:1030–1045` 在 `set_next_entity()` 的原摘树逻辑前加上 deadline 快照；`__dequeue_entity()` 的替换也同步维护 EEVDF 的平均虚拟时间记账。这里没有“禁止 hook 调用”的检查。

`abk_pick_eevdf()` 的 RUN_TO_PARITY 使用该快照（`:750–759`）；`pick_next_entity()` 保留 vendor hook 优先权（`:1110–1121`）；wakeup 的 vendor 决策也在 EEVDF fallback 前（`:1210–1238`）。选择 UX 任务会有意覆盖普通 EEVDF 选择策略，这需要公平性和额度验证，**不等于必然破坏内部结构**。

核心 `set_next_task_fair()` 本身也会调用 `set_next_entity()`，所以不能把后者理解为“只允许某一个 picker 内部调用”。目前不应绕过 deadline/平均虚拟时间记账，或把删除这次赋值作为修复。

sm8850 handler 的签名和职责确实已变（`sa_fair.c:1015–1037`），但不能只凭新版删了循环，就把新版半段行为搬到 5.15。若以后决定转移切换职责，必须核心与 handler 成对改动；这不是当前最小修复的前提。

## 四、其他需要修复或明确的语义

### A. 每任务 slab 对象复用残留

5.15 `sa_oemdata.c:27–71` 每次只 `kmem_cache_alloc()`；全初始化函数仅作为 cache constructor 传入（`:133–182,232–234`），不会在每次复用对象时执行。fork reset（`sa_common.h:551–622`）没有清 `im_flag` 等字段；释放也未完整清零。

旧任务写过分类后退出，新任务复用同一对象时可带旧分类。一个直接后果：残留 `IM_FLAG_LAUNCHER_NON_UX_RENDER` 后发生 IM 写入，`sa_sysfs.c:602–605` 会把新任务设为 HEAVY。

sm8850 `sa_oemdata.c:34–42` 已在每次分配后显式全初始化。应移植这种生命周期修复，而不是只在 ctor 增字段。分配失败、现存任务回填、idle 任务及 free 顺序也要一起覆盖。

### B. 总开关不是干净的 off / 安全热切换

- `sa_common.c:1588,1611–1648`：`boost_kill` 默认 1，exit hook/notifier 不看全局开关；CPU cgroup 数字 ID 等于 BGAPP 时仍把 nice 设为 -20 并改亲和性。vermeer 的具体 ID 映射未核对，但源码足以否定“所有行为都已被 enabled=0 屏蔽”。
- 非 QCOM `sa_fair.c:741–749` 的新任务 util 修改也没有全局开关门禁。
- enabled proc write 只赋整数，不排空 UX 树（`sa_sysfs.c:101–122`）；runtime handler 也没有总开关门禁（`sa_priority.c:506–542`）。
- 条件性记账反例：已有 priority=10 的 linked 节点，关开关后再清其 UX，setter 走“只改 state/prio”分支（`sa_common.c:422–427`），没有先摘树。下一次摘除按新 priority 减权（`sa_priority.c:424–425`），可能从 9548 只减 1024，留下 8524 残权重。该例需要“曾启用并仍 linked”的交错，不是本次从未启用时必发。

需定义并实现关闭协议：停止新的策略决策、在正确锁下按原权重排空/结算、平衡任务引用，并明确标签是否保留及再次开启如何重建。不能把反复写 0/1 当成现成安全的 A/B 开关。首轮测试应关闭或显式门控无关 boost_kill/util 改写。

### C. 清除 ABI 与文档不一致

- 文本写 `p <tid> 132` 后再写 `p <tid> 0`，5.15 明确保留 ANIMATOR（`sa_sysfs.c:259–279`）。
- 5.15 ioctl 的零类型/SET 零类型也保留 ANIMATOR；sm8850 对应 ioctl 分支已经改为清全部类型（5.15 `:422–470`；sm8850 `:513–552`）。不同入口不能混为一个 ABI。
- `r <tid>` 即使设置了选中的 TID，仍返回 `-EFAULT`（5.15 `:219–229`）。文本写还受 `PID_MAX_DEFAULT` 限制，超过范围可能返回成功而未设置（`:244–283`）。

需要先按 ROM 实际调用的文本/ioctl 入口固定 ABI，用设置→清除→读回→树成员检查测试，而不只是比较 write 返回值。不能未经核对就把所有清除操作改成 6.18 语义。

### D. 选核、依赖继承和占位接口未组成完整功能

- `CONFIG_SCHED_WALT` 缺席时，`oplus_task_util()` 读 PELT（`sa_common.h:657–665`），不能因设备有 WALT 模块而强行按 Oplus WALT 结构读 vendor 区。
- `select_task_rq_fair` / `find_energy_efficient_cpu` 的注册在上游入口被注释。`set_ux_task_to_prefer_cpu()` 有实现和导出，不等于当前构建已把它接进选核；`can_migrate_task` 的否决也不等于完整的唤醒放置策略。
- 本构建未编入 audio/balance/pipeline/locking 四块；Binder/锁继承的接入不能只凭状态结构里有字段就宣布完成。
- `audio/enable`、`sched_prop`、`tpd_*`、`disable_setting` 的部分接口仍仅存值/回显；尤其后者尚未落实写权限门禁。正式功能验收不能把它们当已完成后端。

## 五、已排除或降级的疑点

1. **父子任务直接共享旧 OTS 指针：**固定 ACK `fork.c:1041–1045` 在 dup hook 前清零 OEM/vendor 区，故不能仅凭 `arch_dup_task_struct` 复制父任务就认定共享。固定 backport 的 fork 改动也没有提供相反证据；完整最终构建树仍应留档核对。此结论不影响 slab 对象复用残留问题。
2. **CFI 必然在 init panic：**`profile_event_register_t` 返回 `unsigned long`，实际声明返回 `int`，类型确实不匹配；但固定 ACK `include/linux/init.h:50` 的 `__init` 已包含 `__nocfi`，而调用所在 `oplus_sched_assist_init()` 带 `__init`。因此不能把该处作为当前 init 必触发 CFI panic 的证据。应修正类型、减少动态查符号依赖，不能以关闭全局 CFI 解决。
3. **OEM 槽一定冲突：**当前适配使用 task OEM[2]，rq 则直接占 OEM 区。源码中的尺寸检查只证明放得下，不能证明没有其他所有者。实际 vendor 栈的 task/rq 槽位所有权与布局仍是启用前门槛，不能直接宣布冲突或安全。

## 六、最小修复与验收顺序

### 第一批：先做可编译的核心兼容补丁，默认关闭

- 固定上游 SHA，并为 ABI/config/hook shape 添加明确检查，缺锚点时停止而不盲改。
- 补齐非 Oplus-QCOM 目标的任务 enqueue/dequeue，保留 5.15 pick/switch 契约；增加合法候选防线与计数。
- 按每次分配完整初始化 OTS，核实槽位和 task/rq 生命周期；不改 KMI 导出结构或签名。
- 总开关增加 readiness 与排空语义；禁用无关 boost_kill 等副作用，使 off 的定义可验证。
- 对齐实际 ROM 使用的 UX 清除/查询 ABI；修正 profile 函数指针类型。
- 禁止 built-in 与旧 `.ko` 共存；当前 KSU loader 无内建识别，不能只靠 lsmod 判断是否已有实现。

这些属于一套完整的 sched_assist 核心兼容修复，不以新建占位节点替代功能。第一批不迁入整套 6.18 EEVDF/WALT，不同时修改调频、RT 或 FrameBoost。

### 第二批：编译与离线检查

- 恢复匹配当前目标的构建环境/工具链；不能把旧 DDK 路径或 5.15.202 编译结果当作 5.15.225 最终证明。
- 先编译；检查 CFI/KMI、目标 config 的条件分支、hook 签名及重复注册。
- 覆盖设置/清除、fork/exit、slab 复用、睡眠/唤醒、跨 CPU 迁移、调度类切换、cgroup throttle、开关切换与计数/权重/引用守恒。
- 在未完成关闭协议前，使用重启后的独立内核测试组，而不是在一个已污染的 rq 上反复写开关。

### 第三批：受控设备验证

先备份并确认可启动的回滚 boot；内建代码通过重启回滚，KSU 禁用不等于撤销本次启动的 hook。设备验证单独确认后才进行：先标签与生命周期，再有限启用，再回收和长期稳定性，最后至少四对交替 A/B。历史 panic 的根因判断必须用新观测与匹配 WALT 断言源码收口。

## 七、本轮已执行检查与未执行项

已执行：只读设备快照、完成构建日志追溯、下载固定补丁源码与构建包、payload 摘要核对、Image 配置提取、17 个 EEVDF pick-logic 锚点的静态套用及函数体对比、按实际配置解析 UX hook 注册分支。

静态结果为“锚点核对成功，同时发现任务 enqueue/dequeue 缺口”，**不是 UX 已通过编译或功能测试**。未执行：新内核构建、设备启用、panic 复现、帧时间/启动耗时/功耗 A/B、最终镜像逐字节源码还原。

本地证据保存在 `out/ux_compat_20261120/`（gitignored）：

- `evidence.vSixkq/`：固定 ACK 文件、固定 EEVDF 脚本、构建日志。
- `artifacts.ntuPe7/image_evidence/kernel.config`：从归档 Image 提取的配置。
- `contract_check/contract-audit.json`：注册分支与 17 步检查结果。
- `contract_check/fair.eevdf-overlay.c`：仅 EEVDF pick-logic 替换后的隔离副本，不是完整最终内核源码。
- `headers.9UQSYe/init.h`：用于排除 init CFI 误报的固定版本宏。
- `analysis_tools/`：独立编写的归档/配置/AST 检查脚本，执行使用 `python -I`，未执行下载目录中的构建脚本。

[build]: https://github.com/fanziyun/ABK/actions/runs/44276746591
[eevdf]: https://github.com/fanziyun/ABK_5.15_BACKPORT/blob/06be1d8906bfa7476964f5089be1d399e0d42fde/scripts/batch15_perf_eevdf.py
[sa-register]: https://github.com/OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8550/blob/0241347436d48df011e2a6f6fe2d8740c26a379c/vendor/oplus/kernel/cpu/sched/sched_assist/sched_assist.c#L65-L108
