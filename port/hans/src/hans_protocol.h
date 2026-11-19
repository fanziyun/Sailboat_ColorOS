/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SAILBOAT_COLOROS_HANS_PROTOCOL_H
#define SAILBOAT_COLOROS_HANS_PROTOCOL_H

#include <linux/types.h>
#include <linux/errno.h>
#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/stddef.h>
#else
#include <string.h>
#include <stddef.h>
#endif

#define HANS_FAMILY "oplus_hans"
#define HANS_FAMILY_VERSION 1
#define HANS_CMD_GENL 1
#define HANS_ATTR_MSG_GENL 1
#define HANS_PORT_MAGIC 0x15356
#define HANS_SYSTEM_UID 1000
#define HANS_MIN_APP_UID 10000
#define HANS_TOKEN_SIZE 140
#define HANS_PARCEL_OFFSET 16
#define HANS_CPUCTL_VERSION 2
#define HANS_USE_CGRPV2 (-99)
#define HANS_CHECK_CGRPV2 (-8000)
#define HANS_SKIP_REPLY_ON (-9000)
#define HANS_SKIP_REPLY_OFF (-9001)
#define HANS_TRANS_BINDER_REPLY (-100)
#define HANS_TRANS_BINDER_TRANSACTION (-200)
#define HANS_LEGACY_SIZE 172
#define HANS_MESSAGE_SIZE 176
#define HANS_COLOROS17_SIZE 177
#define HANS_CHECK_UID 0
#define HANS_CHECK_ALL 2

enum hans_type {
	HANS_ASYNC_BINDER,
	HANS_SYNC_BINDER,
	HANS_FROZEN_TRANS,
	HANS_SIGNAL,
	HANS_PKG,
	HANS_SYNC_BINDER_CPUCTL,
	HANS_SIGNAL_CPUCTL,
	HANS_CPUCTL_TRANS,
	HANS_LOOP_BACK,
	HANS_TYPE_MAX,
};

enum hans_pkg_cmd {
	HANS_ADD_UID,
	HANS_DEL_UID,
	HANS_DEL_ALL,
	HANS_PKG_CMD_MAX,
};

struct hans_message {
	__s32 type;
	__s32 port;
	__s32 caller_uid;
	__s32 caller_pid;
	__s32 target_pid;
	__s32 target_uid;
	__s32 pkg_cmd;
	__s32 code;
	char rpc_name[HANS_TOKEN_SIZE];
	__s32 persistent;
};

_Static_assert(sizeof(struct hans_message) == HANS_MESSAGE_SIZE, "hans wire size");
_Static_assert(offsetof(struct hans_message, rpc_name) == 32, "hans token offset");
_Static_assert(offsetof(struct hans_message, persistent) == HANS_LEGACY_SIZE,
	       "hans legacy size");

static inline int hans_decode_message(struct hans_message *msg,
				     const void *data, size_t len)
{
	if (!msg || !data ||
	    (len != HANS_LEGACY_SIZE && len != HANS_MESSAGE_SIZE &&
	     len != HANS_COLOROS17_SIZE))
		return -EMSGSIZE;
	memset(msg, 0, sizeof(*msg));
	memcpy(msg, data, len < sizeof(*msg) ? len : sizeof(*msg));
	if (msg->type < 0 || msg->type >= HANS_TYPE_MAX || msg->port <= 0)
		return -EINVAL;
	switch (msg->type) {
	case HANS_LOOP_BACK:
		return 0;
	case HANS_PKG:
		if (msg->pkg_cmd < 0 || msg->pkg_cmd >= HANS_PKG_CMD_MAX ||
		    msg->persistent < 0 || msg->persistent > 2)
			return -EINVAL;
		if (msg->pkg_cmd != HANS_DEL_ALL && msg->target_uid < HANS_MIN_APP_UID)
			return -EINVAL;
		return 0;
	case HANS_FROZEN_TRANS:
	case HANS_CPUCTL_TRANS:
		if (msg->target_uid == HANS_CHECK_CGRPV2 ||
		    msg->target_uid == HANS_SKIP_REPLY_ON ||
		    msg->target_uid == HANS_SKIP_REPLY_OFF)
			return 0;
		if (msg->target_uid < 0 ||
		    (msg->persistent != HANS_CHECK_UID && msg->persistent != HANS_CHECK_ALL))
			return -EINVAL;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static inline void hans_make_report(struct hans_message *msg, int type,
		int caller_pid, int caller_uid, int target_pid, int target_uid,
		const char *token, int code)
{
	size_t i;

	memset(msg, 0, sizeof(*msg));
	msg->type = type;
	msg->port = HANS_PORT_MAGIC;
	msg->caller_pid = caller_pid;
	msg->caller_uid = caller_uid;
	msg->target_pid = target_pid;
	msg->target_uid = target_uid;
	msg->pkg_cmd = -1;
	msg->code = code;
	if (!token)
		return;
	for (i = 0; i + 1 < HANS_TOKEN_SIZE && token[i]; i++)
		msg->rpc_name[i] = token[i];
}

#endif
