// SPDX-License-Identifier: GPL-2.0-only
#include <linux/atomic.h>
#include <linux/cpufreq.h>
#include <linux/cred.h>
#include <linux/irq_work.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/sched/cpufreq.h>
#include <linux/seq_file.h>
#include <linux/sysctl.h>
#include <kernel/sched/sched.h>
#include <trace/hooks/cpufreq.h>
#include <trace/hooks/sched.h>
#include "sa_common.h"
#include "sailboat_coloros_ux.h"
#include "frame_boost.h"
#include "frame_group.h"
#include "sailboat_coloros_fbg.h"
#include "sailboat_coloros_fbg_policy.h"

static bool sailboat_coloros_fbg_ready;
static DEFINE_MUTEX(sailboat_coloros_fbg_control);
static DEFINE_PER_CPU(struct irq_work, sailboat_coloros_fbg_freq_work);
static atomic64_t sailboat_coloros_fbg_queries;
static atomic64_t sailboat_coloros_fbg_raised;
static atomic64_t sailboat_coloros_fbg_requests;
static atomic64_t sailboat_coloros_fbg_placements;
static atomic64_t sailboat_coloros_fbg_adopts;
static atomic64_t sailboat_coloros_fbg_launch_boosts;
static atomic64_t sailboat_coloros_fbg_boost_until;

#define SAILBOAT_COLOROS_FBG_LAUNCH_BOOST_NS 300000000ULL
#define SAILBOAT_COLOROS_FBG_BOOST_CAPACITY 800
static atomic64_t sailboat_coloros_fbg_ioctls;
static atomic64_t sailboat_coloros_fbg_system_ioctls;
static int sailboat_coloros_fbg_last_stage;
static unsigned int sailboat_coloros_fbg_last_command;

bool sailboat_coloros_fbg_active(void)
{
	return smp_load_acquire(&sailboat_coloros_fbg_ready) &&
		READ_ONCE(sysctl_frame_boost_enable);
}

int sailboat_coloros_fbg_ioctl_gate(bool privileged)
{
	if (privileged && !uid_eq(current_euid(), GLOBAL_ROOT_UID) &&
	    !uid_eq(current_euid(), KUIDT_INIT(SYSTEM_UID)))
		return -EPERM;
	return sailboat_coloros_fbg_active() ? 0 : -EAGAIN;
}

void sailboat_coloros_fbg_note_ioctl(unsigned int command, int stage, bool privileged)
{
	atomic64_inc(&sailboat_coloros_fbg_ioctls);
	if (privileged)
		atomic64_inc(&sailboat_coloros_fbg_system_ioctls);
	WRITE_ONCE(sailboat_coloros_fbg_last_stage, stage);
	WRITE_ONCE(sailboat_coloros_fbg_last_command, command);
}

void sailboat_coloros_fbg_adopt_frame(int pid, int tid)
{
	if (pid <= 0 || READ_ONCE(save_top_app_tgid) != pid)
		return;
	if (get_frame_group_ui(DEFAULT_FRAME_GROUP_ID) == pid)
		return;
	set_ui_thread(DEFAULT_FRAME_GROUP_ID, pid, tid);
	atomic64_inc(&sailboat_coloros_fbg_adopts);
}

int sailboat_coloros_fbg_enable_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table local = *table;
	int value, ret;

	mutex_lock(&sailboat_coloros_fbg_control);
	value = READ_ONCE(sysctl_frame_boost_enable);
	local.data = &value;
	ret = proc_dointvec(&local, write, buffer, lenp, ppos);
	if (ret || !write)
		goto out;
	if (!uid_eq(current_euid(), GLOBAL_ROOT_UID)) {
		ret = -EPERM;
		goto out;
	}
	if (value != 0 && value != 1) {
		ret = -EINVAL;
		goto out;
	}
	if (READ_ONCE(sysctl_frame_boost_enable) && !value) {
		ret = -EBUSY;
		goto out;
	}
	ret = sailboat_coloros_ux_write_gate(true);
	if (ret || !smp_load_acquire(&sailboat_coloros_fbg_ready)) {
		if (!ret)
			ret = -EAGAIN;
		goto out;
	}
	smp_store_release(&sysctl_frame_boost_enable, value);
out:
	mutex_unlock(&sailboat_coloros_fbg_control);
	return ret;
}

