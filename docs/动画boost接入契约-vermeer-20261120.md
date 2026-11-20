# 动画 boost 接入契约：vermeer / 5.15_BACKPORT（2026-11-20）

## 验收状态

**设备验收未完成。** UX picker 修复和 combined `sailboat_coloros_animation_v1` 已在保留的 UXmodBase 目标上完成编译、严格 MODPOST/链接及 159 项模块版本 CRC 核对；主机 ABI/runtime/policy 测试通过。未打包 KSU、未部署，且当前设备底座与该候选 ko 的 vermagic 不匹配；没有动画事件抵达内核或 A/B 收益数据。

## 设备的当前事实

只读探测时设备 USB serial `14d93093`：

- `uname`：`5.15.225-Sailboat-ColorOS-20261118`，并非先前用于 UX 模块验证的 `-UXmodBase-20261120`。
- `/proc/oplus_scheduler/sched_assist` 已存在，`sched_assist_enabled` 返回 `enabled=0`，没有本项目 `ux_status`。provider 的具体源码/OTS 布局尚未据此确认，不能直接加载新的 SA 与其共存。
- `/proc/oplus_frame_boost`、`/proc/oplus_cpu`、`/proc/sys/fbg` 不存在。
- `vendor.boostfwk.version` 读取为空，而不是已证明的版本 5 或以上；不改属性、不重启服务来伪造能力。
- policy0 governor 是 `walt`，`/proc/modules` 包含 `sched_walt`。GKI 配置没有 `CONFIG_SCHED_WALT=y` 不代表设备没有 WALT。
- 存在性能 HAL `/odm/bin/hw/vendor-oplus-hardware-performance-V1-service`，以及 SF/system_server；静态 fd 快照没有证明有 FrameBoost 打开/调用。

### 2026-11-21 无线 ADB 复核

- 设备经无线 ADB 连通；shell 以 `uid=0(root)`、SELinux 域 `u:r:ksu:s0` 运行，`/system/bin/su` 存在。`uname -r` 仍为 `5.15.225-Sailboat-ColorOS-20261118`，当前 slot 为 `_a`，`ro.boot.verifiedbootstate=orange`、`ro.boot.flash.locked=0`；`boot_a/boot_b` 与 `init_boot_a/init_boot_b` 分区均存在。
- `/proc/config.gz` 再次确认 `CONFIG_OPLUS_SCHED_ASSIST=y`；`/proc/oplus_scheduler/sched_assist/sched_assist_enabled` 为 `enabled=0`。`/proc/modules` 可见 `kernelsu`、`kiwi_v2` 与 `sched_walt`，未见本项目 `sailboat_sched_assist`；FrameBoost proc/sysctl 节点仍缺失。
- SELinux 维持 Enforcing。dmesg 有 `OplusUIFirst` 通过 `system_server` 域对 `proc_oplus_scheduler:sched_assist/ux_task` 执行 ioctl `0x2701` 被拒绝（`avc: denied { ioctl }`）的记录。这证明当前 HAL/服务路径至少有一次被策略阻断；本轮没有改策略、属性、节点或服务。
- 本轮没有备份或写入 boot 分区，没有部署模块，也没有重启。当前 ABK 44809038003 产物是 `UXmodBase`/module 配置，不匹配正在运行的 `20261118` 内建 SA kernel；不得将其直接刷入或加载。
- 用户提供的 ZIP SHA256 为 `04195b7a47725397a2852de125d3e3972199c140c1cfa0939eb90de8fe47f2ad`，bundle manifest 指向 ABK run `44809038003`。其中 AnyKernel `Image` 的 IKCONFIG 为 `CONFIG_OPLUS_SCHED_ASSIST=m` 且没有 `CONFIG_KSU` 项；该 payload 不能当作保留当前 KernelSU root 的安全替换镜像。未验证 manifest 签名，也未写入设备。

原始证据（忽略目录，不提交）：

