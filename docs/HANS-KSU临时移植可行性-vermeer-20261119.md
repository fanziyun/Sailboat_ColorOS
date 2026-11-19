# HANS：KernelSU 临时移植可行性（vermeer，2026-11-19）

## 状态与结论

本轮按用户要求先探索、先汇报，不开始实现。

- 已核对项目移植原则、两代 OnePlusOSS HANS 源码、ColorOS17 原版模块的消息 ABI、现有 KSU 包装，以及当前设备的关键配置和导出符号。
- 本轮仅新增本文档；未修改内核/模块/KSU 源码，未编译、打包、安装、insmod/rmmod、改属性、改冻结状态、停服务或刷机。
- 推荐：**以 OnePlusOSS 5.15 HANS 为实现底座，核对并补齐 ColorOS17 消息 ABI，编译独立 `sailboat_coloros_hans.ko`，用独立 `Sail_Hans` KSU 包装临时装载。**
- KernelSU 是交付、权限和装载入口，不是 Binder/freezer/netfilter 的替代实现。纯 shell、用户态 daemon 或空 Generic Netlink family 均不能替代完整内核协作。
- 当前证据支持先走可装载模块路线，尚未证明一定不需要内核 graft；全部依赖、CRC、CFI、结构布局和功能验证通过前，不宣称可加载或与原版功能等价。

## 一、源码已找到，不必从零反编译

| 来源 | 实际版本 | 角色 |
|---|---|---|
| `src_upstream/oplus_sm8550/vendor/oplus/kernel/hans/` | OnePlus11 Android15 / 5.15，commit `0241347436d48df011e2a6f6fe2d8740c26a379c` | 目标内核同代实现底座 |
| `src_upstream/oplus_sm8850/vendor/oplus/kernel/hans/` | OnePlus15 Android16 / 6.18，commit `4c03af1d6d462b9573a7a9f85b983c6bae49497a` | 新代行为与接口参照；不等同原版 OnePlus16 Android17 |
| c17 原版 `out/op16_vdlkm/lib/modules/oplus_sys_hans.ko` | 6.18.21 / Android17 / 4K | 仅补开源缺失的消息 ABI、常量和值域 |

2026-11-19 用 `git ls-remote --heads` 核对官方 sm8850 仓库，返回 OnePlus15、15t、Pad3Pro、Pad4 的 16.0 分支，未列出 17.0 分支。此结论仅限当次查询的该仓库。

5.15 目录包含 `hans.c`、`hans_help.c`、`hans_netfilter.c`、`hans.h` 和 Kbuild/构建描述，三份 C 源码共约 1,200 行。Kbuild 将三份源文件链接为同一个 `oplus_hans` 模块。

**两代现成源码都已经使用 Generic Netlink，而不是必须把旧式私有 Netlink 整套改成 Generic Netlink。**

源码入口：

- [5.15 消息结构和协议](../src_upstream/oplus_sm8550/vendor/oplus/kernel/hans/hans.h:44)
- [5.15 family 与消息处理](../src_upstream/oplus_sm8550/vendor/oplus/kernel/hans/hans.c:32)
- [5.15 Binder/信号实现](../src_upstream/oplus_sm8550/vendor/oplus/kernel/hans/hans_help.c:71)
- [5.15 网络实现](../src_upstream/oplus_sm8550/vendor/oplus/kernel/hans/hans_netfilter.c:249)

## 二、协议骨架已吻合，但不能直接宣布完全兼容

### 开源两代共同骨架

| 项目 | 值 |
|---|---|
| family | `oplus_hans` |
| family version | `1`（开源声明） |
| Generic Netlink command / attribute | `1` / `1` |
| 消息字段 | 八个 32 位整数、140 B `rpc_name`、32 位 `persistent` |
| 完整消息长度 | 176 B；不含最后 `persistent` 的旧式消息为 172 B |
| 类型 | 0 异步 Binder、1 同步 Binder、2 冻结事务检查、3 信号、4 网络事件、5/6/7 CPUCTL 相关、8 LOOP_BACK |
| 握手报告 | LOOP_BACK，`code=2` |
| cgroupv2 能力通知 | PKG，`code=-99`；`target_uid=-8000` 请求探测 |
| 内核上报 `port` 常量 | `0x15356`；不是身份认证令牌 |
| 网络命令 | `pkg_cmd=0/1/2` 分别添加/删除/清空；`persistent=0/1/2` 涉及单次监听、持久记录及持久记录删除 |

