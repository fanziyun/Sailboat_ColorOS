# Sail HANS 5.15 原型

日期：2026-11-19。目标：vermeer / `5.15.225-Sailboat-ColorOS-20261118` / 4K / Enforcing。

## 当前定位

这是**本地编译、协议与装载门禁验证用原型**，不是已经可替代原生 HANS 的修复包。

- 独立内核模块 `sailboat_coloros_hans.ko`，保持 ROM 的 `oplus_hans` Generic Netlink 接口。
- 独立 KSU 模板 `ksu/Sail_Hans`，不混入 sched_assist/调频/RT 包。
- `native_enabled=1` 在注册任何资源前返回 `-EOPNOTSUPP`；即使不经 KSU 加载也不能启用 native。`allow_test_client` 默认关闭，显式开启后也只接收 root 审计客户端。
- KSU service 不装载模块；打包器固定生成 `target_abi_verified=0`，`probe` 也会被门禁拒绝。没有 force-load、关闭校验、自动重试或自动冻结。
- **未完成原生 ABI，故不允许 native 装载**；不能把本原型的 family 存在或编译成功当成 HANS 已恢复。
- 本轮没有安装到设备、insmod/rmmod、改属性、改 freezer 状态、停服务或刷机。

前期依据：[可行性报告](../../docs/HANS-KSU临时移植可行性-vermeer-20261119.md)。

## 源码依据

- OnePlusOSS sm8550 HANS：commit `0241347436d48df011e2a6f6fe2d8740c26a379c`，`vendor/oplus/kernel/hans/{hans.c,hans.h,hans_help.c,hans_netfilter.c}`，5.15 形态为底座。
- OnePlusOSS sm8850 HANS：commit `4c03af1d6d462b9573a7a9f85b983c6bae49497a`，同目录，参照短消息兼容和新代接口约束。
- 原版 ColorOS17 4K `oplus_sys_hans.ko`：SHA-256 `e1d2a98aa214110d9c1b0b49fe4caa01915dd4824d2940751258f945b988ebad`，仅补消息 ABI/控制值。

实现保留上述开源的主要事件语义，但重写了消息边界、生命周期、失败返回和 UID 状态管理；不是上游文件原样复制。没有新增 task/Binder 结构字段或占用 OEM 槽位，没有改导出 hook 的签名。

## 已实现的源码路径

| 文件 | 职责 |
|---|---|
| `src/hans_protocol.h` | 内核与 host 测试共享的 wire layout、172/176 B 解码、值域、零初始化上报 |
| `src/hans.c` | 真实 Generic Netlink family、发送者/portid 校验、会话、计数、初始化回滚、诊断 proc |
| `src/hans_help.c` | 五个普通 VH、同步/异步 Binder、回复、低缓冲通知、信号、todo/stack 查询 |
| `src/hans_netfilter.c` | 各 net namespace 的 IPv4/IPv6 TCP 入站监听、UID 单次/persistent 状态、失败重挂、退出释放 |
| `../../ksu/Sail_Hans/` | 默认不装载的 KSU 包；校验内核版本、目标 ABI 门禁与 ko SHA-256 |
| `../../tests/hans/` | 共享 C 解析器测试、模糊输入测试、KSU 无加载/无执行注入测试 |

诊断入口：`/proc/sailboat_coloros_hans/status`。该入口是本原型的真实状态，不冒充原版 OPPO proc 驱动。

### 消息约束

- family `oplus_hans`、version 1、command 1、attribute 1。
- 完整结构 176 B；旧式 172 B 解码后 `persistent=0`；ColorOS17 原生发送 177 B（前 172 B 与开源布局一致，第 177 字节恒 0，见下文实测），按 176 B 截断解码；其他长度拒绝。
- 消息必须来自已允许的真实 UID（`allow_test_client` 放行 root 审计客户端，`allow_system_client` 放行 system UID 1000 的原生 hans，均为测试开关、默认关闭），并且 payload port 与 Generic Netlink sender portid 相同。
- 用户态只允许 LOOP_BACK、PKG、FROZEN_TRANS、CPUCTL_TRANS 请求。方向不对、非法 UID/类型/命令/值域均返回 errno，不伪成功。
- LOOP_BACK 上报的 `code=2` 是保持开源 wire 值，不表示目标已经恢复所有 CPUCTL 能力。当前状态单独报告 `cpuctl_available`。
- `target_uid=-8000` 查询 v2 支持；`-9000` 打开 reply-filter（初始默认已开）、`-9001` 关闭。过滤开启时事务扫描只认 `need_reply=1` 的同步事务，`need_reply=0` 的回复型事务跳过；上报 `code` 采用 6.18 语义 `-200`（事务）/`-100`（回复）。

