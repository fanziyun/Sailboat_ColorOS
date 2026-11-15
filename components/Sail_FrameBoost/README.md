# Sail_FrameBoost：开应用转场 GPU 抬频

> 迁移自 c17 `code/Sail_FrameBoost`（2026-11-15）。按用户决定：优化类组件即使交替 A/B 没测出可测收益也保留并迁移，不以"无可测收益"为去留依据。本组件为**用户态守护进程**（不依赖内核模块），在 5.15.223-Sailboat 上可直接使用。

适用：一加 16 `PYB110` ColorOS 17（`PYB110_17.0.1.201(CN01)`）移植到 Redmi K60 Ultra（vermeer，SM8550/kalama），vendor 来自 Redmi HyperOS 13。

## 作用

从桌面点图标打开 app 时，ColorOS 的转场动画由 SystemUI 渲染，单帧 p50 12.89ms，而 120Hz 的预算是 8.33ms。成因是 GPU 时钟：kgsl 的 `min_pwrlevel` 出厂默认 7（220MHz，最低档），`default_pwrlevel` 也是 7，`idle_timer` 80ms。真实点击采样显示 GPU 要等约 350ms 才升到 348MHz，随后 `gpu_load` 归零又立刻回落，全程到不了 550/680MHz。

把地板钉到 0（680MHz）后 SystemUI 单帧降到 6.47ms，低于 8.33ms 预算。本守护进程在**触摸按下**时把 `min_pwrlevel` 抬到 0；若这次触摸随后真的发起应用启动（事件流里出现非桌面 activity start），保持自动延长到目标应用首个 resume + 800ms（硬上限 8 秒），保证从桌面打开应用的整个转场动画都在 680MHz 下渲染；普通点击 1.5 秒后还原。只在桌面时触发，桌面状态只认桌面自身的 resume/pause 事件，其他应用的后台 resume 不再影响判断（早期版本用"最近一条 resume"判断前台，转场期间会被后台 resume 污染导致漏抬频）。

实测（`dumpsys gfxinfo com.android.systemui` 的 Janky frames，每档 5 次真实点击）：

| 条件 | jank 率 |
|---|---|
| 基线（不抬频） | 7.6% |
| 点击后 150ms 才抬频 | 6.0% |
| **点击前抬 680MHz，保持 1.5s** | **3.1%** |
| 静态 680MHz | 2.9% |

守护进程实机 A/B（交替 5+5 次，亮度 150，uiautomator 每轮校准图标坐标）：**基线 8.9% → 守护在场 1.8%**，守护臂每轮恰好 1 帧 jank，10 次点击全部真实拉起应用。

触发必须早于转场动画第一帧。activity 事件（`wm_on_start_called` / `wm_on_resume_called`）都晚于动画起点，只能到 6.0%；唯一更早的信号是触摸按下。

### 2026-11-15 复测结论（v2，真实点击注入）

v2 已用 uinput 虚拟触摸屏做真实点击（完整触摸链路，冷启动各 3 次 A/B），并把指标换成 DRM `/sys/class/drm/sde-crtc-0/measured_fps`（0.5s 窗口实际出帧率）。结果推翻上面的 jank 收益：

- 默认地板 7（220MHz）下转场段本身就是 110–120fps；钉到 0（680MHz）无变化。GPU 不是转场瓶颈。
- 启动期间 walt 已把三簇顶满（policy0/3/7 多数采样在 max），CPU 钉频无额外收益。
- OPPO 刷新率选择器启动期间锁 120Hz（`ConfigId:1`、`[120 120]`）。
- 20–80fps 爬升段与 ~59fps 平台是抖音启动渲染与 60fps 视频内容；应用商城 A/B 同样（转场 110–119、启动段 80–90fps 平台，两臂分布重合）。系统侧不可消除。
- 上表的 gfxinfo jank 是 SystemUI 自绘帧相对 deadline 的统计，与转场实际出帧率不相关，不能作为收益证据。

守护进程行为本身正常（v2 前台判断修复有效），但按现设计无用户可感知收益。去留见 `docs/已知问题.md` FrameBoost 条目。

## 文件

