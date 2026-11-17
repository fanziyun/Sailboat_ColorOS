# 待办：启用 sched_assist（`enabled=1`）

日期：2026-11-17　状态：**未处理，当前 `enabled=0`，先关着**

用户 2026-11-17 决定：这个先放着，后面再处理。本文记录为什么不能直接开、卡在哪、要动什么。

## 一、现状

- 模块已加载，`/proc/oplus_scheduler/sched_assist/` 14 个节点齐全，`ux_task` 读写正常。
- `sched_assist_enabled = 0`，所有热路径在入口短路。
- 后果：ux 状态**能记录但不生效**。已验证 HAL 一次点击写了 16 次 `ux_task`（`p <tid> 132`）、`oplus_set_ux_state_lock` 被调 517 次、launcher 停在 `ux_state=0x06000006 ux_prio=6`——但这些对选核/抢占/迁移没有任何影响。

空转的函数（入口即 `return`）：`oplus_replace_next_task_fair`、`should_ux_task_skip_cpu`、`oplus_check_preempt_wakeup`。
空函数体（未开对应 config）：`enqueue_entity` / `dequeue_entity` / `place_entity` 等 handler。

## 二、为什么不能直接置 1

2026-11-15 置 1 后约 80 秒、一次应用启动时内核 panic：

```
kernel BUG at kernel/sched/walt/walt.c:4488!
pc : android_rvh_dequeue_task+0x40c/0x43c [sched_walt]
Call trace:
 android_rvh_dequeue_task [sched_walt]
 __traceiter_android_rvh_after_dequeue_task
 dequeue_task → __schedule → schedule → do_epoll_wait
（CPU2 PID 8748 system_server）
```

日志来源：`/data/vendor/diag/last_kmsg`（本机 `pstore` 为空，别去翻 pstore）。

内核 cmdline 带 `hung_task_panic=1 hung_task_timeout_secs=30`、`softlockup_panic=1`、`panic_on_rcu_stall=1`——卡住超过 30 秒就自己重启，所以"卡死"和"重启"是同一个事件。

## 三、根因

`sa_fair.c` 的 `android_rvh_replace_next_task_fair_handler` 里这段：

```c
if (simple && true == *repick) {
	for_each_sched_entity((*se)) {
		struct cfs_rq *cfs_rq = cfs_rq_of(*se);
		set_next_entity(cfs_rq, *se);
	}
}
```

- ACK 5.15 的 `pick_next_task_fair`（`fair.c:7671-7686`）：hook 置 `repick` 后内核 `goto done`，**自己不再调 `set_next_entity`**，把责任交给 hook。所以这段是必要的，不是可选的。
- 但本机内核被 ABK 的 `batch15_perf_eevdf.py` 把 `set_next_entity()` 的函数体换成了 EEVDF 版本，里面多了 `cfs_rq->abk_pick_deadline = se->deadline;`，脚本注释明说这个 stash 依赖"从正常 pick 路径调用"。从 hook 里越界调用会破坏这个前提。
- 6.18（一加 16，同样跑 EEVDF）的同一个 handler **整段删掉了**（`sa_fair.c:1015`，连 `se`、`simple` 两个参数都不要了）。

关键点：**6.18 能删是因为它配套的 `fair.c` 也改了**（handler 签名少两个参数就是证据）。handler 和 fair.c 是一对契约，只搬一半不行。而 `fair.c` 只有 in-tree 才能改——外部模块做不到。

## 四、处理步骤

1. **二分定位**：先把 `android_rvh_replace_next_task_fair` 一个 hook 从注册表摘掉，其余 23 个保留，重新编译，置 1 复测。
   - 不再崩 → 锁定就是它，进入第 2 步。
   - 还崩 → 问题在别的 hook（`place_entity`、`check_preempt_tick` 也被 ABK 改过），继续二分。
2. **对照 6.18 逐函数改 `sa_fair.c`**：`replace_next_task_fair`、`place_entity`、`check_preempt_tick`、`pick_next_entity` 在 6.18 里都改过签名或逻辑，不能只改一处。
3. **决定走哪条路**：
   - **兼容**：保留 5.15 结构（自己接管 `set_next_entity`），但绕开 ABK 的 `abk_pick_deadline` stash。前提是能找到一个安全的调用点。
   - **替换**：按 6.18 的契约 handler + `fair.c` 成对替换。干净，但必须 in-tree（转 ABK module_set 之后才能做）。
   - **换机制**：不劫持 pick，改用权重 / `nice` / `uclamp` / 亲和性让 ux 任务占优。代价是语义不完全等价（EEVDF 下权重影响 vruntime 推进和 slice，不是"下一个必须选我"）。
4. 无论哪条路，都要先只读观察一轮再开写入，按 `docs/plan.md` 的口径做交替 A/B。

## 五、不要重复踩的

- 别只改 handler 不改 `fair.c`——那会比现在更糟（任务被选中但 `cfs_rq->curr`/`nr_running` 没更新）。
- 别在没有 panic 日志的情况下猜。抓 `/data/vendor/diag/last_kmsg`。
- 改 `.ko` 必须重启才能生效（模块无 `module_exit`，rvh hook 不可注销）。
- 本机 `CONFIG_SCHED_WALT` 未置位，WALT 是独立模块 `sched_walt`（`[permanent]`），不是内建。

## 六、相关

- `port/sched_assist/README.md`「首次开启开关的真机崩溃」一节有完整 panic 栈。
- `docs/plan.md` P1 阶段。
- `docs/接口-oplus_scheduler.md` 节点清单。
