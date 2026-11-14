// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/cpumask.h>
#include <kernel/sched/sched.h>

#define SLOTPROBE_TAG "sailboat_slotprobe"
#define SLOTPROBE_TASK_SLOTS 6
#define SLOTPROBE_RQ_SLOTS 16

static unsigned long task_nonzero[SLOTPROBE_TASK_SLOTS];
static unsigned long task_total;
static unsigned long rq_nonzero[SLOTPROBE_RQ_SLOTS];
static unsigned long rq_total;

static void scan_tasks(void)
{
	struct task_struct *g, *p;
	int i;

	rcu_read_lock();
	for_each_process_thread(g, p) {
		task_total++;
		for (i = 0; i < SLOTPROBE_TASK_SLOTS; i++)
			if (READ_ONCE(p->android_oem_data1[i]))
				task_nonzero[i]++;
	}
	rcu_read_unlock();
}

static void scan_rqs(void)
{
	int cpu, i;

	for_each_possible_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);

		rq_total++;
		for (i = 0; i < SLOTPROBE_RQ_SLOTS; i++)
			if (READ_ONCE(rq->android_oem_data1[i]))
				rq_nonzero[i]++;
	}
}

static int __init slotprobe_init(void)
{
	int i;

	scan_tasks();
	scan_rqs();

	for (i = 0; i < SLOTPROBE_TASK_SLOTS; i++)
		pr_info("%s: task slot%d nonzero %lu/%lu\n", SLOTPROBE_TAG,
			i, task_nonzero[i], task_total);
	for (i = 0; i < SLOTPROBE_RQ_SLOTS; i++)
		pr_info("%s: rq slot%d nonzero %lu/%lu\n", SLOTPROBE_TAG,
			i, rq_nonzero[i], rq_total);

	pr_info("%s: task_struct size=%zu rq size=%zu oem_off=%zu rq_oem_off=%zu\n",
		SLOTPROBE_TAG, sizeof(struct task_struct), sizeof(struct rq),
		offsetof(struct task_struct, android_oem_data1),
		offsetof(struct rq, android_oem_data1));
	return 0;
}

static void __exit slotprobe_exit(void)
{
	pr_info("%s: unloaded\n", SLOTPROBE_TAG);
}

module_init(slotprobe_init);
module_exit(slotprobe_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("read-only android_oem_data1 slot usage statistics");
