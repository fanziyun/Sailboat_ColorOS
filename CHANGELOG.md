# Changelog

## 0.7.0 — 2026-11-17

- 新增 `scripts/ab_launch.sh`：同一次开机内交替 A/B 测启动帧数据（每轮动态定位桌面图标、
  uinput 真实点击、点击后校验前台包名，无效样本跳过）。
- `measure_launch.sh` 支持坐标模式与标签模式；修掉三处自身 bug：MSYS 路径转换、
  `ts_$LABEL_$i` 被当成变量名、Python 在 Windows 文本模式写 `\r\n` 导致算术报错。
- P0 落地：`input_boost_ms` 100→400/200/600（按档位），新增 GPU 频率下限控制
  （`GPU_PWRLEVEL`，开机记录原值，off 时恢复），guard 同时看守频率与 GPU 档位。
- guard 停止方式从 `pkill -f`（会误杀调用方自己的 shell）改为 pid 文件 + `kill`。
- **实测结论：P0 对 QQ 启动流畅度无可测收益。** 交替 A/B 4 对，去掉预热离群后
  ON 2.13% vs OFF 1.95%。与 Sail_FrameBoost 此前的结论一致。是否回退未定。
- 修正判读口径：`displayConfigStats` 不被 `--timestats -clear` 清零，是累积值，
  不能用来判断单次启动的刷新率；只有 `presentToPresent` 直方图被清零。

## 0.6.0 — 2026-11-17

- 新增 `scripts/selfcheck.sh`：设备侧自检，覆盖模块加载、节点齐全、档位是否被 ROM 改回、
  guard、KSU 模块文件、HAL 对接（含一次 ux_task 安全读写）、环境。真机跑通，当前 0 FAIL。
- `boost.sh` 加 `guard` 子命令：发现 `input_boost_freq` 被外部改回就重新套用档位并记日志。
  起因是实测档位会被 ROM 事件触发式覆盖。`tunables.conf` 加 `GUARD_INTERVAL`，0 可关。
- `service.sh` 修 `load.log` 无限堆积：超过 40 行转 `.1` 再重建，每次开机只留本次记录。
- 新增 `docs/待办-sched_assist启用.md`：记录 `enabled=1` 导致内核 panic 的根因
  （`set_next_entity` 与 ABK 回移的 EEVDF 不兼容）、处理步骤与不要重复踩的坑。
  按用户决定先关着，不处理。

## 0.5.0 — 2026-11-16

- KSU 模块新增性能档位与点击提频：`boost.sh` + `tunables.conf`，三档 `light/standard/aggressive`，走 WALT 内核 `input_boost`，不另建用户态守护进程。
- `action.sh` 增加 `boost-on` / `boost-off` / `boost-test`，保留原有 `fb-start` / `fb-stop`；`service.sh` 开机等 `sys.boot_completed` 后再套用默认档位。
- 新增 `tools/uinput_tap`：`/dev/uinput` 真实触摸注入的最小实现，配 NDK 构建脚本。
- 真机验证：kprobe 打在 `sched_walt` 的 `do_input_boost` 上，一次点击命中 1 次，确认走的是内核提频路径。
- 新增 `docs/KSU-性能档位.md`。

## 0.4.0 — 2026-11-15

- 迁入用户态优化组件 `components/Sail_FrameBoost`（源码、工具、KSU 模块配置）；按用户决定：优化类组件即使实测无可测收益也迁移保留。
- README 增加「用户态优化组件」一节；`.gitignore` 忽略 `*.stackdump`。

## 0.3.0 — 2026-11-15

- 补齐 ColorOS 性能 HAL 需要的 5 个节点：`audio/enable`、`disable_setting`、`sched_prop`、`tpd_cmds`、`tpd_id`，以及 `/proc/sys/kernel/sched_assist_scene`。
- 新增 `docs/接口-oplus_scheduler.md`：15 个节点与 1 个 sysctl 的路径、权限、格式、语义与实现状态。
- 新增 `port/sched_assist/src/sailboat_coloros_nodes.c`：占位实现，接受写入并回显，保证 HAL 的 open/write 不再失败。
- `scripts/wsl/probe_sched_assist.sh` 改为 `build_sched_assist.sh`，产物更名 `sail_sched_assist.ko`；新增 `scripts/package_ksu.sh`。
- 真机验证：15 个节点齐全、5 个新节点读写正常、sysctl 与 proc 节点共用状态；KSU 模块已安装到 `/data/adb/modules/sailboat_oppo_perf/`。

## 0.2.0 — 2026-11-15

- sched_assist 缺口分析完成（`port/sched_assist/README.md`）：26 个 hook、113 个导入符号、结构体布局全部对齐 5.15.223-Sailboat。
- 新增 `tools/slotprobe`：只读统计 `task_struct`/`rq` 的 `android_oem_data1` 槽位占用；实测 0/1 号槽被小米调度栈占用，`OTS_IDX` 改为 2。
- 新增 `scripts/wsl/probe_sched_assist.sh`：从 `src_upstream/` 复现 sched_assist 探针构建。
- 真机跑通一版 sched_assist：`insmod` rc=0、9 个 proc 节点齐全、`ux_task` 读写生效、无内核告警；`rmmod` 不可用（无 `module_exit`）。

## 0.1.0 — 2026-11-15

- 开荒：仓库骨架、源码清单、移植计划。
- 首期范围：sched_assist、frame_boost + uad、task_cpustats/task_load/task_sched。
- P0 冒烟通过：DDK 5.15.202 的 hello.ko 在 5.15.223-Sailboat 上 insmod/rmmod 正常。
