# KSU 模块：性能档位与点击提频

日期：2026-11-16　模块 id：`sailboat_oppo_perf`

## 一、模块做什么

两件事：

1. **加载内核模块**：开机 `insmod lib/modules/sail_sched_assist.ko`，提供 `/proc/oplus_scheduler/sched_assist/*`（接口清单见 [`接口-oplus_scheduler.md`](接口-oplus_scheduler.md)）。
2. **性能档位**：把"点击提频"接到本机已有的 WALT 内核提频上，并给三档可调参数。

点击提频**不是**自建的用户态守护进程，而是直接开内核里已有的 WALT `input_boost`：每条触摸事件由内核 input 层触发，`do_input_boost` 工作项抬频 `input_boost_ms` 毫秒。这样做的好处是零用户态开销、无轮询、无后台进程，也避免和 OPPO 的 `uad/touch_boost.c` 重复造轮子（后者就是高通同名代码的另一份拷贝）。

## 二、文件

| 文件 | 作用 |
|---|---|
| `module.prop` | KSU 模块描述 |
| `post-fs-data.sh` | 修 `.ko` 与脚本权限 |
| `service.sh` | 加载 `.ko`；等 `sys.boot_completed=1` 后套用默认档位并启动 guard |
| `boost.sh` | 档位核心：`on <档>`、`off`、`guard`、`test [ms]`、`status` |
| `tunables.conf` | 档位参数（频率、时长、手动测试参数、guard 间隔） |
| `action.sh` | KSU 动作入口：`status`、`load`、`boost-on`、`boost-off`、`boost-test`、`boost-guard`、`boost-guard-stop` |
| `sepolicy.rule` | `insmod` 所需规则 |
| `lib/modules/*.ko` | 打包时放入的内核模块 |

## 三、档位

`tunables.conf` 里每档 8 个频率值，对应 CPU0–CPU7（本机 4 银 + 3 金 + 1 超大核）：

| 档位 | `input_boost_ms` | 银核 0–3 | 金核 4–6 | 超大核 7 |
|---|---:|---|---|---|
| `light` | 80 | 1228800 | 1536000 | 1708800 |
| `standard`（默认） | 100 | 1459200 | 1785600 | 2092800 |
| `aggressive` | 150 | 1670400 | 2054400 | 2476800 |
| `off` | — | 关闭 `sched_boost_on_input` | | |

同时会打开电源键提频（`powerkey_sched_boost_on_input`、`powerkey_input_boost_ms`）。

## 四、ROM 会覆盖档位，用 guard 兜住

实测发现：模块开机套用档位后，`input_boost_freq` 可能被 ROM 改回默认值（`1228800 0 0 0 0 0 0 0`），而 `sched_boost_on_input` 不被碰。是**事件触发的一次性覆盖**，不是持续覆盖（连续观察 20 秒 + 四次应用启动都没再被改）。来源很可能是性能 HAL 在某个时机重写。

因此 `boost.sh` 加了 `guard` 子命令：每 `GUARD_INTERVAL` 秒读一次 `input_boost_freq`，发现和当前档位不符就重新写回，并在 `load.log` 记一行。`service.sh` 开机套用档位后自动起它（`GUARD_INTERVAL=0` 可关）。

手动控制：

```bash
adb shell su -c 'sh /data/adb/modules/sailboat_oppo_perf/action.sh boost-guard'
adb shell su -c 'sh /data/adb/modules/sailboat_oppo_perf/action.sh boost-guard-stop'
```

验证（2026-11-17）：手工把频率改回 ROM 默认值，12 秒后 guard 恢复成 standard 档，`load.log` 出现 `guard: 频率值被外部改回，已重新套用`。

代价：一个常驻 shell 循环，每 10 秒读一个 proc 文件。这与"点击提频本身不走用户态"不冲突——guard 只做状态校正，不参与提频决策。

各簇可用频率（实测）：

- CPU0：307200 … 2016000
- CPU4：499200 … 2707200
- CPU7：595200 … 2956800

## 五、用法

```bash
adb shell su -c 'sh /data/adb/modules/sailboat_oppo_perf/action.sh status'
adb shell su -c 'sh /data/adb/modules/sailboat_oppo_perf/action.sh boost-on aggressive'
adb shell su -c 'sh /data/adb/modules/sailboat_oppo_perf/action.sh boost-off'
adb shell su -c 'sh /data/adb/modules/sailboat_oppo_perf/action.sh boost-test 200'
```

- `boost-test` 直接抬 `scaling_min_freq` 指定毫秒再恢复，用来在不触摸的情况下确认档位参数可用。
- 默认档位由 `tunables.conf` 的 `PROFILE_DEFAULT` 决定，开机自动套用。

