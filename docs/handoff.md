# Sailboat_ColorOS 交接文档

日期：2026-11-15　适用对象：接手本项目的会话/开发者

## 一、一页速览

- **项目**：把 ColorOS（OPPO，一加 16）的性能调用内核模块 backport 到 vermeer 的 `android13-5.15`（Sailboat）内核，让 ColorOS 性能 HAL / Osense / UIFirst / AFS 真正落到内核上。
- **仓库**：`H:\Sailboat_ColorOS`（本地 git，未推 GitHub），已有 2 个 commit（开荒、P0 里程碑）。
- **进度**：P0 已通过——DDK 5.15.202 编的 `.ko` 在 5.15.223-Sailboat 真机上 `insmod`/`rmmod` 正常，本地迭代路线成立。OPPO 6.18（sm8850）与 5.15（sm8550）源码已拉到 `src_upstream/`。
- **下一步**：读 sched_assist 源码 → 列符号/hook 缺口 → 编出第一个 .ko → 真机验证节点 → 再做 frame_boost+uad、task_*。

## 二、前情：为什么开这个项目

1. **开应用掉帧调查已收口**：转场实测已 110–120fps（DRM `measured_fps`），`Sail_FrameBoost` 抬频无收益，用户已决定移除；低帧段是应用自身渲染。
2. **桌面加速链逐条核对**：七条链（Osense/UIFirst/AFS/SF transact/绑核/档位/模糊）在源码里真实存在，但本机设备侧全是 no-op——请求能发出，内核后端全缺（`/proc/oplus_scheduler`、`/proc/jank_info`、`/proc/oplus_healthinfo`、`/sys/module/tpd` 全不存在）。
3. **一加 16 原版模块不可直接加载**：vendor_dlkm/vendor_boot 的 OPPO 模块全为 `6.18.21-android17-5-o-maybe-dirty-{4k,16k}` + `modversions`，与 5.15 三重不兼容。
4. 用户拍板：**立项 backport**，把真机 5.15 缺的性能内核模块按 OPPO 源码移植回来。

相关文档（c17 项目）：

- `docs/已知问题.md` — FrameBoost 结论与待办
- `docs/动画加速检测点-源码核对-ColorOS17.md` — 43 条声明核对、15 处文档错误
- `docs/一加16-vendor_dlkm内核模块核对-ColorOS17.md` — 6.18 模块不可移植的证据
- `docs/OPPO-boost框架调查-ColorOS17.md` — 早期 HAL 空转实测

## 三、新项目定义（用户 2026-11-15 拍板）

**源码原则（用户要求写进记忆）**：以 OnePlusOSS 开源源码为主（主参照 6.18 一加 16 的 sm8850 vendor 模块树）；很多私有实现闭源，只能从 `.ko` 反编译补充，反编译只作辅助。

**首期范围**：`sched_assist` 核心、`frame_boost` + `uad`、`task_cpustats` / `task_load` / `task_sched`。`sched_info`（`/proc/jank_info`、`/proc/oplus_healthinfo`）第二阶段再加。

**交付形态**：先 KSU 模块带预编译 `.ko` 试水（`insmod`/`rmmod` 迭代），稳定后转 ABK `module_set` 编进内核构建固化。

**仓库**：`H:\Sailboat_ColorOS` 只建本地 git，暂不推 GitHub。

**与 ABK 的关系**：独立 ABK 外部模块，与 `fanziyun/ABK_5.15_BACKPORT` 一起注入 ABK 构建；对其不满意/需重构的部分尽量在本仓库写覆盖，必要时向上游提兼容 PR。

**验收标准**（三条都要）：
1. 节点齐全 + 性能 HAL 无 open 失败日志、写入生效；
2. SurfaceFlinger timestats `presentToPresent ≥ 33ms` 占比在交替 A/B 中下降；
3. 冷启动（`am_proc_start` → 首帧）/ 转场时长有可测下降。

## 四、仓库现状与用法