### 原版 Android17 二进制的专项复核

本轮使用现有 LLVM，限定核对 `hans_handler`、`hans_report` 和数据段，不加载或执行原版模块。

- `hans_handler` 在 `0x414–0x418` 检查 attribute payload 至少 172 B。
- 网络消息和冻结事务消息分别在 `0x45c–0x460`、`0x4bc–0x4c0` 检查是否有完整 176 B；短消息走扩展值 0。
- `hans_report` 在 `0xc0–0xc8` 使用 command 1，在 `0x120–0x128` 使用 attribute 1、长度 176 B。
- `hans_report` 在 `0xcc–0xd8` 写入 `0x15356`；接收扩展字段位于 payload+172。
- 原版 handler 另外识别 `target_uid=-9000/-9001`，分别设置/清除 `g_skip_trans_binder_reply`。这两个 ABI 控制在本地两代 `hans.c` 中没有对应实现，必须作为差异继续核对；不能只回成功而不实现其语义。

原版 SHA-256：`e1d2a98aa214110d9c1b0b49fe4caa01915dd4824d2940751258f945b988ebad`，既有证据位于 c17 `out/hans_module_audit_20261119/`。

2026-11-19 更新：`-9000/-9001` 不再只存在于闭源模块。`OnePlusOSS/android_kernel_modules_and_devicetree_oneplus_sm8850` 的 `oneplus/sm8850_b_16.0_pad_3_pro`、`oneplus/sm8850_b_16.0_pad_4` 分支（head `07ca84e621baf401a8bedc5c9460d7d033ca65e6`，6.18）在 `vendor/oplus/kernel/hans` 公开了该 ABI：

- `hans.h`（blob `a1a6cc8631af691f67db3eda608077bc7ddb1a31`）：`SKIP_TRANS_BINDER_REPLY (-9000)`、`UNSKIP_TRANS_BINDER_REPLY (-9001)`，`g_skip_trans_binder_reply` 默认 1
- `hans.c`（blob `75eeff27dd06631a4fad7834e98e60ceecd02954`）：`FROZEN_TRANS`/`CPUCTL_TRANS` 分支按 `target_uid` 设置/清除该全局
- `hans_help.c`（blob `63f9657380eddba02b483b1864df5431de275322`）：`btrans_type = btrans->need_reply == 1 ? HANS_TRANS_BINDER_TRANSACTION : HANS_TRANS_BINDER_REPLY`，仅在该全局为 0 时上报 reply 型事务

同代对比：`sm8550`（5.15，含 `b_16.0.0_oneplus_11`）与 `sm8650` 的 `hans.c` blob 为 `299e7cb248949611df9782ca07cd6ced64db464d`，与本仓库既有 5.15 快照一致，无此控制值；`sm8750`/`sm8850` 的 `oneplus_13`/`oneplus_15`/`15t` 分支为 `57fb18af107353c0ad5293ef4f220a260ba7b7d8`，也无。也就是说 5.15 目标仍需把 6.18 的 reply-filter 语义移植过来，并先核对目标 `binder_transaction.need_reply` 是否可用。

**已经核对的是骨架与上述常量，不是完整用户态往返，也不是全部消息分支等价。** 仍需核对当前 `/system_ext/bin/hans` 的实际发送者、portid、命令序列、握手解释、断线重连和新增控制值。不得把开源的握手值直接当作所有能力均已实现。

## 三、当前设备的只读可行性证据

本轮使用 `adb devices -l` 已有的 vermeer 连接，不主动重新连接其他设备。当前采样：

- 内核 `5.15.225-Sailboat-ColorOS-20261118`，4096 B 页，SELinux Enforcing。
- `/proc/version`：clang 14.0.7，Android r450784e，ThinLTO。
- `CONFIG_MODULES`、`MODULE_UNLOAD`、`MODVERSIONS`、`CFI_CLANG`、`FREEZER`、`CGROUP_FREEZER`、`NETFILTER`、`IPV6`、`ANDROID_VENDOR_HOOKS` 均为 y。
- **`CONFIG_CFS_BANDWIDTH` 未开启**；`CGROUP_SCHED` 和 `FAIR_GROUP_SCHED` 为 y。

### 实际命中的导出符号

