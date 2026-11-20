/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SAILBOAT_COLOROS_UX_H
#define SAILBOAT_COLOROS_UX_H
#include <linux/spinlock_types.h>
#include "sailboat_coloros_ux_policy.h"

struct rq;
struct task_struct;
struct sched_entity;
struct proc_dir_entry;
struct oplus_task_struct;

struct oplus_task_struct *sailboat_coloros_ux_ensure_ots(struct task_struct *task);
void sailboat_coloros_ux_clear_inherit_locked(struct task_struct *task);

enum sailboat_coloros_ux_counter {
	SAILBOAT_UX_ENQUEUE,
	SAILBOAT_UX_DEQUEUE,
	SAILBOAT_UX_SELECTED,
	SAILBOAT_UX_RUNTIME,
	SAILBOAT_UX_EXPIRED,
	SAILBOAT_UX_REJECTED,
	SAILBOAT_UX_THROTTLED,
	SAILBOAT_UX_SET,
	SAILBOAT_UX_ALLOC_FAILED,
	SAILBOAT_UX_COUNTERS,
};

void sailboat_coloros_ux_note(enum sailboat_coloros_ux_counter counter);
spinlock_t *sailboat_coloros_ux_rq_lock(int cpu);
int sailboat_coloros_ux_prepare(void);
int sailboat_coloros_ux_publish_ready(void);
int sailboat_coloros_ux_set_enabled(int value);
int sailboat_coloros_ux_write_gate(bool system_only);
int sailboat_coloros_ux_status_init(struct proc_dir_entry *parent);
void sailboat_coloros_ux_pick(struct rq *rq, struct task_struct **task,
		struct sched_entity **entity, bool *repick);
#endif
