/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SAILBOAT_COLOROS_HANS_H
#define SAILBOAT_COLOROS_HANS_H

#include <linux/atomic.h>
#include <linux/cgroup.h>
#include <linux/freezer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <trace/hooks/binder.h>
#include <trace/hooks/signal.h>
#include "hans_protocol.h"
#include "binder_internal.h"
#include "binder_alloc.h"

#define HANS_TAG "sailboat_coloros_hans: "

struct hans_counters {
	atomic64_t rx;
	atomic64_t rejected;
	atomic64_t tx[HANS_TYPE_MAX];
	atomic64_t tx_failed;
	atomic64_t scans;
	atomic64_t scan_not_ready;
	atomic64_t token_faults;
	atomic64_t network_packets;
};

extern struct hans_counters sailboat_coloros_hans_counters;
extern int sailboat_coloros_hans_skip_binder_reply;

static inline bool hans_task_frozen(struct task_struct *task)
{
	struct task_struct *leader;

	if (!task)
		return false;
	if (cgroup_task_frozen(task) || (READ_ONCE(task->jobctl) & JOBCTL_TRAP_FREEZE))
		return true;
	leader = READ_ONCE(task->group_leader);
	return leader && (frozen(leader) || freezing(leader));
}

bool hans_session_active(void);
int hans_report(int type, int caller_pid, int caller_uid, int target_pid,
		int target_uid, const char *token, int code);
int hans_register_hooks(void);
void hans_unregister_hooks(void);
bool hans_binder_ready(void);
int hans_check_transactions(uid_t uid, int type, int check_type);
int hans_netfilter_init(void);
void hans_netfilter_exit(void);
int hans_network_command(uid_t uid, int persistent, int cmd);
void hans_network_reset(void);
void hans_network_status(struct seq_file *m);

#endif