- `out/animation_probe_20261120_contract/report.json`
- `out/animation_probe_20261120_full/report.json` 与各自独立的 `files/<编号>/`。

后者保存了取证时进程 maps/fd、文件 sha256 与匹配字符串偏移。未收集的文件明确记录在 `uncollected_paths`；无匹配不能推出不存在消费者，压缩/动态生成路径也会漏过明文扫描。

### 已找到的实际 ROM 字符串

- 性能 HAL executable：偏移 `0x433e` 为 `/proc/oplus_scheduler/sched_assist/ux_task`。
- `/system/framework/oplus-framework.jar`：偏移 `0x9f6398` 为 `/proc/oplus_frame_boost/info`，`0x9f6465` 为 `/proc/sys/fbg/frame_boost_debug`；另含 `FBAppManager`、`FBThreadManager`、`UagManager` 与 `enableAnimationCtrl`。
- `/system/bin/surfaceflinger`：含 `OsenseNativeProxy`、`osensemanager` 与 UniRenderEngineUafManager 的请求/降级字符串。

这证明当前 ROM 带有相关代码/接口参照，**不证明静态代码已激活，也不证明 `sys_ctrl` 的实际 ioctl 参数、group ID 与 stage 用法**。下一步应沿 JNI/服务实际调用链核实，而不是根据整张分区镜像的明文扫描结果断言“没有 FrameBoost 消费者”。

## UX picker 修复与编译

`android_rvh_replace_next_task_fair` 的真实 `prev` 沿 handler → `oplus_replace_next_task_fair` → `sailboat_coloros_ux_pick` 显式传入。不能使用 `rq->curr` 猜测 outgoing task；NULL/non-fair prev 或 NULL outgoing cfs_rq 不选取候选。仍保留同 cfs_rq 约束；跨组只跳过，不摘除有效 UX 成员。

测试覆盖 `rq->curr != prev` 的选中/跳过、空 prev/cfs、非 fair prev、过期摘除及关闭门禁；host kernel stubs 不代替真机稳定性验证。

匹配材料来自 `fanziyun/ABK` run `44683587474`，commit `d6c4c5482ea5983f858e0cae57a04d96056a38dd`。保留的准备后目标在 WSL `/home/fanziyun/ux_local_44683587474.gvExWQ/target/`；编译器 Android clang r450784e，CFI + ThinLTO。

- host suite 通过；`out/build/ux_prev_contract_20261120/sailboat_sched_assist.ko` 本地编译成功。
- 严格 modpost/版本表核对：115 个符号 CRC 与目标内核匹配，114 个强导入已覆盖。
- **只证明匹配 UXmodBase 材料的构建有效，不证明当前 20261118 真机可加载；未部署该新 ko。**

## FrameBoost 源码 ABI 与目标 hook

来源、公共 LP64 布局与运行时限制见 [FrameBoost README](../port/frame_boost/README.md)。

- `scripts/test_frame_boost.sh` 检查 SM8550/SM8850 两代源码头文件，各检查 multi_fbg 宏开/关；当前布局、16 个 ioctl 编码和测试覆盖的公共 stage 一致。Host runtime 测试覆盖 snapshot/TOCTOU、tgid、compat、disabled、range 与 unsupported stage gate。
- 6.18 额外 stage（218/219/501..507）和 union 的 group_id 语义仍未支持；不得把布局一致当作 multi_rtg 与 V1 等价。
- `scripts/audit_frame_boost_target.py` 对保留的最终目标进行 Binder/scheduler 原型与 tracepoint 导出核对，结果通过；本轮证据为 `out/build/animation_v1_20261120_02/target-contract.json`。
- `scripts/prepare_animation_boost.py` 生成单一 `sailboat_sched_assist` combined profile：`OTS_IDX=2`、默认开启（UX 与动画 boost；运行期不能热关，停止需重启）、禁热关闭；接入 schedule/migration/placement 与 cpufreq floor consumer，UAD ioctl 走 gated snapshot dispatcher，移除范围外 touch ioctl 初始化依赖。
- 完整目标构建 `out/build/animation_v1_20261120_02/sailboat_sched_assist.ko` 通过 CFI + ThinLTO 编译、MODPOST 与链接。159 个 `__versions` CRC 与保留目标 `Module.symvers` 逐项匹配；compile-only 探针发现的四个未解析导入不再出现在最终 module import/version 表中。
- 上述只证明匹配 UXmodBase 构建材料的产物链接有效，不证明当前 20261118 真机可加载；模块 vermagic 为 `5.15.225-Sailboat-ColorOS-UXmodBase-20261120`，设备为 `5.15.225-Sailboat-ColorOS-20261118`。未部署该候选 ko，也未证明 ROM 实际 ioctl 或 WALT 调频消费。