### 与原版仍有差异

1. **reply-filter 已按 6.18 pad 分支实现**（2026-11-19）。依据 `oneplus/sm8850_b_16.0_pad_4`（head `07ca84e6`）的 `hans.h`/`hans.c`/`hans_help.c`：`SKIP_TRANS_BINDER_REPLY (-9000)` 置位、`UNSKIP_TRANS_BINDER_REPLY (-9001)` 清除，`g_skip_trans_binder_reply` 默认 1；扫描时用 `btrans->need_reply` 区分事务/回复，过滤开启下回复型不上报。本原型用 5.15 的 `binder_transaction.need_reply:1`（目标 BTF 偏移 72 位 0）实现同一语义，并把上报 `code` 从旧 5.15 开源树的 1/0 改为 6.18 的 `-200`/`-100`。该 wire 值变化尚未与设备上的原生 daemon 核对。
2. **CPUCTL quota 分支**仅按实际 `CONFIG_CFS_BANDWIDTH` 编译，当前目标该配置未开；未通过 KSU 伪造 quota 能力。
3. **6.18 fg_todo 和 transaction.start_time** 未移植，仍使用目标 5.15 的 todo/stack 形态。未修改 KMI 来硬加新字段。
4. token 读取采用有界 nofault 路径，失败计数、空 token 上报；该降级在原生 framework 的处理仍需验证。
5. 事务 detail 上报保持可用的发送/目标信息与待处理判断，但尚未证明每个 reason/code 与原版 17 完全等价。

这些差异未收敛前，不能作为原生 HANS 修复包启用。

## 生命周期与回滚边界

- hook 注册精确记录已成功项，失败时只撤销这些项并等待在途回调。
- NF 通过 pernet 设施处理 namespace 生命周期，退出后清空 UID 表。
- 不再在 RCU 解锁后继续使用未引用的查找任务。
- Binder 列表入口未取得时，事务查询返回 `EAGAIN`，不会误报没有未决事务。
- hash 限额 4096 UID，重复操作保持有界；通知失败使用 generation 防止复活已删除/替换的监听项。
- 卸载时在 `command_lock` 下停止新命令并等当前命令结束；释放锁后撤销网络和 tracepoint 回调并等待同步，最后注销 Netlink family。
- root 审计模式不接纳 system UID 的原生 hans，不制定冻结名单、不写 cgroup；它的卸载路径仍必须经设备实测。
- KSU 禁用仅阻止后续启动；本轮并未验证实际 rmmod 或设备回滚。

## 构建与打包

依赖恢复脚本 `scripts/fetch_hans_ddk.py` 固定 LFS oid/长度/SHA，分别在新空目录下载三包；安全解压、重定位已知 DDK 内部链接，不执行安装脚本。来源为 KernelSU 文档推荐的 Ylarod DDK，预编译对象仓库 commit `8b6e66bd195c779fb8665315249f146206dae496`。它只是初始编译底座，不证明目标 ABI。