```
H:\Sailboat_ColorOS\
  README.md           项目说明、范围、验收、回滚
  AGENTS.md           代码规则：源码原则、KMI、无叙述性注释、sailboat_coloros_* 标记、提交与验证
  LICENSE             GPL-2.0-only（沿用 ABK 系列）
  module.conf         ABK 外部模块描述符（module:repo;after_patch|...;before_build）
  CHANGELOG.md
  .gitignore          out/、*.ko、src_upstream/ 等不入库
  docs/
    plan.md           P0–P5 阶段、验收与风险
    sources.md        OnePlusOSS 源码清单、反编译补充位置
    porting_policy.md 移植策略（范围、形态、禁止半成品、KMI、可观测、可回滚、实测优先）
    handoff.md        本文档副本
  port/
    README.md         特性目录规范（README + src 保留上游路径）
    _shared/README.md
    sched_assist/README.md  首个目标
    frame_boost/README.md
    uad/README.md
    task_sched/README.md    （task_cpustats/task_load 同属这组）
  ksu/
    sailboat_oppo_perf/     KSU 模块骨架：module.prop、service.sh（insmod lib/modules/*.ko）、
                            post-fs-data.sh（chmod 0644 + chcon）、action.sh（status/load）、sepolicy.rule
  scripts/
    fetch_upstream.sh       sparse clone OnePlusOSS 两套源码到 src_upstream/
    build_modules.sh        通用 out-of-tree 构建（KDIR 环境变量）
    wsl/hello_ddk.sh        P0 冒烟脚本（DDK 构建 + PAHOLE=true）
  tools/hello/              hello.c + Makefile（modversions 冒烟）
  src_upstream/             已拉取的 OPPO 源码（gitignored）
```

**构建 .ko（已验证可用）**：

```bash
make -C /home/fanziyun/ddk-android13-5.15-20260313/opt/ddk/kdir/android13-5.15 \
     M=/mnt/h/Sailboat_ColorOS/tools/hello \
     ARCH=arm64 LLVM=1 PAHOLE=true modules
```

（DDK 未带 pahole，`PAHOLE=true` 跳过 BTF；模块运行不需要 BTF。）

**真机加载流程**：

```bash
adb push hello.ko /data/local/tmp/hello.ko
adb shell su -c 'insmod /data/local/tmp/hello.ko; echo rc=$?; dmesg | tail -4'
adb shell su -c 'rmmod hello'
```

## 五、环境事实清单

**设备**：vermeer（SM8550/kalama），一加 16 `PYB110` ColorOS 17 移植（Android 17 / SDK 37）。
- adb 无线：`192.168.31.15:40607`；`su` 可用（uid=0, `u:r:ksu:s0`）。
- KernelSU 3.3.1（uapi 5）；现有模块：`Sail_AodBacklight`、`Sail_AodFeature`、`Sail_HbmBridge`、`Sail_SELinuxCompat`、`Sail_VibFix`、`abk_runtime_tunables`、`extreme_gt`、`magic_mount_rs`、`yukizygisk`、`zygisk_lsposed`、`zygisksu`。

**内核**：`5.15.223-Sailboat-20261105`（ABK 自建，`sub_level=X`、`os_patch_level=lts`、version 标签 `Sailboat`）。
- `CONFIG_MODULES=y`、`MODULE_UNLOAD=y`、`MODVERSIONS=y`、`MODULE_SIG=y`（**无 SIG_FORCE**）、`CFI_CLANG=y`、`LTO_CLANG_THIN=y`、`BPF_SYSCALL=y`、`KPROBES=y`、`UCLAMP_TASK=y`。
- WALT 内建，`/proc/sys/walt/` 58 项（input_boost、sched_boost、sched_user_hint、sched_ed_boost、task_load_boost 等）；governor = `walt`。
- 缺失节点：`/proc/oplus_scheduler`、`/proc/jank_info`、`/proc/oplus_healthinfo`、`/sys/module/tpd`。

**WSL**：Ubuntu-26.04（`/home/fanziyun`，`/` 902G 可用）。已有：`git`、`python3`、`clang`、`gcc`、`make`、`bc`；无 `repo` 工具。DDK 在 `~/ddk-android13-5.15-20260313`（5.15.202，含 kdir/Module.symvers/clang r450784e）。其它：`vermeer/`（boot 镜像工作目录）、`bootwork/`、`xiaomi13-k618-build/`（16G）。sudo 密码 `qwe123`。

**GitHub**：`gh` 已登录 `fanziyun`（id 186413865），token scopes：`gist, read:org, repo, workflow`。