## 尚未满足的验收门槛

1. 确认目标设备底座唯一 SA provider 与 OEM 槽位归属，并取得与待交付 ko 完全匹配的构建材料；不允许与未知 provider 并载。
2. 对实际 framework/JNI/服务的 FrameBoost 请求做源码/调用证据核对，区分 AFS、UIFirst、Osense 与 SF/UAF；不将手工 tag 当作原生链路成功。
3. 在匹配设备上实证 hook 触发、帧组记账/选核语义和 governor/WALT 对 util floor 的消费；host tests 和更新请求计数不替代设备证据。
4. 用匹配材料完成本地 ABK/ko/KSU 构建打包；当前候选只在 UXmodBase 目标完成 ko 编译/CRC 检查，未构建可交付包。
5. 经确认的安全部署方案后才能加载/刷入。系统内 dd；写 boot_a 前备份并校验，写后按同范围摘要核对。不交付 panic 初版。
6. 设备上只在当前桌面页点击已定位图标；**无论如何不左右滑动桌面**。找不到图标停止。重启后等 40 秒再扫描变化的 ADB 端口。
7. 证明真实动画事件、目标线程调度/调频与无 panic，再至少 4 对交替 A/B 统计 SurfaceFlinger 长帧占比、转场与首帧。没有收益就记录没有收益。

## 2026-11-21 匹配内核部署与真机验证

ABK `run 44809038003`（head `d6c4c5482ea5983f858e0cae57a04d96056a38dd`）的 module-mode 内核已按用户要求刷入：`boot_a` 先备份到 `/data/local/tmp/boot_a_pre_abk448090_20261121.img`（MD5 `bb4f11db2e21960a537d5366356bf847`），系统内 `dd` 写入后按同范围摘要核对。重启后 `uname -r=5.15.225ColorOS`、root 保留、无 panic。

- 用保留目标 `/home/fanziyun/abk448090_target` 与 clang r450784e 重建 combined ko（`out/build/animation_v1_20261121_03/`），159 个 `__versions` 对目标 `Module.symvers` 全部匹配、158 个强导入无缺失；vermagic `5.15.225ColorOS`。insmod 成功且常驻（`init_state=1 init_errno=0 init_stage=5`）。
- 启用 `sched_assist_enabled=1`、`/proc/sys/fbg/frame_boost_enabled=1` 后，真实应用启动/切换期间有 `CMD_ID_BOOST_HIT`(0xc030de02, stage 200) 到达。
- 关键缺口：本 ROM 没有 FrameBoost 用户态服务，system_server 从不打开 `sys_ctrl`（`frame_system_ioctls=0`），因此 DEFAULT/INPUTMETHOD 帧组始终为空，原生 `BOOST_HIT` 全部被 pid/group 门禁静默忽略。`sys_ctrl` 亦被标为通用 `proc` 类型，cameraserver 访问被拒；未放宽全局 SELinux。
- 模块增加受控收编：仅当 `ctrl` 侧的 `BOOST_FRAME_START` 调用者 tgid 等于 `sched_impt_task` 记录的顶层应用 tgid 时，调用 `set_ui_thread(DEFAULT_FRAME_GROUP_ID)` 建立帧组；计数器 `frame_group_adopts`。`sched_impt_task` 由 ROM 正常写入（`top(<pid> <type>)`），不是猜测前台。
- 之后 QUICK 切换中 `frame_group_adopts`、`frame_freq_requests` 与 `frame_freq_raised` 都能增长，说明帧组建立和 cpufreq floor 已被真实动画触发。