| 文件 | 作用 |
|---|---|
| `src/main.c` | 守护进程：evdev 触摸事件 + logcat 前台包名 → 写 `min_pwrlevel` |
| `src/start.S` | PIE `_start`，转 `Sail_FrameBoost_start` |
| `src/android_abi.h` | 无 libc 头文件的 ABI 声明（含 24 字节 `input_event` 断言） |
| `tools/build.py` | clang 交叉编译 + `llvm-readelf` 结构断言，产出 `frameboost/dist` |
| `tools/stage.py` | 把二进制、rc、CIL 铺进 `system_ext` 工作树，供打包 |
| `tools/uinput_tap.c`、`tools/uinput_tap_start.S` | 开发验证工具：创建 `/dev/uinput` 虚拟触摸设备（名 `goodix_test`），读 FIFO 注入点击/滑动；**不打包** |
| `tools/build_tap.py` | 编译 `uinput_tap` |
| `tools/pwr_trace.sh`、`tools/resume_trace.sh` | 验证观测（推到设备 `/data/local/tmp/` 运行）：前者每 0.2s 轮询 `min_pwrlevel`、只在变化时记一行到 `pwr_trace.log`，精确捕捉每次抬频窗口；后者持续记录 `wm_on_resume_called` 前台事件到 `resume_trace.log` |
| `tools/fps_trace.sh` | 验证观测：每 0.25s 记录 `/proc/uptime` + DRM `measured_fps` 到 `/data/local/tmp/fps_trace.log`；转场平滑度的决定性指标（SF 实际出帧率），优于 gfxinfo jank |
| `tools/sel_trace.sh` | 验证观测：每 ~70ms 记录 `dumpsys SurfaceFlinger` 的 `ConfigId`/`Current Policy` 到 `sel_trace.log`，看 OPPO 刷新率选择器状态 |
| `tools/cpu_trace.sh` | 验证观测：每 0.1s 记录 policy0/3/7 的 `scaling_cur_freq` 到 `cpu_trace.log` |
| `config/Sail_FrameBoost.rc` | init 服务定义（`/system_ext/bin`，root，`restart_period 5`） |
| `config/system_ext_sepolicy.cil.part` | 域/执行类型与权限，编进 `system_ext` 的 `plat_sepolicy.cil` |
| `module/module.prop`、`module/service.sh`、`module/watch.sh` | KernelSU 模块（临时部署用）：`service.sh` 守护循环 + `watch.sh` SystemUI jank 观测器（日志 `/data/local/tmp/frameboost_watch.log`） |

产物（不入本目录）：`frameboost/dist/Sail_FrameBoost`。

## 关键接口与值域

### 写入节点

`/sys/class/kgsl/kgsl-3d0/min_pwrlevel`，SELinux 类型 `vendor_sysfs_kgsl`，`0644 root:root`。

| 值 | 频率 |
|---|---|
| 0 | 680 MHz |
| 1 | 615 MHz |
| 2 | 550 MHz |
| 3 | 475 MHz |
| 4 | 401 MHz |
| 5 | 348 MHz |
| 6 | 295 MHz |
| 7 | 220 MHz（出厂默认） |

守护进程启动时读一次当前值，抬频结束后写回该值，不硬编码 7。

### 读取节点

| 节点 | 用途 |
|---|---|
| `/dev/input/eventN`（`EVIOCGNAME` = `goodix_ts`） | 触摸按下。扫描 `event0`..`event31` 按名字匹配，不硬编码编号 |
| `/system/bin/logcat -b events -s wm_on_resume_called:I wm_on_paused_called:I wm_on_start_called:I` | 前台状态机：桌面 resume→在桌面；桌面 pause→离开桌面；非桌面 start→启动已发起（开启保持延长）；目标应用 resume→转场接近结束。启动时先查一次 `dumpsys window displays` 的 `mFocusedApp` 确定初始状态 |

触摸判定：`EV_ABS` + `ABS_MT_TRACKING_ID`(57) 值 ≥ 0，或 `EV_KEY` + `BTN_TOUCH`(330) 值 = 1。

### 参数

| 宏 | 默认 | 含义 |
|---|---|---|
| `FB_HOLD_MS` | 1500 | 普通点击的抬频保持时长（`--hold-ms`） |
| `FB_LAUNCH_GRACE_MS` | 800 | 检测到应用启动后，目标应用 resume 之后继续保持的时长 |
| `FB_HOLD_CAP_MS` | 8000 | 单次抬频硬上限，启动卡死时防止长期占频 |
| `FB_TICK_MS` | 50 | 主循环 tick |
| `FB_FLOOR_HIGH` | `"0\n"` | 抬频时写入的档位 |
| `FB_TOUCH_PREFIX` | `"goodix_ts"` | 触摸设备名前缀（`--touch-name`），验证时用 `goodix_test` 指向虚拟设备 |
| `FB_NO_FILTER` | 未定义 | 定义后不做桌面过滤，每次触摸都抬频（功耗对照用） |
| `FB_DEBUG` | 未定义 | 每次抬频/还原打一条日志 |

## 重建

```sh
python code/Sail_FrameBoost/tools/build.py
```

