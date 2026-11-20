/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SAILBOAT_COLOROS_FBG_H
#define SAILBOAT_COLOROS_FBG_H

#include <linux/types.h>
#include <linux/string.h>

struct ctl_table;
struct seq_file;
struct rq;
struct file;

long sailboat_coloros_fbg_dispatch(struct file *file, unsigned int command,
		unsigned long argument, bool privileged,
		long (*dispatch)(struct file *, unsigned int, unsigned long));

static inline int sailboat_coloros_fbg_copy(void *destination,
		const void *snapshot, size_t size)
{
	memcpy(destination, snapshot, size);
	return 0;
}

int sailboat_coloros_fbg_prepare(void);
void sailboat_coloros_fbg_publish(void);
void sailboat_coloros_fbg_failed(void);
bool sailboat_coloros_fbg_active(void);
int sailboat_coloros_fbg_enable_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp, loff_t *ppos);
int sailboat_coloros_fbg_ioctl_gate(bool privileged);
void sailboat_coloros_fbg_note_ioctl(unsigned int command, int stage, bool privileged);
void sailboat_coloros_fbg_adopt_frame(int pid, int tid);
void sailboat_coloros_fbg_launch_boost(void);
void sailboat_coloros_fbg_status(struct seq_file *file);
int sailboat_coloros_fbg_native_init(void);
int sailboat_coloros_fbg_native_hooks(void);
int sailboat_coloros_fbg_sysctl_init(void);
void sailboat_coloros_fbg_sysctl_cleanup(void);
int sailboat_coloros_fbg_ua_init(void);
void sailboat_coloros_fbg_ua_cleanup(void);

#endif