**ABK 构建链**（从 `fanziyun/ABK` 的 `build.yml` 读出）：
- 内核源：AOSP `kernel/manifest` 的 `common-android13-5.15-lts`（repo 工具，depth 1）；也支持自定义 source repo。
- 工具链：`kernel/prebuilts/build-tools` @ `main-kernel-build-2024`。
- 外部模块：`custom_external_modules` 描述符 `module:repo;stage` / `set:repo#child;stage`，阶段只有 `after_patch` / `before_build`；模块脚本收 `KERNEL_ROOT`、`DEFCONFIG`、`CUSTOM_EXTERNAL_MODULE_STAGE` 环境变量（`scripts/libabk.sh` 提供 `abk_require_env`、`abk_enable_config` 等）。
- 参考仓库：`fanziyun/ABK_5.15_BACKPORT`（Python graft registry：`scripts/abk_stable_*.py` 的 `replace_once` 锚点、`files/` 整文件 overlay、`module.conf` 的 module_set 列表、`ksu/` 运行时模块）、`xingguangcuican6666/ABK_KSU_SANDBOX_MODULE`（KSU 模块模板：module.conf/setup.sh/patches/scripts/tests）。

**OPPO 源码（已拉到 `src_upstream/`）**：
- `oplus_sm8850/vendor/oplus/kernel/cpu/sched/{sched_assist,frame_boost,sched_info,task_cpustats,task_load,task_sched,eas_opt,sched_tune}`、`cpu/uad/{touch_boost.c,ua_ioctl/}`、`sched_ext/`、`smart_freq/`、`freq_qos_arbiter/`、`geas/` 等（6.18，行为主参照）。
- `oplus_sm8550/...` 同一批模块的 5.15 形态（5.15.167 一加 11 树）。

**反编译补充位置**（c17 项目）：`out/op16_vdlkm/lib/modules/`（718 个 .ko）、`out/op16_vdlkm_vendor_boot/`（sched-walt、sched_assist、frame_boost、schedinfo 核心模块）、脚本 `out/decode_ramdisk.py`、`out/scan_vdlkm.py`。

## 六、P0 结果（已实测）

- `tools/hello` 用 DDK 5.15.202 编译：vermagic `5.15.202-android13-5.15.202_r00-dirty`、带 `__versions`。
- 在 5.15.223-Sailboat 真机：`insmod rc=0`，`dmesg` 出现 `sailboat_hello: loaded`，`lsmod` 可见，`rmmod` 干净。
- 设备 `CONFIG_MODULE_SIG_FORCE`、`CONFIG_MODULE_FORCE_LOAD` 均未开启 → 跨 sublevel 可加载。
- 注意事项：能载不代表所有符号 CRC 都匹配（目前只验证了 `module_layout`/`printk` 这类）；每引入新符号都要真机加载验证，遇 CRC 不匹配再考虑用 ABK 构建产物对齐 `Module.symvers`。

## 七、下一步（按顺序）

1. **确认源码完整**：`src_upstream/oplus_sm8850`、`oplus_sm8550` 已存在（约 100MB+ 各一份）；如不完整重跑 `bash H:/Sailboat_ColorOS/scripts/fetch_upstream.sh`。
2. **sched_assist 符号缺口分析**：
   - 读 `src_upstream/oplus_sm8850/vendor/oplus/kernel/cpu/sched/sched_assist/*` 与 5.15 版本；
   - 列出它引用的导出符号与 `android_vh_*` 钩子；
   - 在设备上对照 `/proc/kallsyms`（root 可读）确认缺哪些；
   - 结论写进 `port/sched_assist/README.md`（证据 + 文件 + 符号）。
3. **编出第一个 .ko**：先按 5.15 版最小子集适配（proc 节点 + ux_task/im_flag），DDK 构建，真机 insmod，核对 `/proc/oplus_scheduler/sched_assist/*`。
4. **功能核对**：HAL 日志（`/odm/bin/hw/vendor-oplus-hardware-performance-V1-service`）不再报 open 失败；桌面 Osense/UIFirst 写入可见。
5. **后续模块**：frame_boost + uad（注意 ua_ioctl 命令号要和 ROM 侧一致）、task_cpustats/task_load/task_sched。
6. **固化**：把验证过的源码并入 `setup.sh` 两阶段（after_patch 拷源码 + 接 Kbuild；before_build 开 Kconfig/defconfig），与 ABK_5.15_BACKPORT 同批注入。