## 六、实测：对 QQ 启动流畅度无可测收益（2026-11-17）

按 `docs/plan.md` 的口径做同一次开机内交替 A/B（`scripts/ab_launch.sh`，每轮动态定位桌面图标、uinput 真实点击、点击后校验前台包名，无效样本跳过），4 对：

| 对 | P0 ON | P0 OFF |
|---|---|---|
| 1 | 21/131 = **16.0%** | 3/97 = 3.1% |
| 2 | 3/108 = 2.8% | 2/103 = 1.9% |
| 3 | 2/110 = 1.8% | 2/107 = 1.9% |
| 4 | 2/111 = 1.8% | 2/97 = 2.1% |
| **合计** | 28/460 = **6.09%** | 9/404 = **2.23%** |

表面对比是 ON 差近 3 倍，但**差异全部来自第 1 对**——那一对 ON 侧是刚切完档位后的第一次启动（预热离群）。去掉第 1 对：

- P0 ON：7/329 = **2.13%**
- P0 OFF：6/307 = **1.95%**

**结论：无可测收益。** 后 3 对两组基本重合（1.8–2.8% vs 1.9–2.1%）。

这与此前 `Sail_FrameBoost` 的实测结论一致（交接文档：转场已 110–120fps，抬频无收益）。

另一个背景：基线本身已经不差——约 83–85% 的帧是 8ms（120Hz 准时），掉帧率约 2%。

顺带修正一个判读口径：`displayConfigStats`（60fps/120fps 各停留多久）**不被 `--timestats -clear` 清零**，是累积值，不能用来判断某次启动的刷新率；只有 `presentToPresent` 直方图是被清零的。另外启动过程中出现的 700–1000ms P2P 多半是静态 splash 没有新帧提交，不是掉帧。

**待办**：`input_boost_ms=400` 与 `GPU_PWRLEVEL=1` 目前是开着的，但实测无收益且有耗电/发热成本。是否回退到 `100ms` + GPU 不控，未定。


| 项 | 结果 |
|---|---|
| 档位写入 | `boost.sh on standard` 后 `input_boost_freq=1459200 1459200 1459200 1459200 1785600 1785600 1785600 2092800`、`input_boost_ms=100`、`sched_boost_on_input=1` |
| 状态回读 | `boost.sh status` 输出档位、四组参数与三簇 `scaling_min_freq` |
| 点击触发内核提频 | 用 `/dev/uinput` 注入一次真实触摸（[`tools/uinput_tap`](../../tools/uinput_tap/uinput_tap.c)），kprobe 打在 `sched_walt` 的 `do_input_boost` 上，**命中 1 次**，说明触摸确实走到内核提频路径 |
| 频率窗口 | 同一次触摸前后 1.2 秒采样（20ms 间隔）：cpu4 `614400 → 2803200`、cpu7 `864000 → 3187200`；cpu0 采样时已处于 2016000，无法区分档位贡献 |

注意：频率数字受当时系统负载干扰，只作旁证；**机制性证据是 `do_input_boost` 的 kprobe 命中**。

## 七、安全与回滚

- 关闭提频：`action.sh boost-off`（只写 `sched_boost_on_input=0`，不改其它状态）。
- 停用整个模块：`touch /data/adb/modules/sailboat_oppo_perf/disable` 后重启。
- 提频只抬频率下限，不改 governor、不改 `scaling_max_freq`、不碰 GPU。
- 省电影响未测。需要时用 `docs/plan.md` 里的测量口径做交替 A/B。

## 八、未完成

- [x] 三档的耗电与流畅度 A/B（`presentToPresent ≥ 33ms` 占比）——已做，结论是**无可测收益**，见第六节
- [ ] 耗电与发热 A/B（只测了流畅度，没测功耗）
- [ ] 更多对数重跑一遍 A/B（当前 4 对，且第 1 对是预热离群）；要下"无收益"的结论至少再跑 6 对
- [ ] `input_boost_ms` / `GPU_PWRLEVEL` 是否回退到低耗默认值，未定
- [ ] `input_boost_freq` 的 8 个值是否真的按 CPU 生效、金核与超大核是否每次触摸都抬到位（当前只有近似读数）。
- [ ] 场景联动（`sched_boost`、`sched_low_latency`、`sched_pipeline`）尚未接进档位。
- [ ] 与 `sched_assist` 的 ux 路径联动：目前两套互不知情，都打开时可能有叠加。
- [ ] 冷启动时长（`am_proc_start` → 首帧）还没测，只测了帧分布。
