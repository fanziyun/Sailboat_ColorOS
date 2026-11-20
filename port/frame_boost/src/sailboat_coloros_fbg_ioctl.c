// SPDX-License-Identifier: GPL-2.0-only
#include <linux/compat.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include "frame_ioctl.h"
#include "sailboat_coloros_fbg.h"

static DEFINE_MUTEX(sailboat_coloros_fbg_ioctl_lock);

long sailboat_coloros_fbg_dispatch(struct file *file, unsigned int command,
		unsigned long argument, bool privileged,
		long (*dispatch)(struct file *, unsigned int, unsigned long))
{
	struct ofb_ctrl_data data;
	struct ofb_stune_data tune;
	union {
		struct ofb_ctrl_cluster cluster;
		struct ofb_key_thread_info key_threads;
		struct ofb_frame_util_info frame_util;
	} extra;
	void __user *input = (void __user *)argument;
	void *snapshot = &data;
	bool extra_output = false;
	int stage = -1;
	long ret;

	if (in_compat_syscall())
		return -EOPNOTSUPP;
	if (_IOC_TYPE(command) == OFB_EXTRA_MAGIC) {
		if (!privileged)
			return -EPERM;
		switch (command) {
		case CMD_ID_SET_TASK_PREFERED_CLUSTER:
			if (copy_from_user(&extra.cluster, input, sizeof(extra.cluster)))
				return -EFAULT;
			if (extra.cluster.tid <= 0 || extra.cluster.cluster_id < -1)
				return -ERANGE;
			snapshot = &extra.cluster;
			break;
		case CMD_ID_ADD_TASK_TO_GROUP:
			if (copy_from_user(&extra.key_threads, input, sizeof(extra.key_threads)))
				return -EFAULT;
			if (extra.key_threads.thread_num > MAX_KEY_THREAD_NUM ||
			    extra.key_threads.add > 1)
				return -ERANGE;
			snapshot = &extra.key_threads;
			break;
		case CMD_ID_NOTIFY_FRAME_START:
			memset(&extra.frame_util, 0, sizeof(extra.frame_util));
			snapshot = &extra.frame_util;
			extra_output = true;
			break;
		default:
			return -EOPNOTSUPP;
		}
		stage = command;
		goto dispatch_snapshot;
	}
	if (_IOC_TYPE(command) != OFB_MAGIC)
		return -EOPNOTSUPP;
	if (command == CMD_ID_BOOST_STUNE || command == CMD_ID_BOOST_STUNE_GPU) {
		if (!privileged)
			return -EPERM;
		if (copy_from_user(&tune, input, sizeof(tune)))
			return -EFAULT;
		snapshot = &tune;
		if (command == CMD_ID_BOOST_STUNE &&
		    (tune.ed_task_boost_mid_duration > 1000 ||
		     tune.ed_task_boost_max_duration > 1000 ||
		     tune.ed_task_boost_timeout_duration > 1000))
			return -ERANGE;
	} else {
		if (command != CMD_ID_SET_FPS && command != CMD_ID_BOOST_HIT &&
		    command != CMD_ID_SET_SF_MSG_TRANS)
			return -EOPNOTSUPP;
		if (copy_from_user(&data, input, sizeof(data)))
			return -EFAULT;
		stage = data.stage;
		if (data.pid <= 0 || data.tid < 0)
			return -EINVAL;
		if (!privileged && data.pid != current->tgid)
			return -EPERM;
		if (command == CMD_ID_SET_FPS) {
			if (privileged)
				return -EOPNOTSUPP;
			if (data.vsyncNs < 1000000 || data.vsyncNs > 1000000000)
				return -ERANGE;
		} else if (command == CMD_ID_SET_SF_MSG_TRANS) {
			if (!privileged || (stage != BOOST_MSG_TRANS_START && stage != BOOST_SF_EXECUTE))
				return -EOPNOTSUPP;
		} else if (privileged) {
			if (stage != BOOST_MOVE_FG && stage != BOOST_MOVE_FG_IMS &&
			    stage != BOOST_ADD_FRAME_TASK && stage != BOOST_ADD_FRAME_TASK_IMS)
				return -EOPNOTSUPP;
			if (stage == BOOST_ADD_FRAME_TASK || stage == BOOST_ADD_FRAME_TASK_IMS) {
				if (data.related_width < 0 || data.related_width > 20 ||
				    data.related_depth < 0 || data.related_depth > 4 ||
				    (data.capacity_need != 0 && data.capacity_need != 1))
					return -ERANGE;
			}
		} else if (stage != BOOST_FRAME_START && stage != BOOST_OBTAIN_VIEW &&
			   stage != BOOST_SET_RENDER_THREAD && stage != BOOST_FRAME_TIMEOUT &&
			   stage != BOOST_INPUT_START) {
			return -EOPNOTSUPP;
		}
	}
dispatch_snapshot:
	sailboat_coloros_fbg_note_ioctl(command, stage, privileged);
	ret = sailboat_coloros_fbg_ioctl_gate(privileged);
	if (ret)
		return ret;
	if (!privileged && command == CMD_ID_BOOST_HIT && stage == BOOST_FRAME_START)
		sailboat_coloros_fbg_adopt_frame(data.pid, data.tid);
	mutex_lock(&sailboat_coloros_fbg_ioctl_lock);
	ret = dispatch(file, command, (unsigned long)snapshot);
	mutex_unlock(&sailboat_coloros_fbg_ioctl_lock);
	if (!ret && extra_output && copy_to_user(input, &extra.frame_util, sizeof(extra.frame_util)))
		return -EFAULT;
	return ret;
}