测量口径：SurfaceFlinger timestats `presentToPresent ≥ 33ms` 占比（同一次开机内交替 A/B，≥4 对）；冷启动 `am_proc_start` → 首帧；转场时长。触摸必须走 uinput 真实点击（见下）。

## 八、风险与硬性注意事项

- `CONFIG_CFI_CLANG=y`：.ko 必须用同一版本 clang（DDK 自带 r450784e）编译，换工具链可能直接加载失败。
- sched 相关模块可能 panic/死机：单模块逐个加载，先验证 `rmmod` 回滚路径；KSU 模块可一键禁用。
- 与厂商 WALT/FAS 冲突：先只读、后写入，每一步做交替 A/B。
- **禁止 `stop; start`**：本机会 init abort 触发内核重启；重启框架只用 `setprop ctl.restart zygote`。
- SELinux 结论前先 `adb shell getenforce`（用户常驻 Permissive，缺规则看不出来）。
- 包内脚本零注释（KSU 模块 zip 也算刷机包），只留 shebang；说明写进 docs。
- 触摸/转场验证必须用 uinput 注入真实点击，`monkey`/`am start` 没有参考性。
- 不改 `framework.jar`；仓库只本地 git，暂不推 GitHub。

## 九、相关记忆

记忆目录：`C:\Users\Administrator\.claude\projects\C--Users-Administrator-Desktop-c17\memory\`；项目副本在 c17 `docs/记忆/`。

### 与本项目直接相关（建议全文读）

| 记忆文件 | 内容 |
|---|---|
| `sailboat_coloros_project.md` | 本项目路径、决策、环境、P0 结果（本文档的浓缩版） |
| `op16_kernel_modules_incompatible.md` | 一加 16 模块为何不能直接加载：vermagic/modversions/内置核心 |
| `launcher_anim_doc_verified.md` | 动画加速检测点文档核对：15 处错误与结论 |
| `frameboost_measured_fps_verdict.md` | 转场已 110–120fps、抬频无收益、measured_fps 是决定性指标 |
| `abk_custom_kernel_dispatch.md` | ABK 构建 kernel-custom.yml 的完整参数配方（版本串、external modules 写法） |
| `coloros_port_xiaomi_fixes.md` | ColorOS→小米移植的五类修复手法（背景知识） |

### 操作规范类（做设备验证时必须遵守）

| 记忆文件 | 内容 |
|---|---|
| `feedback_no_stop_start.md` | 禁止 stop;start，只能 ctl.restart zygote |
| `uinput_evdev_injection.md` | uinput 虚拟触摸屏注入 evdev 事件的方法 |
| `feedback_simulate_real_tap.md` | 转场验证必须模拟真实点击 |
| `feedback_verify_changes.md` | 改完二进制必须用目标平台校验器验（dex2oat 等） |
| `selinux_enforcing_port_method.md` | 补 SELinux 一次给全 + 设备 secilc 编译校验 |
| `feedback_check_getenforce_before_selinux_verify.md` | 任何 SELinux 结论前先 getenforce |
| `feedback_code_layout.md` | 实现代码放 `code/<Sail组件>/` + README |
| `feedback_sail_component_names.md` | 组件统一 `Sail_*` 命名 |
| `feedback_script_no_comments.md` | 刷机包内脚本零注释 |

## 十、文件索引

- 新仓库：`H:\Sailboat_ColorOS`（README、AGENTS、docs/、port/、ksu/、scripts/、tools/）
- c17 项目：`docs/已知问题.md`、`docs/动画加速检测点-源码核对-ColorOS17.md`、`docs/一加16-vendor_dlkm内核模块核对-ColorOS17.md`、`docs/OPPO-boost框架调查-ColorOS17.md`
- 反编译/解包产物：c17 `out/op16_vdlkm/`、`out/op16_vdlkm_vendor_boot/`、`out/wf_launcher_verify.txt`
- 本项目文档：`H:\Sailboat_ColorOS\docs\{plan,sources,porting_policy,handoff}.md`