## 快速切换实测（human 序列）

使用仓库既有 `out/sailboat_sequence` + `out/fast_actions.txt`（uinput 单指点击 QQ/抖音并按上滑回桌面，20 次启动，坐标 228/884 x, 2230 y），SurfaceFlinger timestats（先 `-enable`）统计屏幕 P2P。

| 条件 | P2P 帧数 | 8ms | 16–32ms | ≥33ms |
|---|---|---|---|---|
| FB off（新启动，SA on） | 1503 | 92.1% | 7.7% | 0.3% |
| FB on（另一启动） | 1457 | 93.3% | 6.4% | 0.3% |
| FB on（同启动、OFF 之后） | 1452 | 91.5% | 8.1% | 0.3% |

两臂顺序与热状态不完全对称，样本小；提频链路已证实被触发，但快速切换的屏幕 P2P 差异落在噪声内，**不宣称流畅度收益**。`frame_freq_raised` 只在 governor 目标低于帧组 floor 时增加，温控压低 policy max 时不会增长。

KSU `Sail_UX` 包已更新为匹配本内核的 `target_release/notes_sha256/ko_sha256/source_id`，下次重启由 `uxctl.sh load` 单次尝试加载。

## 默认开启与启动提频（2026-11-21）

按用户要求改为默认开启：`sched_assist_enabled` 与 `/proc/sys/fbg/frame_boost_enabled` 在模块 init 成功发布后即为 1；运行期仍不能热关。顶层应用变化（`sched_impt_task` 写 `fg`）时，模块在 300ms 窗口内对 capacity ≥800 的 policy 直接 floor 到该 policy 当前 max，并触发各在线 CPU 的频率更新；计数器 `frame_launch_boosts`。该窗口只由 ROM 的顶层应用切换触发，不是定时提频。

- `out/build/animation_v1_20261121_05/`：159 个版本 CRC 全匹配，vermagic `5.15.225ColorOS`，sha256 `e01aa817…`。
- 刷入 `Sail_UX` 包后重启，`uxctl.sh load` 自动加载；未手工 insmod 时读到 `enabled=1`、`frame_boost_enabled=1`，说明默认开启生效。

## 内建内核部署（2026-11-21）

ABK run `44832020536`（builtin 形态，参数取自 run `44683587474`）构建的 `5.15.225-Sailboat-ColorOS-20261121` 已刷入 `boot_a`：写入前备份 `/data/local/tmp/boot_a_pre_20261121_builtin.img`（md5 `bb420dd8…`），写入后前 64 MiB 摘要 `d914424f…` 与本地镜像一致。

- Image 内 `CONFIG_OPLUS_SCHED_ASSIST=y`，含 `oplus_frame_boost`、`sailboat_coloros_animation`、`sailboat_coloros_fbg_launch_boost` 字符串；启动日志 `sailboat_coloros_animation_v1: ready, default enabled`。
- 重启后 `uname -r=5.15.225-Sailboat-ColorOS-20261121`，KernelSU 3.3.1 root 保留（`uid=0`、`u:r:ksu:s0`）；无 `sailboat_sched_assist` 模块，`sched_assist_enabled=1`、`frame_boost_enabled=1`，`frame_group_adopts=1`、`frame_launch_boosts=2`。
- 本轮未做流畅度 A/B；此前 human 快速切换的 P2P 差异仍在噪声内，未宣称收益。
- KSU `Sail_UX` 包保留在设备上不会覆盖内建实现：`uxctl.sh load` 检测到已有 sched_assist 会拒绝加载。