以 `/proc/kallsyms` 的 `__ksymtab_*` 项为依据，而不是仅看到函数名字：

- `__tracepoint_android_vh_binder_preset`
- `__tracepoint_android_vh_binder_trans`
- `__tracepoint_android_vh_binder_reply`
- `__tracepoint_android_vh_binder_alloc_new_buf_locked`
- `__tracepoint_android_vh_do_send_sig_info`
- `genl_register_family` / `genl_unregister_family`
- `nf_register_net_hooks` / `nf_unregister_net_hooks`
- `tracepoint_probe_register` / `tracepoint_probe_unregister`
- `find_task_by_vpid`、`system_freezing_cnt`、`freezing_slow_path`
- `synchronize_rcu`、`synchronize_srcu`、`tracepoint_srcu`

两次符号查询设置了设备侧 8 秒超时，均完成、rc=0。以上是选定依赖的存在性检查，**不是全部导入符号审计，不含 CRC/回调签名/CFI/结构体逐字段证明**。

### 冻结设施

- cgroup v1：`/dev/freezer/frozen/freezer.state` 与 `/dev/freezer/thaw/freezer.state` 存在；只读采样分别为 FROZEN、THAWED。
- cgroup v2：`/sys/fs/cgroup/apps/cgroup.freeze`、`/sys/fs/cgroup/system/cgroup.freeze` 存在；两处只读采样值均为 0。
- `cgroup.controllers` 的根节点输出为空，不能据此认定没有 freezer；v2 freezer 是核心能力，且本轮非根组文件已经实际核对。
- 上述状态不证明所有应用冻结策略已经正确；更不证明冻结由 HANS 触发，HANS 模块当前未见加载。
- `/sys/kernel/btf/vmlinux` 与 `/sys/kernel/kheaders.tar.xz` 可供后续 ABI 核对，但本轮没有提取并完成布局对照。kheaders 不等于完整可构建内核和匹配的 `Module.symvers`。

### 与小米栈共存

当前加载 `millet_binder`、`millet_sig`、`millet_core`、`millet_oem_cgroup`、`millet_pkg`、`binder_gki`、`binder_prio` 等；多个模块为 permanent。

不卸载 Millet，不修改其私有存储。普通 VH 可以存在多个监听者，但这不证明语义无冲突。须验证双重报告、冻结所有权、Binder 锁序及策略是否重复。HANS 的原版接口不应通过 Millet 协议直接冒充。

## 四、需要实现的是真实事件协作

| 能力 | 5.15 已有实现 | 临时模块需要验证 |
|---|---|---|
| 通信 | family、LOOP_BACK、网络命令、冻结事务查询、v2 能力通知 | 与当前原生 hans 双向往返；新增控制值；重连 |
| 同步/异步 Binder | 对冻结目标上报；异步提取 interface token/code | 事务、回复语义和用户指针读取；不能仅模拟日志 |
| Binder 缓冲 | 空间不足时对冻结目标上报 `free_buffer_full` | 按目标 `binder_alloc` 布局和 5.15 回调签名处理 |
| 冻结前事务查询 | 扫描 proc/thread todo 和 transaction_stack，返回待处理事务 | 列表入口可用、锁序、对象寿命；不能“永远没事务” |
| 信号 | 对冻结目标的 KILL/TERM/ABRT/QUIT 上报 | 测试应用内验证通知/解冻协作，不操作 system_server |
| 网络 | IPv4/IPv6 本地入站 TCP、UID 单次监听/持久记录，原实现始终 NF_ACCEPT | UID 匹配、命令删除语义、上报失败处理；不阻断流量 |
| CPUCTL | 源码有条件编译的 CFS quota 检测与相关上报 | 当前内核没有 CFS_BANDWIDTH；不得声称这部分已恢复，也不能通过 KSU 脚本补出编译期能力 |

HANS 在这些源文件中主要做事件通知与协作，不负责替用户态制定全部冻结名单。冻结/解冻策略仍属于 ROM 的原生 hans/framework 与已有 freezer 后端；不另写“按名单 SIGSTOP/SIGCONT”的替代冻结器。

## 五、不能原样复制两代源码

### 5.15 原样代码必须补的安全和生命周期约束

