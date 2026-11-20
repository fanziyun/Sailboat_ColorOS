/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _SAILBOAT_COLOROS_NODES_H_
#define _SAILBOAT_COLOROS_NODES_H_

struct proc_dir_entry;

int sailboat_coloros_nodes_proc_init(struct proc_dir_entry *parent);
int sailboat_coloros_settings_disabled(void);

#endif