static void sailboat_coloros_fbg_schedule(void *unused, struct task_struct *prev,
		struct task_struct *next, struct rq *rq)
{
	if (sailboat_coloros_fbg_active())
		fbg_android_rvh_schedule_handler(prev, next, rq);
}

static void sailboat_coloros_fbg_select(void *unused, struct task_struct *task,
		int prev_cpu, int sd_flag, int wake_flags, int *cpu)
{
	int selected = *cpu;

	if (!sailboat_coloros_fbg_active())
		return;
	if (set_frame_group_task_to_perfer_cpu(task, &selected) && selected >= 0 &&
	    selected < nr_cpu_ids && cpu_active(selected) &&
	    cpumask_test_cpu(selected, task->cpus_ptr)) {
		*cpu = selected;
		atomic64_inc(&sailboat_coloros_fbg_placements);
	}
}

static void sailboat_coloros_fbg_migrate(void *unused, struct task_struct *task,
		int destination, int *can_migrate)
{
	if (sailboat_coloros_fbg_active() && *can_migrate &&
	    fbg_skip_migration(task, task_cpu(task), destination))
		*can_migrate = 0;
}

static void sailboat_coloros_fbg_frequency_adjust(struct cpufreq_policy *policy,
		unsigned int *target, bool update_resolved_index)
{
	struct cpufreq_frequency_table *entry;
	unsigned long util = 0;
	unsigned int floor, selected = UINT_MAX, highest = 0;
	unsigned int index, selected_index = 0, highest_index = 0;
	u64 boost_until = atomic64_read(&sailboat_coloros_fbg_boost_until);
	bool launch = boost_until && ktime_get_ns() < boost_until;

	if (!sailboat_coloros_fbg_active() || !policy || !policy->freq_table)
		return;
	atomic64_inc(&sailboat_coloros_fbg_queries);
	if (!launch) {
		if (!fbg_freq_policy_util(0, policy->related_cpus, &util) || !util)
			return;
		floor = sailboat_coloros_fbg_freq_floor(util,
			arch_scale_cpu_capacity(policy->cpu), policy->cpuinfo.max_freq,
			policy->min, policy->max);
		if (!floor || floor <= *target)
			return;
	} else {
		if (arch_scale_cpu_capacity(policy->cpu) < SAILBOAT_COLOROS_FBG_BOOST_CAPACITY)
			return;
		floor = policy->max;
	}
	cpufreq_for_each_valid_entry_idx(entry, policy->freq_table, index) {
		unsigned int frequency = entry->frequency;

		if (frequency < policy->min || frequency > policy->max)
			continue;
		if (frequency > highest) {
			highest = frequency;
			highest_index = index;
		}
		if (frequency >= floor && frequency < selected) {
			selected = frequency;
			selected_index = index;
		}
	}
	if (selected == UINT_MAX) {
		selected = highest;
		selected_index = highest_index;
	}
	if (selected > *target) {
		*target = selected;
		if (update_resolved_index)
			policy->cached_resolved_idx = selected_index;
		atomic64_inc(&sailboat_coloros_fbg_raised);
	}
}

void sailboat_coloros_fbg_launch_boost(void)
{
	int cpu;

	if (!sailboat_coloros_fbg_active())
		return;
	atomic64_set(&sailboat_coloros_fbg_boost_until,
		ktime_get_ns() + SAILBOAT_COLOROS_FBG_LAUNCH_BOOST_NS);
	atomic64_inc(&sailboat_coloros_fbg_launch_boosts);
	for_each_online_cpu(cpu)
		irq_work_queue_on(&per_cpu(sailboat_coloros_fbg_freq_work, cpu), cpu);
}

static void sailboat_coloros_fbg_fast_switch(void *unused,
		struct cpufreq_policy *policy, unsigned int *target, unsigned int old_target)
{
	sailboat_coloros_fbg_frequency_adjust(policy, target, false);
}

static void sailboat_coloros_fbg_target(void *unused,
		struct cpufreq_policy *policy, unsigned int *target, unsigned int old_target)
{
	sailboat_coloros_fbg_frequency_adjust(policy, target, true);
}

