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
| `service.sh` | 加载 `.ko`；等 `sys.boot_completed=1` 后套用默认档位 |
| `boost.sh` | 档位核心：`on <档>`、`off`、`test [ms]`、`status` |
| `tunables.conf` | 档位参数（频率、时长、手动测试参数） |
| `action.sh` | KSU 动作入口：`status`、`load`、`boost-on`、`boost-off`、`boost-test` |
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

各簇可用频率（实测）：

- CPU0：307200 … 2016000
- CPU4：499200 … 2707200
- CPU7：595200 … 2956800

## 四、用法

```bash
adb shell su -c 'sh /data/adb/modules/sailboat_oppo_perf/action.sh status'
adb shell su -c 'sh /data/adb/modules/sailboat_oppo_perf/action.sh boost-on aggressive'
adb shell su -c 'sh /data/adb/modules/sailboat_oppo_perf/action.sh boost-off'
adb shell su -c 'sh /data/adb/modules/sailboat_oppo_perf/action.sh boost-test 200'
```

- `boost-test` 直接抬 `scaling_min_freq` 指定毫秒再恢复，用来在不触摸的情况下确认档位参数可用。
- 默认档位由 `tunables.conf` 的 `PROFILE_DEFAULT` 决定，开机自动套用。

## 五、验证记录（2026-11-16，真机 5.15.223-Sailboat）

| 项 | 结果 |
|---|---|
| 档位写入 | `boost.sh on standard` 后 `input_boost_freq=1459200 1459200 1459200 1459200 1785600 1785600 1785600 2092800`、`input_boost_ms=100`、`sched_boost_on_input=1` |
| 状态回读 | `boost.sh status` 输出档位、四组参数与三簇 `scaling_min_freq` |
| 点击触发内核提频 | 用 `/dev/uinput` 注入一次真实触摸（[`tools/uinput_tap`](../../tools/uinput_tap/uinput_tap.c)），kprobe 打在 `sched_walt` 的 `do_input_boost` 上，**命中 1 次**，说明触摸确实走到内核提频路径 |
| 频率窗口 | 同一次触摸前后 1.2 秒采样（20ms 间隔）：cpu4 `614400 → 2803200`、cpu7 `864000 → 3187200`；cpu0 采样时已处于 2016000，无法区分档位贡献 |

注意：频率数字受当时系统负载干扰，只作旁证；**机制性证据是 `do_input_boost` 的 kprobe 命中**。

## 六、安全与回滚

- 关闭提频：`action.sh boost-off`（只写 `sched_boost_on_input=0`，不改其它状态）。
- 停用整个模块：`touch /data/adb/modules/sailboat_oppo_perf/disable` 后重启。
- 提频只抬频率下限，不改 governor、不改 `scaling_max_freq`、不碰 GPU。
- 省电影响未测。需要时用 `docs/plan.md` 里的测量口径做交替 A/B。

## 七、未完成

- [ ] 三档的耗电与流畅度 A/B（`presentToPresent ≥ 33ms` 占比、冷启动时长）。
- [ ] `input_boost_freq` 的 8 个值是否真的按 CPU 生效、金核与超大核是否每次触摸都抬到位（当前只有近似读数）。
- [ ] 场景联动（`sched_boost`、`sched_low_latency`、`sched_pipeline`）尚未接进档位。
- [ ] 与 `sched_assist` 的 ux 路径联动：目前两套互不知情，都打开时可能有叠加。