1. **消息校验**：5.15 `hans.c:167–172` 的发送者 UID 检查被注释；手动解析 attribute，不能只依赖 payload 内的 port。用目标 5.15 Generic Netlink 校验设施、明确长度和值域、真实发送者身份与会话 portid；先核对原生服务的 system UID，不能随意要求其具备 root/CAP_NET_ADMIN。
2. **短消息**：5.15 `hans.c:230–239` 在事务检查路径没有像 6.18 那样对 172 B 消息补默认值，却读取 `persistent`。需补齐并测试。上报结构必须整体初始化，避免把未初始化的 `persistent`/尾部字节发送出去。
3. **初始化发布顺序**：family 先注册，daemon port 和 hash/锁稍后初始化；不能让用户态在尚未就绪时完成握手或发命令。需要完整就绪门禁，逐项记录已注册资源并精确回滚。
4. **Binder 列表入口**：`binder_procs`/lock 由 preset hook 缓存；`hans_help.c:482–496` 未检查其是否仍为 NULL 就锁定。晚加载时不能假定已经执行该 hook；未就绪应返回真实错误/重试，不崩溃、不伪报“没有事务”。
5. **任务寿命**：`hans_help.c:200–204` 在 RCU 解锁后仍用 `find_task_by_vpid` 的结果。使用 RCU 内访问或显式 task 引用，并审计同类生命周期。
6. **退出**：上游有 `module_exit` 和普通 VH 注销，不是 sched_assist 那种 RVH 永久注册模型；但尚未证明 rmmod 安全。退出需停止新会话/上报，注销 hook 并等待在途回调，最后释放两份 UID hash。现成 `hans_netfilter_deinit` 只注销网络 hook，没有清空 hash。
7. **失败与日志**：单次监听 UID 在通知前被移除，上报失败处理需审计；拒绝和重连限频，避免修复 family 后转成另一种刷屏循环。

这不是代码审查交付，而是原型实现门槛清单，未在本轮修复。

### 新代代码不能硬搬的部分

- 6.18 preset hook 比 5.15 多一个 `binder_proc *` 参数，alloc hook 多一个 `bool *should_fail`；不能改目标导出 hook 签名或直接使用新签名。
- 新代 `fg_todo` 使用 Binder OEM 存储扩展；不能据此占用小米的 OEM 槽位。
- 新代对 incoming transaction 使用 `binder_transaction.start_time` 做 30 分钟过滤；是否能在目标结构中复用尚未核对，不能新增/移动 KMI 字段。
- 冻态查询使用 5.15 的 freezer/task 形态，不复制 6.18 的 TASK_FROZEN 或 `freezer_active` 等依赖。
- 两代 `hans_netfilter.c` 的本轮内容对比相同，可以以 5.15 为底座，但仍需上述退出/并发/报错处理。

## 六、独立 KSU 原型的建议边界

拟定位置，**均未创建实现**：

- `port/hans/`：适配源码、ABI 约束、构建说明。
- `ksu/Sail_Hans/`：独立 module.prop、装载/状态/停止入口、必要的最小 SELinux 规则。
- 编译产物：`sailboat_coloros_hans.ko`，仅放 out/ 和最终测试包，不入 git。
- ROM 通信名称仍为 `oplus_hans`；新的内部标记采用 `sailboat_coloros_hans` 前缀。

不得塞进当前 `ksu/sailboat_oppo_perf`：该包的 `service.sh` 会在加载模块后运行 boost，并可启动 guard/UniRender RT。混用会污染 HANS 单变量实验。

初版默认不自动装载，不开机无限重试。装载入口做单次检查：版本/架构、重复模块/family、依赖、装载结果、就绪状态、daemon 握手与事件计数。失败明确报告，不能伪报成功。完成手动功能和回滚验证后再考虑默认自启及开机时序。

状态优先暴露真实的就绪/会话/错误/各事件计数，不以创建 `/proc/oplus_*` 空节点作为成果；HANS 的主要原生入口是 Generic Netlink，不是 sched_assist proc。

**KSU 禁用只防下次启动，不等于本次内核已卸载。** 目前已有其他冻结组处于 FROZEN，不能为了退出 HANS 全局解冻其他模块管理的应用。退出前必须明确 HANS 新增冻结策略如何关闭、哪些任务归其管理、如何排空其未决事务和解冻；该协作未核对前，不承诺“一键热回滚”。禁止 force-rmmod。

## 七、实现与验收门槛

### 先本地，不立刻装手机

