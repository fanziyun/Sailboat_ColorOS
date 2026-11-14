// SPDX-License-Identifier: GPL-2.0-only

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/sysctl.h>
#include <linux/uaccess.h>

#include "sa_sysfs.h"
#include "sailboat_coloros_nodes.h"

#define SAILBOAT_OPAQUE_MAX	128

static int sailboat_sa_disable_setting = 1;
static int sailboat_sa_audio_enable;

static DEFINE_MUTEX(sailboat_sa_opaque_lock);
static char sailboat_sa_sched_prop[SAILBOAT_OPAQUE_MAX];
static char sailboat_sa_tpd_cmds[SAILBOAT_OPAQUE_MAX];
static char sailboat_sa_tpd_id[SAILBOAT_OPAQUE_MAX];

static struct ctl_table_header *sailboat_sa_scene_hdr;

static ssize_t sailboat_int_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos, int *val)
{
	char buffer[16];
	int ret;

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	buffer[count] = '\0';
	ret = kstrtoint(strstrip(buffer), 10, val);
	if (ret)
		return ret;

	return count;
}

static ssize_t sailboat_disable_setting_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	return sailboat_int_write(file, buf, count, ppos, &sailboat_sa_disable_setting);
}

static ssize_t sailboat_disable_setting_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[16];
	size_t len;

	len = snprintf(buffer, sizeof(buffer), "%d\n", sailboat_sa_disable_setting);

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t sailboat_audio_enable_write(struct file *file,
		const char __user *buf, size_t count, loff_t *ppos)
{
	return sailboat_int_write(file, buf, count, ppos, &sailboat_sa_audio_enable);
}

static ssize_t sailboat_audio_enable_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[16];
	size_t len;

	len = snprintf(buffer, sizeof(buffer), "enabled=%d\n", sailboat_sa_audio_enable);

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t sailboat_opaque_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos, char *store)
{
	if (!count)
		return 0;

	if (count > SAILBOAT_OPAQUE_MAX - 1)
		count = SAILBOAT_OPAQUE_MAX - 1;

	mutex_lock(&sailboat_sa_opaque_lock);
	if (copy_from_user(store, buf, count)) {
		mutex_unlock(&sailboat_sa_opaque_lock);
		return -EFAULT;
	}
	store[count] = '\0';
	strim(store);
	mutex_unlock(&sailboat_sa_opaque_lock);

	return count;
}

static ssize_t sailboat_opaque_read(char __user *buf, size_t count, loff_t *ppos,
		char *store)
{
	char buffer[SAILBOAT_OPAQUE_MAX + 2];
	size_t len;

	mutex_lock(&sailboat_sa_opaque_lock);
	len = snprintf(buffer, sizeof(buffer), "%s\n", store);
	mutex_unlock(&sailboat_sa_opaque_lock);

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t sailboat_sched_prop_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	return sailboat_opaque_write(file, buf, count, ppos, sailboat_sa_sched_prop);
}

static ssize_t sailboat_sched_prop_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	return sailboat_opaque_read(buf, count, ppos, sailboat_sa_sched_prop);
}

static ssize_t sailboat_tpd_cmds_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	return sailboat_opaque_write(file, buf, count, ppos, sailboat_sa_tpd_cmds);
}

static ssize_t sailboat_tpd_cmds_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	return sailboat_opaque_read(buf, count, ppos, sailboat_sa_tpd_cmds);
}

static ssize_t sailboat_tpd_id_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	return sailboat_opaque_write(file, buf, count, ppos, sailboat_sa_tpd_id);
}

static ssize_t sailboat_tpd_id_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	return sailboat_opaque_read(buf, count, ppos, sailboat_sa_tpd_id);
}

static const struct proc_ops sailboat_disable_setting_fops = {
	.proc_write	= sailboat_disable_setting_write,
	.proc_read	= sailboat_disable_setting_read,
	.proc_lseek	= default_llseek,
};

static const struct proc_ops sailboat_audio_enable_fops = {
	.proc_write	= sailboat_audio_enable_write,
	.proc_read	= sailboat_audio_enable_read,
	.proc_lseek	= default_llseek,
};

static const struct proc_ops sailboat_sched_prop_fops = {
	.proc_write	= sailboat_sched_prop_write,
	.proc_read	= sailboat_sched_prop_read,
	.proc_lseek	= default_llseek,
};

static const struct proc_ops sailboat_tpd_cmds_fops = {
	.proc_write	= sailboat_tpd_cmds_write,
	.proc_read	= sailboat_tpd_cmds_read,
	.proc_lseek	= default_llseek,
};

static const struct proc_ops sailboat_tpd_id_fops = {
	.proc_write	= sailboat_tpd_id_write,
	.proc_read	= sailboat_tpd_id_read,
	.proc_lseek	= default_llseek,
};

static struct ctl_table sailboat_sa_scene_table[] = {
	{
		.procname	= "sched_assist_scene",
		.data		= &global_sched_assist_scene,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{ }
};

int sailboat_coloros_nodes_proc_init(struct proc_dir_entry *parent)
{
	struct proc_dir_entry *audio_dir;

	if (!parent)
		return -EINVAL;

	proc_create("disable_setting", 0666, parent, &sailboat_disable_setting_fops);
	proc_create("sched_prop", 0666, parent, &sailboat_sched_prop_fops);
	proc_create("tpd_cmds", 0666, parent, &sailboat_tpd_cmds_fops);
	proc_create("tpd_id", 0666, parent, &sailboat_tpd_id_fops);

	audio_dir = proc_mkdir("audio", parent);
	if (audio_dir)
		proc_create("enable", 0666, audio_dir, &sailboat_audio_enable_fops);

	sailboat_sa_scene_hdr = register_sysctl("kernel", sailboat_sa_scene_table);
	if (IS_ERR(sailboat_sa_scene_hdr)) {
		pr_err("sailboat_coloros_nodes: sched_assist_scene sysctl register failed\n");
		sailboat_sa_scene_hdr = NULL;
		return -ENOENT;
	}

	return 0;
}


