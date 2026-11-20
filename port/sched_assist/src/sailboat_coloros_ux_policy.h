/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SAILBOAT_COLOROS_UX_POLICY_H
#define SAILBOAT_COLOROS_UX_POLICY_H
#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#endif

enum sailboat_coloros_ux_candidate {
	SAILBOAT_UX_STALE,
	SAILBOAT_UX_SKIP,
	SAILBOAT_UX_PICK,
};

enum sailboat_coloros_ux_init_stage {
	SAILBOAT_UX_PREFLIGHT,
	SAILBOAT_UX_RQ_INIT,
	SAILBOAT_UX_OEM_INIT,
	SAILBOAT_UX_HOOK_INIT,
	SAILBOAT_UX_PROC_INIT,
	SAILBOAT_UX_READY_INIT,
};

static inline int sailboat_coloros_ux_init_result(int resident, int error)
{
	return resident ? 0 : error;
}

static inline int sailboat_coloros_ux_transition(int ready, int active, int value)
{
	if (value != 0 && value != 1)
		return -EINVAL;
	if (!ready)
		return -EAGAIN;
	if (active && !value)
		return -EBUSY;
	return 0;
}

static inline int sailboat_coloros_ux_imflag_valid(int value,
		unsigned int clear, unsigned int bits)
{
	unsigned int bit;

	if (value < 0)
		return 0;
	bit = (unsigned int)value > clear ? (unsigned int)value - clear :
		(unsigned int)value;
	return bit < bits;
}

static inline int sailboat_coloros_ux_value_valid(unsigned int value,
		unsigned int allowed, unsigned int shift, unsigned int levels)
{
	return !(value & ~allowed) && (value >> shift) < levels;
}

static inline int sailboat_coloros_ux_candidate(int same_rq, int fair,
		int queued, int owner, int budget, int hierarchy)
{
	if (!same_rq || !fair || !queued || !owner || !budget)
		return SAILBOAT_UX_STALE;
	return hierarchy ? SAILBOAT_UX_PICK : SAILBOAT_UX_SKIP;
}
#endif
