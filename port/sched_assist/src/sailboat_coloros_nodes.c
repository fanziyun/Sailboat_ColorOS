// SPDX-License-Identifier: GPL-2.0-only
#include <linux/cred.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#include <linux/uaccess.h>
#include "sa_sysfs.h"
#include "sailboat_coloros_nodes.h"
#include "sailboat_coloros_ux.h"

static int sailboat_coloros_disable_setting = 1;
static struct ctl_table_header *sailboat_coloros_scene_header;

int sailboat_coloros_settings_disabled(void)
{
	return READ_ONCE(sailboat_coloros_disable_setting);
}

static ssize_t sailboat_coloros_disable_write(struct file *file,
		const char __user *buf, size_t count, loff_t *position)
{
	char buffer[16];
	int value, ret;

	if (!uid_eq(current_euid(), GLOBAL_ROOT_UID) &&
	    !uid_eq(current_euid(), KUIDT_INIT(1000)))
		return -EPERM;
	if (!count || count >= sizeof(buffer))
		return -EINVAL;
	if (copy_from_user(buffer, buf, count))
		return -EFAULT;
	buffer[count] = 0;
	ret = kstrtoint(strstrip(buffer), 10, &value);
	if (ret)
		return ret;
	if (value != 0 && value != 1)
		return -EINVAL;
	WRITE_ONCE(sailboat_coloros_disable_setting, value);
	return count;
}

static ssize_t sailboat_coloros_disable_read(struct file *file,
		char __user *buf, size_t count, loff_t *position)
{
	char buffer[16];
	int length;

	length = scnprintf(buffer, sizeof(buffer), "%d\n",
		READ_ONCE(sailboat_coloros_disable_setting));
	return simple_read_from_buffer(buf, count, position, buffer, length);
}

static ssize_t sailboat_coloros_unsupported_write(struct file *file,
		const char __user *buf, size_t count, loff_t *position)
{
	return -EOPNOTSUPP;
}

static ssize_t sailboat_coloros_unsupported_read(struct file *file,
		char __user *buf, size_t count, loff_t *position)
{
	return -EOPNOTSUPP;
}

static const struct proc_ops sailboat_coloros_disable_ops = {
	.proc_write = sailboat_coloros_disable_write,
	.proc_read = sailboat_coloros_disable_read,
	.proc_lseek = default_llseek,
};
static const struct proc_ops sailboat_coloros_unsupported_ops = {
	.proc_write = sailboat_coloros_unsupported_write,
	.proc_read = sailboat_coloros_unsupported_read,
	.proc_lseek = default_llseek,
};
static struct ctl_table sailboat_coloros_scene_table[] = {
	{
		.procname = "sched_assist_scene",
		.data = &global_sched_assist_scene,
		.maxlen = sizeof(int),
		.mode = 0644,
		.proc_handler = proc_dointvec,
	},
	{ }
};

int sailboat_coloros_nodes_proc_init(struct proc_dir_entry *parent)
{
	struct proc_dir_entry *audio;

	if (!parent)
		return -EINVAL;
	if (!proc_create("disable_setting", 0666, parent, &sailboat_coloros_disable_ops) ||
	    !proc_create("sched_prop", 0666, parent, &sailboat_coloros_unsupported_ops) ||
	    !proc_create("tpd_cmds", 0666, parent, &sailboat_coloros_unsupported_ops) ||
	    !proc_create("tpd_id", 0666, parent, &sailboat_coloros_unsupported_ops))
		return -ENOMEM;
	audio = proc_mkdir("audio", parent);
	if (!audio || !proc_create("enable", 0666, audio, &sailboat_coloros_unsupported_ops))
		return -ENOMEM;
	sailboat_coloros_scene_header = register_sysctl("kernel", sailboat_coloros_scene_table);
	if (IS_ERR_OR_NULL(sailboat_coloros_scene_header))
		return -ENOMEM;
	return sailboat_coloros_ux_status_init(parent);
}