该 DDK 随包的 `modpost` 生成的 `__versions` 段为空，首次候选被打包器拒绝。与 [Android common `android13-5.15.202_r00` 的 `modpost.c`](https://android.googlesource.com/kernel/common/+/refs/tags/android13-5.15.202_r00/scripts/mod/modpost.c) 对照，DDK 源码只注释了 `s->module = exp->module;` 和 `check_exports(mod);` 两行。最终候选在 WSL 隔离 SDK 中从恢复这两行的源码重编主机工具，构建后还原原二进制。未修改生成的 CRC、`Module.symvers`、vermagic、CFI 或内核装载校验；以后使用该 DDK 重建候选也需要标准 `modpost`。

`build_hans.sh` 接受完整 KDIR、匹配 clang bin 和新输出目录；必须有 `.config`、`Module.symvers`，不会改 vermagic 或关闭 CFI。示例需先替换为实际路径：

```powershell
wsl.exe -d Ubuntu-26.04 -- bash /mnt/h/Sailboat_ColorOS/scripts/wsl/build_hans.sh '/path/to/prepared/kdir' '/path/to/clang/bin' /mnt/h/Sailboat_ColorOS/out/build/hans-new
```

测试：

```powershell
wsl.exe -d Ubuntu-26.04 -- bash /mnt/h/Sailboat_ColorOS/scripts/test_hans.sh /mnt/h/Sailboat_ColorOS/out/hans-host-tests-new
```

打包器会核对 ELF64/AArch64/可重定位、5.15 vermagic、modversions、CFI 标记、模块名，并生成候选 ZIP 与校验 JSON：

```powershell
wsl.exe -d Ubuntu-26.04 -- python3 -I /mnt/h/Sailboat_ColorOS/scripts/package_hans.py '/path/to/sailboat_coloros_hans.ko' /mnt/h/Sailboat_ColorOS/out/Sail_Hans-candidate.zip
```

ZIP 固定不自启、未认证目标 ABI，**不可据此刷入并开启 native 策略**。

## 本地验证记录

- 共享 C 解析/上报代码以 `-Wall -Wextra -Werror` 和 ASan/UBSan 执行，172/176/177 B、非法长度和值域、结构偏移、零初始化、token 截断、100,000 个固定种子随机输入通过。
- KSU 六项 host 测试通过：未认证 ABI 不调用 insmod；native 路径被拒绝；状态只读；manifest 不作为 shell 执行；开机服务不装载；未知 force-load 命令拒绝。
- KSU shell 与构建 shell 语法检查通过；Python 下载/打包/测试脚本语法通过。
- 安全解压与非法 ELF 打包拒绝路径八项测试通过；已处理异常分支的解压管道释放。
- SDK 在 NTFS 解压曾因大小写文件名冲突失败，已改到 WSL Linux 文件系统重建并复用校验过的 kdir 包，不删除或覆盖原始包。
- 最终候选在 5.15.202 DDK + r450784e + 标准 `modpost` 下通过 CC/LTO/MODPOST/LD，保留 AArch64 ELF、5.15.202 vermagic 和 `__cfi_check`。`__versions` 共 63 项（62 个导入加 `module_layout`，4032 B），逐项匹配 **DDK** `Module.symvers`。
- 用目标 `5.15.225-Sailboat-ColorOS-20261118` 的只读快照核对：62/62 导入都有内建导出；七个 Binder 类型的 BTF 布局一致，五个 hook 签名与目标 kheaders 一致。`task_struct`、`cgroup` 整体不同，已核对的冻结相关访问字段偏移一致。这里不代表全部 ABI 已验证。
- 产物：`out/Sail_Hans-audit-candidate-20261119.zip`、同名 `.json` 校验报告。ZIP 完整性、归档文件与本地字节一致、SHA-256 和 `target_abi_verified=0`、`autoload=0`、`native_load_blocked=1` 均已复核。
  - `.ko` SHA-256：`c5c9de00cecb86c050895fcc5343ce6e382b27946e88c591c286adf2af39082b`
  - ZIP SHA-256：`5e9b556b55106a33aa64104130fe5f693fd4e880327723d77eeaeacae127602d`
- 2026-11-19 reply-filter 移植：5.15 `binder_transaction.need_reply` 先用 DDK 源码核对，再用目标内核 BTF（`/sys/kernel/btf/vmlinux`，`5.15.225-Sailboat-ColorOS-20261118`）确认字段位于偏移 72 位 0。改动后 DDK 构建通过 CC/LTO/MODPOST/LD，导入符号仍为 62 个（`imports.txt`），host 测试 8/8 通过。构建产物 `out/build/hans_reply_filter_20261119/sailboat_coloros_hans.ko`（SHA-256 `a23004e5513acafff933a5c49065fc9d7fb026cbedd86a97f15e96569dba90ac`）**没有 `__versions` 段**（DDK modpost 未恢复标准检查），只作编译证据，未打包、未上机。
- **目标 CRC、完整 ABI/CFI 兼容性、设备装载与卸载、真实 Netlink 往返、Binder/信号/网络协作、A/B：未验证。**

Host 解析器测试只测试共享解析器，不等于完整内核 Generic Netlink、并发或设备冻结功能已通过。

## 后续门槛

审计包的下一道门槛是取得目标内核的 `Module.symvers` 或等价构建凭据，核对导入 CRC、`module_layout` 和剩余 ABI/CFI 差异。通过后，才可在单独授权下验证审计模式的真机往返和卸载。原生接入还需确定冻结策略退出及解冻语义，并核对 `-100/-200` 上报 `code` 与设备原生 daemon 的约定；最后再做冻结协作与单变量 A/B。不要手动把 manifest 的 ABI 标志改为 1。

## 设备安装与原生接口验证（2026-11-19）

- 安装：`ksud module install /data/local/tmp/Sail_Hans-audit-candidate-20261119.zip`，随后 `ksud soft-reboot`（仅重启用户态，内核 uptime 未重置）。`ksud module list` 可见 `Sail_Hans`。
- 包状态：`action.sh status` 输出 `prototype=1 autoload=0 native_load_blocked=1 loaded=0`。
- 内核侧（重启后）：`/proc/sailboat_coloros_hans`、`/sys/module/sailboat_coloros_hans`、`/proc/oplus_hans`、`/dev/oplus_hans` 均不存在；`/proc/kallsyms` 中 `oplus_hans` 命中 0。
- 原生侧：`logcat` 中 `IHansComunication: HANS createRcvThread: genl_get_family_id failed` 在重启后 42 秒内出现 481 次（PID 19530→26624），`OplusHansManager` 持续输出 `MSG_HANS_DISABLED` 与 `OplusHansManager is not ready`，init 反复拉起 `/system_ext/bin/hans`。
- 结论：**原生程序无法初始化 `oplus_hans` 内核通信接口。** 当前有三层阻断：目标内核没有该 Generic Netlink family；候选包按设计不装载，`probe` 被 `target_abi_verified=0` 拒绝；即使审计模式装载，非 root 的 system UID 客户端也会被 `-EACCES` 拒绝，不会建立会话。
- 包保持已安装但未装载状态；卸载用 `ksud module uninstall Sail_Hans`，无需重新刷机。

## 原生 hans 接入实测（2026-11-19）

- 报文实测：`/system_ext/bin/hans`（uid 1000，port 即 pid）LOOP_BACK 发送 **177 B**，前 32 B 为 `type=8 port=<snd_portid> caller_uid=0 caller_pid=-1 target_pid=0 target_uid=-1 pkg_cmd=-1 code=0`，rpc_name 与尾部全 0。前 172 B 与开源 172 B 布局逐字段一致，ColorOS17 仅多出第 177 字节（恒 0），无需新布局。
- 处理：`hans_decode_message` 接受 172/176/177，177 截断到 176 解码；新增 `allow_system_client` 测试参数放行 system UID 1000，默认 0，打包原语不变。
- 结果：载入 `allow_system_client=1` 后重启原生服务，`rx=1 rejected=0 session_port=<pid> tx_8=1 tx_4=1`；logcat 由 `parseAndProcessMessage: default Error msg type+` 变为 `parseAndProcessMessage: LOOP_BACK+`；原生进程 63 s 未重启、无新增 `genl_get_family_id failed`。该 ko SHA-256 `305007318bba9aa50fb4aa638bf3d13d0af1a4e2ac9e3338e58d0669dbc6b251`，`__versions` 4160 B（65 项）。
- 重连与卸载：`setprop sys.hans.enable false/true` 后原生以新 pid 重连，计数按会话递增（rx=2, tx_8=2, 新 `session_port`）；`rmmod` 返回 0 且内核日志仅 `unloaded`、无 oops，重载后可再次握手（`LOOP_BACK+`）。
- 未验证：冻结/网络事件触发的 PKG/FROZEN_TRANS 原生报文尚未采样；上报 `code -200/-100` 与原生 daemon 的约定仍未核对；原生 UID 放行只是设备测试开关，不是交付默认。
