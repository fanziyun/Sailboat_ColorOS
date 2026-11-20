// SPDX-License-Identifier: GPL-2.0-only
#include <linux/atomic.h>
#include <linux/cred.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/proc_fs.h>
#include <linux/sched/signal.h>
#include <linux/seq_file.h>
#include <kernel/sched/sched.h>
#include "sa_common.h"
#include "sa_priority.h"
#include "sailboat_coloros_nodes.h"
#include "sailboat_coloros_ux.h"

#if IS_ENABLED(CONFIG_OPLUS_SYSTEM_KERNEL_QCOM) || IS_ENABLED(CONFIG_SCHED_WALT)
#error "sailboat_coloros_ux_min requires the audited non-Oplus-WALT hook profile"
#endif

static DEFINE_PER_CPU(spinlock_t, sailboat_coloros_ux_locks);
static DEFINE_MUTEX(sailboat_coloros_ux_control);
static bool sailboat_coloros_ux_ready;
static atomic64_t sailboat_coloros_ux_counters[SAILBOAT_UX_COUNTERS];
static const char * const sailboat_coloros_ux_counter_names[] = {
	"enqueue", "dequeue", "selected", "runtime", "expired", "rejected",
	"hierarchy_skipped", "set", "alloc_failed",
};

void sailboat_coloros_ux_note(enum sailboat_coloros_ux_counter counter)
{
	atomic64_inc(&sailboat_coloros_ux_counters[counter]);
}

spinlock_t *sailboat_coloros_ux_rq_lock(int cpu)
{
	return &per_cpu(sailboat_coloros_ux_locks, cpu);
}

int sailboat_coloros_ux_prepare(void)
{
	struct task_struct *group, *task;
	int cpu, ret = 0;

	for_each_possible_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);

		if (!rq || !rq->idle)
			return -ENODEV;
		if (memchr_inv(rq->android_oem_data1, 0, sizeof(struct oplus_rq)) ||
		    READ_ONCE(rq->idle->android_oem_data1[OTS_IDX]))
			return -EBUSY;
	}
	read_lock(&tasklist_lock);
	for_each_process_thread(group, task) {
		if (READ_ONCE(task->android_oem_data1[OTS_IDX])) {
			ret = -EBUSY;
			break;
		}
	}
	read_unlock(&tasklist_lock);
	return ret;
}

int sailboat_coloros_ux_publish_ready(void)
{
	if (atomic64_read(&sailboat_coloros_ux_counters[SAILBOAT_UX_ALLOC_FAILED]))
		return -ENOMEM;
	smp_store_release(&sailboat_coloros_ux_ready, true);
	return 0;
}

int sailboat_coloros_ux_set_enabled(int value)
{
	int ret;

	if (!uid_eq(current_euid(), GLOBAL_ROOT_UID))
		return -EPERM;
	mutex_lock(&sailboat_coloros_ux_control);
	ret = sailboat_coloros_ux_transition(
		smp_load_acquire(&sailboat_coloros_ux_ready),
		READ_ONCE(global_sched_assist_enabled), value);
	if (!ret && value && !READ_ONCE(global_sched_assist_enabled) &&
	    atomic64_read(&sailboat_coloros_ux_counters[SAILBOAT_UX_ALLOC_FAILED]))
		ret = -ENOMEM;
	if (!ret)
		smp_store_release(&global_sched_assist_enabled, value);
	mutex_unlock(&sailboat_coloros_ux_control);
	return ret;
}

int sailboat_coloros_ux_write_gate(bool system_only)
{
	bool system = uid_eq(current_euid(), GLOBAL_ROOT_UID) ||
		uid_eq(current_euid(), KUIDT_INIT(SYSTEM_UID));

	if (!system && (system_only || sailboat_coloros_settings_disabled()))
		return -EPERM;
	if (!smp_load_acquire(&sailboat_coloros_ux_ready) ||
	    !READ_ONCE(global_sched_assist_enabled))
		return -EAGAIN;
	return 0;
}

void sailboat_coloros_ux_clear_inherit_locked(struct task_struct *task)
{
	struct oplus_task_struct *ots = get_oplus_task_struct(task);

	lockdep_assert_rq_held(task_rq(task));
	if (!ots)
		return;
	atomic64_set(&ots->inherit_ux, 0);
	ots->ux_depth = 0;
	oplus_set_ux_state_lock(task, 0, false);
}