- 恢复匹配的完整 5.15 构建环境和 r450784e 工具链。旧脚本写死的 `/home/fanziyun/ddk-android13-5.15-20260313/...` 本轮已不存在，不能直接运行旧脚本当作可用构建。
- 取得匹配内核配置、`Module.symvers` 和内核私有 Binder 头文件；用真机 BTF 核对实际访问的 task/Binder/alloc/thread/transaction 字段及锁。
- 完整导入符号、CRC、CFI 逐项核对，不改 vermagic、不关校验。
- 172/176 B 消息、新增 -9000/-9001 控制、错误消息、身份校验、重连、失败回滚与停止路径先通过测试。
- 当前 CPUCTL 缺口及握手能力语义必须明确；不支持的分支不伪装支持。若需要编译期开关或 graft，届时另报范围。

### 设备功能验证需单独获准

- 首先手动单次加载/退出和内核告警检查；真实 hans 能解析 family、握手、通信。
- 仅测试应用验证冻结前 pending Binder、冻结后同步/异步 Binder、低缓冲区、信号、IPv4/IPv6 TCP 唤醒协作。
- 验证 Enforcing 的真实服务通道，仅针对实际 AVC 补最小规则，不关闭 SELinux。
- daemon 重连、模块禁用/重启、热卸载与冻结所有权要验证；不靠“lsmod 有了”验收。
- 不暂停/卸载 Millet，不批量停服务，不改 NoActive，不动温控、调频、FrameBoost、RT 策略。

### 最后才验证性能收益

先确认 family 失败循环和对应原生进程拉起频率是否下降，再按项目口径做同应用持续滚动帧时间、SurfaceFlinger timestats、CPU PSI、功耗的同场景单变量 A/B，至少四对，并做长期稳定性。

968 个不同 PID 不等于已证明 968 次 init 服务重启；接通 family 也不保证所有拉起循环和卡顿消失。HANS 修复不涵盖 URCC/QSPM、Binder UX/FrameBoost、色彩桥或 MIDAS 的其他断链。

## 汇报落点

可行性阶段推荐实现真实的内核 HANS backport，而不是替代冻结器或成功返回桩。

2026-11-19 更新：独立 [HANS 审计原型](../port/hans/README.md) 已实现并用 5.15.202 DDK 构建；新版 62 个导入均在目标内建导出快照中。候选 ZIP 已生成和校验，但 DDK 的 CRC 不是目标的 CRC。候选包不自启、拒绝 `probe`，内核模块本身拒绝 native 模式。设备未安装或装载，真实通信、卸载、冻结协作与性能收益均未验收。

2026-11-19 追加：reply-filter 已按官方 `sm8850_b_16.0_pad_4` 语义移植到 5.15 原型（`need_reply` 区分事务/回复，`-9000/-9001` 开关，上报 `code -200/-100`），DDK 编译与 host 测试通过；目标 BTF 确认 `need_reply:1` 位于偏移 72 位 0。构建产物无 `__versions`，未打包、未上机，`code` 取值尚未与设备原生 daemon 核对。

2026-11-19 再次追加（原生接口实测）：设备上抓到原生 `/system_ext/bin/hans`（uid 1000，port 等于 pid）的 LOOP_BACK 报文为 **177 B**；前 32 B 为 `type=8 port=<snd_portid> caller_uid=0 caller_pid=-1 target_pid=0 target_uid=-1 pkg_cmd=-1 code=0`，第 172 B 之后全 0，与开源 172 B 布局逐字段一致，仅多 1 个尾字节。原型放宽解码到 177 B 并新增默认关闭的 `allow_system_client` 测试开关后，设备上手装 `allow_system_client=1` 可完成握手：`rx=1 rejected=0 session_port=<pid> tx_8=1 tx_4=1`，原生 logcat 从 `default Error msg type+` 变为 `LOOP_BACK+`，进程 63 秒未重启、无新增 `genl_get_family_id failed`。**结论：临时 KSU 模块可以接上系统原生 hans 的 LOOP_BACK 握手。** 仍未验证冻结/网络事件触发的 PKG/FROZEN_TRANS 报文、`-100/-200` code 约定、原生 UID 放行的交付策略，以及 KSU 包级安装/卸载回滚；手动重连（新 pid 重握手）、`rmmod`（rc=0、无 oops）与重载已通过。