static void sailboat_coloros_fbg_update_work(struct irq_work *work)
{
	struct rq *rq = this_rq();
	struct rq_flags flags;

	if (!sailboat_coloros_fbg_active())
		return;
	rq_lock(rq, &flags);
	update_rq_clock(rq);
	cpufreq_update_util(rq, 0);
	rq_unlock(rq, &flags);
}

static void sailboat_coloros_fbg_request(struct rq *rq, unsigned int flags)
{
	int cpu = cpu_of(rq);

	if (!sailboat_coloros_fbg_active() || !cpu_online(cpu))
		return;
	atomic64_inc(&sailboat_coloros_fbg_requests);
	irq_work_queue_on(&per_cpu(sailboat_coloros_fbg_freq_work, cpu), cpu);
}

int sailboat_coloros_fbg_prepare(void)
{
	int cpu, ret;

	for_each_possible_cpu(cpu)
		init_irq_work(&per_cpu(sailboat_coloros_fbg_freq_work, cpu),
			sailboat_coloros_fbg_update_work);
	ret = sailboat_coloros_fbg_native_init();
	if (ret)
		return ret;
	ret = sailboat_coloros_fbg_native_hooks();
	if (ret)
		return ret;
#define SAILBOAT_COLOROS_FBG_REGISTER(name, function) do { \
	ret = register_trace_##name(function, NULL); \
	if (ret) \
		return ret; \
} while (0)
	SAILBOAT_COLOROS_FBG_REGISTER(android_rvh_schedule, sailboat_coloros_fbg_schedule);
	SAILBOAT_COLOROS_FBG_REGISTER(android_rvh_select_task_rq_fair, sailboat_coloros_fbg_select);
	SAILBOAT_COLOROS_FBG_REGISTER(android_rvh_can_migrate_task, sailboat_coloros_fbg_migrate);
	SAILBOAT_COLOROS_FBG_REGISTER(android_vh_cpufreq_fast_switch, sailboat_coloros_fbg_fast_switch);
	SAILBOAT_COLOROS_FBG_REGISTER(android_vh_cpufreq_target, sailboat_coloros_fbg_target);
#undef SAILBOAT_COLOROS_FBG_REGISTER
	fbg_add_update_freq_hook(sailboat_coloros_fbg_request);
	ret = sailboat_coloros_fbg_sysctl_init();
	if (ret)
		return ret;
	ret = sailboat_coloros_fbg_ua_init();
	if (ret)
		sailboat_coloros_fbg_sysctl_cleanup();
	return ret;
}

void sailboat_coloros_fbg_publish(void)
{
	smp_store_release(&sailboat_coloros_fbg_ready, true);
	pr_info("sailboat_coloros_fbg_v1: ready, runtime disabled\n");
}

void sailboat_coloros_fbg_failed(void)
{
	smp_store_release(&sailboat_coloros_fbg_ready, false);
	WRITE_ONCE(sysctl_frame_boost_enable, 0);
	sailboat_coloros_fbg_ua_cleanup();
	sailboat_coloros_fbg_sysctl_cleanup();
}

void sailboat_coloros_fbg_status(struct seq_file *file)
{
	seq_printf(file, "frame_profile=sailboat_coloros_fbg_v1 frame_ready=%u frame_active=%u frame_hot_disable=0\n",
		READ_ONCE(sailboat_coloros_fbg_ready), sailboat_coloros_fbg_active());
	seq_printf(file, "frame_ioctls=%lld frame_system_ioctls=%lld frame_last_cmd=%u frame_last_stage=%d\n",
		atomic64_read(&sailboat_coloros_fbg_ioctls),
		atomic64_read(&sailboat_coloros_fbg_system_ioctls),
		READ_ONCE(sailboat_coloros_fbg_last_command),
		READ_ONCE(sailboat_coloros_fbg_last_stage));
	seq_printf(file, "frame_freq_requests=%lld frame_freq_queries=%lld frame_freq_raised=%lld frame_placements=%lld frame_group_adopts=%lld frame_launch_boosts=%lld\n",
		atomic64_read(&sailboat_coloros_fbg_requests), atomic64_read(&sailboat_coloros_fbg_queries),
		atomic64_read(&sailboat_coloros_fbg_raised), atomic64_read(&sailboat_coloros_fbg_placements),
		atomic64_read(&sailboat_coloros_fbg_adopts), atomic64_read(&sailboat_coloros_fbg_launch_boosts));
}