static bool sailboat_coloros_ux_hierarchy(struct task_struct *task)
{
	struct sched_entity *entity = &task->se;

	do {
		if (!entity->on_rq)
			return false;
#ifdef CONFIG_CFS_BANDWIDTH
		if (cfs_rq_of(entity)->throttled)
			return false;
#endif
#ifdef CONFIG_FAIR_GROUP_SCHED
		entity = entity->parent;
#else
		entity = NULL;
#endif
	} while (entity);
	return true;
}

void sailboat_coloros_ux_pick(struct rq *rq, struct task_struct **task,
		struct sched_entity **entity, bool *repick)
{
	struct oplus_rq *orq = (struct oplus_rq *)rq->android_oem_data1;
	struct rb_node *node;
	unsigned long flags;
	unsigned int scanned = 0;

	if (!READ_ONCE(global_sched_assist_enabled))
		return;
	lockdep_assert_rq_held(rq);
	spin_lock_irqsave(orq->ux_list_lock, flags);
	node = rb_first_cached(&orq->ux_list);
	while (node && scanned++ < 64) {
		struct oplus_task_struct *ots = rb_entry(node,
			struct oplus_task_struct, ux_entry);
		struct task_struct *candidate = ots_to_ts(ots);
		int decision;

		node = rb_next(node);
		if (IS_ERR_OR_NULL(candidate)) {
			sailboat_coloros_ux_note(SAILBOAT_UX_REJECTED);
			break;
		}
		decision = SAILBOAT_UX_STALE;
		if (task_cpu(candidate) == cpu_of(rq) && task_rq(candidate) == rq &&
		    test_task_is_fair(candidate) &&
		    task_on_rq_queued(candidate) && !task_on_rq_migrating(candidate) &&
		    get_oplus_task_struct(candidate) == ots)
			decision = sailboat_coloros_ux_candidate(1, 1, 1, 1,
				test_task_ux(candidate), sailboat_coloros_ux_hierarchy(candidate));
		if (decision == SAILBOAT_UX_STALE) {
			update_ux_timeline_task_removal(orq, ots);
			put_task_struct(candidate);
			sailboat_coloros_ux_note(SAILBOAT_UX_REJECTED);
			continue;
		}
		if (decision == SAILBOAT_UX_SKIP) {
			sailboat_coloros_ux_note(SAILBOAT_UX_THROTTLED);
			continue;
		}
		*task = candidate;
		*entity = &candidate->se;
		*repick = true;
		sailboat_coloros_ux_note(SAILBOAT_UX_SELECTED);
		break;
	}
	spin_unlock_irqrestore(orq->ux_list_lock, flags);
}

static int sailboat_coloros_ux_status_show(struct seq_file *file, void *unused)
{
	int cpu, index;

	seq_printf(file, "profile=sailboat_coloros_ux_min ready=%u active=%d hot_disable=0\n",
		smp_load_acquire(&sailboat_coloros_ux_ready),
		READ_ONCE(global_sched_assist_enabled));
	for (index = 0; index < SAILBOAT_UX_COUNTERS; index++)
		seq_printf(file, "%s=%lld\n", sailboat_coloros_ux_counter_names[index],
			(long long)atomic64_read(&sailboat_coloros_ux_counters[index]));
	if (!smp_load_acquire(&sailboat_coloros_ux_ready))
		return 0;
	for_each_possible_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);
		struct oplus_rq *orq = (struct oplus_rq *)rq->android_oem_data1;
		unsigned long flags;
		int running;
		u64 weight;

		raw_spin_rq_lock_irqsave(rq, flags);
		spin_lock(orq->ux_list_lock);
		running = orq->nr_running;
		weight = orq->load_weight;
		spin_unlock(orq->ux_list_lock);
		raw_spin_rq_unlock_irqrestore(rq, flags);
		seq_printf(file, "cpu%d nr_running=%d load_weight=%llu\n", cpu,
			running, (unsigned long long)weight);
	}
	return 0;
}

int sailboat_coloros_ux_status_init(struct proc_dir_entry *parent)
{
	return proc_create_single("ux_status", 0444, parent,
		sailboat_coloros_ux_status_show) ? 0 : -ENOMEM;
}
