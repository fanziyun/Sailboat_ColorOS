# frame_boost

目标：把 ROM 的应用/SF 帧事件、帧组记账、选核与调频消费连成可观测链路；仅出现 proc 节点或 `selected` 增长不算完成。

## 来源与实现边界

- 行为/接口主参照：OnePlusOSS SM8850，当前缓存 SHA `4c03af1d6d462b9573a7a9f85b983c6bae49497a`。
- 5.15 实现参照：OnePlusOSS SM8550，SHA `0241347436d48df011e2a6f6fe2d8740c26a379c`，`vendor/oplus/kernel/cpu/sched/frame_boost/frame_boost_V1/`。
- ROM ioctl 参照：上述两树的 `vendor/oplus/kernel/cpu/uad/ua_ioctl/`。
- 不复制 6.18 `.ko`、multi_rtg、sched_ext 或新 WALT 内部结构到 5.15。

## 状态（2026-11-21）

matched module-mode 内核已刷入（`5.15.225ColorOS`），combined ko 通过 CRC/vermagic 核对并 insmod 常驻。真实应用动画能触发 `frame_freq_raised`，但本 ROM 无 FrameBoost 用户态服务、system_server 不发 `sys_ctrl`；帧组由模块的受控收编建立（`frame_group_adopts`，仅限 `sched_impt_task` 顶层 tgid）。human 快速切换两臂屏幕 P2P 差异落在噪声内，未证明流畅度收益。详见 [动画 boost 接入契约](../../docs/动画boost接入契约-vermeer-20261120.md)。

**运行时移植已完成到单模块编译/链接阶段，设备验证进行中。** `sailboat_coloros_animation_v1` 将 sched_assist、FrameBoost V1 与 UAD ioctl 合并为唯一 `sailboat_sched_assist` 模块；目标 hook/导出检查、严格 modpost、模块版本 CRC 与主机运行时测试通过。匹配内核已部署并加载常驻，KSU `Sail_UX` 包已同步为同一 ko。

- `scripts/test_frame_boost.sh`：编译检查两代源码头文件的 LP64 结构布局、16 个 ioctl 编码与公共动画 stage；5.15 的 `CONFIG_OPLUS_FEATURE_MULTI_FBG` 开/关均检查。测试不证明 ROM 动态调用或 32 位 compat。
- `scripts/audit_frame_boost_target.py`：核对最终内核的 Binder/scheduler hook 原型和 `Module.symvers` 中的 tracepoint 导出，并记录文件摘要。
- `scripts/probe_animation_boost.py`：只读获取当前 kernel、governor、节点、性能 HAL/SF/system_server maps/fd；拉取映射中的相关文件到独立目录，保存字符串偏移和 sha256。不写节点、不改属性、不重启服务、不操作桌面。
- `scripts/wsl/probe_frame_boost_build.sh` + `scripts/prepare_frame_boost_probe.py`：检查固定上游提交及适配 SA 头文件摘要后，准备 5.15 实现的编译探针；用公开 `proc_set_user()` 代替 UAD 对私有 proc 结构的 uid/gid 字段写入。只生成 `built-in.a`，不链接 ko、不注入 setup、不打包 KSU；feature 宏只用于本次编译，不改目标 `.config`。运行时生命周期和策略仍未经适配。
- 早期 compile-only 探针产物 `out/build/frame_boost_probe_20261120_02/built-in.a` 仍保留作排查记录；其中发现的四个链接缺口已在 combined-profile 中处理：schedule/migration 回调改由唯一 SA 模块的 native hook/backend 消费，范围外 touch ioctl 初始化依赖已移除（没有空成功 stub）。完整目标构建 `out/build/animation_v1_20261120_02/sailboat_sched_assist.ko` 已经通过 MODPOST/链接；159 个 `__versions` CRC 与匹配目标 `Module.symvers` 全部一致。

## 已确认的契约

LP64：`ofb_ctrl_data=48`、`ofb_stune_data=68`、`ofb_ctrl_cluster=40`、`ofb_key_thread_info=88`、`ofb_frame_util_info=16`、`ua_info_data=24` 字节。公共命令包括 `0xc030de01..09`、`0xc044de0a..0b`、`0xc028df01`、`0xc058df02`、`0xc010df03`、`0xc0186f01`、`0x80186f02`。

**布局相同不代表语义相同**：6.18 新增 `BOOST_RT_QBUF_END=218`、`BOOST_TD_APP_NOTIFY=219`、`501..507` stage；`m_rtg.group_id` 与 5.15 V1 的 `related_depth` 共用偏移，但含义不同。不能把 multi_rtg 的 group ID 当作 V1 related depth。需要用实际 ROM 调用决定兼容层，未支持操作不得伪成功。

保留 ABK UXmodBase 材料的 Binder 与 scheduler hook 原型/导出检查通过。该构建是 `OPLUS_SCHED_ASSIST=m`、`OPLUS_FEATURE_FRAME_BOOST=n`、`CFI_CLANG=y`、`LTO_CLANG_THIN=y`、`TRIM_UNUSED_KSYMS=y`；不能由 GKI 的 `CONFIG_SCHED_WALT=n` 推断设备没有 `sched_walt.ko`。

## 尚未接通的关键部分

1. **唯一 SA provider 与任务数据归属**：combined-profile 使用适配后的 `sa_common.h`（`OTS_IDX=2`），所有 SA/FrameBoost hook 在同一模块内注册，避免双模块争用；但当前 20261118 设备上既有 SA provider 的源码/OTS 布局仍未确认，禁止与未知 provider 并载。
2. **记账/迁移 hook**：combined-profile 已集成 schedule、can_migrate、select_task_rq_fair 以及两个 cpufreq hook，并通过保留目标的 hook 原型/导出检查和完整链接；真机事件触发、回调顺序及数据语义尚未验证。
3. **调频消费**：`fbg_freq_policy_util()` 接入 cpufreq fast-switch/target 路径，并以帧组 util 计算只增不减的频率 floor；target-index 驱动路径同时更新 `policy->cached_resolved_idx`，否则 cpufreq core 会按旧索引执行。匹配 KDIR 编译通过，真机 `frame_freq_raised` 已增长，证明 governor 路径消费了 floor；温控压低 policy max 时不会增长。
4. **ROM ABI/权限**：本 ROM 的 framework/SurfaceFlinger 有相关代码字符串，但没有实际 `sys_ctrl` 到达内核的事件证据；`vendor.boostfwk.version` 为空。当前以 `ctrl` 侧 `BOOST_FRAME_START` + `sched_impt_task` 顶层 tgid 的受控收编建立 DEFAULT 帧组，不把这些收编计作原生 system_server 链路。未改属性绕门槛，未放宽全局 SELinux。
5. **生命周期与回滚**：默认开启（UX 与动画 boost）、初始化失败收口、root-only 写门禁、ioctl gate 和引用路径已实现；restricted hook 无 unregister，因此成功注册后模块刻意常驻且拒绝热关闭，不能以 rmmod 作回滚；停止策略需重启/对应 boot 镜像恢复。
6. **设备验收**：已在 `5.15.225ColorOS` 上加载并验证帧组建立、提频触发与无 panic；human QQ/抖音快速切换 3 组屏幕 P2P 统计差异落在噪声内，未宣称流畅度收益。需继续按 4 对交替 A/B 与转场/首帧口径复测；没有数据前不宣称收益。

当前设备与构建材料的差异、只读取证路径和未完成项见 [动画 boost 接入契约](../../docs/动画boost接入契约-vermeer-20261120.md)。