可选 `--debug` / `--no-filter` / `--hold-ms N`。需要 NDK r30 的 clang（`--target=aarch64-linux-android34`）。

固化进 `system_ext`：

```sh
python code/Sail_FrameBoost/tools/stage.py --tree <system_ext 树> \
  --precompiled-dir <system_ext 树>/system/etc/selinux
```

CIL 变更后必须删掉 `precompiled_sepolicy*`，否则 init 会继续用不含新域的预编译策略。

## 临时验证（KernelSU）

1. 把 `frameboost/dist/Sail_FrameBoost` 放到 `/data/adb/modules/Sail_FrameBoost/bin/`，`chmod 755`。
2. 把 `module/module.prop`、`module/service.sh`、`module/watch.sh` 放到模块根目录（`service.sh`、`watch.sh` 需 755）。
3. 重启，确认 `logcat -s SailFrameBoost` 有 `started`。
4. 真实点击桌面图标打开 app，观察 `cat /sys/class/kgsl/kgsl-3d0/min_pwrlevel` 是否在点击瞬间变 0、1.5 秒后回 7。
5. 用 `dumpsys gfxinfo com.android.systemui reset` + 5 次点击复测 jank 率。

`service.sh` 在开机 late_start 执行：pkill 旧实例 → sleep 8 → 后台起 `watch.sh` → 死循环跑守护进程（退出则写回 `min_pwrlevel=7` 后 2 秒重启）。`watch.sh` 每 20 秒采样 `dumpsys gfxinfo com.android.systemui`，窗口内帧增量 ≥5 或 jank 增量 ≥1 时向 `/data/local/tmp/frameboost_watch.log` 追加一行（时间、jank 增量、帧增量、当时 `min_pwrlevel`、前台包名），日志超 1MB 截半。

**软重启不会重跑模块脚本**：`setprop ctl.restart zygote` 只重启 zygote/system_server/SystemUI，KSU 的 `service.sh` 只在真实开机阶段执行。软重启后要手动 `sh /data/adb/modules/Sail_FrameBoost/service.sh &` 拉起；模块自动启动要等下一次真实开机。

**回退**：`touch /data/adb/modules/Sail_FrameBoost/disable` 后重启（保留文件），或直接删除模块目录后重启。

**程序化验证的坑**：`input tap` 走 InputDispatcher 注入，**不经过 evdev**，守护进程看不到；`sendevent` 写 event 节点无效（evdev 无 write 处理）。要程序化验证触发，用 `tools/uinput_tap`（`python tools/build_tap.py` 编译）建虚拟触摸设备，守护进程用 `--touch-name goodix_test` 编译测试版指向它；FIFO 注入格式：`echo "x y" > /data/local/tmp/tapfifo` 点一次，`echo "x1 y1 x2 y2 [ms]" > /data/local/tmp/tapfifo` 滑动（缺省 300ms，逐 8ms 线性插值）；事件经 InputReader 完整链路真实拉起应用，守护进程同时可见。坐标换算：帧缓冲像素 × 100（本机 1440x3200，设备轴上限 143999/319999）。虚拟设备已建时守护进程必须后启动。FIFO 读端必须同时持一个常开写 fd（`O_WRONLY|O_NONBLOCK`），否则无写者时 poll 返回 POLLHUP 空转吃满一个核。

**注意**：本机当前 `getenforce` 为 `Permissive`。Permissive 下 SELinux 不拦截，功能能跑不代表策略正确；enforcing 验证必须单独做，并用设备端 `secilc` 编译校验 CIL。

## 未完成

- [x] 2026-11-15 用户决定移除：设备上的 KSU 模块已删除（`/data/adb/modules/Sail_FrameBoost`），不再随开机启动；源码、产物与验证工具保留在本目录。
- [x] 实机验证守护进程版本 jank 率（交替 A/B：基线 8.9% → 守护在场 1.8%，见上表；但 2026-11-15 复测表明该指标与转场实际出帧率不相关）
- [ ] 固化进 system_ext 镜像（已随移除决定搁置）
- [ ] enforcing 模式下验证 SELinux 策略（当前设备是 Permissive）
- [ ] `--no-filter` 与默认过滤的功耗对比，确认滚动/游戏场景的代价
- [ ] 应用内跳转、通知栏点击、最近任务拉起不在覆盖范围
- [ ] 返回桌面（home 手势）、返回手势、最近任务等其它转场未覆盖，只覆盖从桌面打开应用
- [ ] 前台包名缓存依赖 logcat 子进程；若被 SELinux 拒绝需要补 `logdr_socket` / `logcat` 域规则
