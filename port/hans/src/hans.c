/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/kernel.h>
#include <linux/netlink.h>
#include <linux/proc_fs.h>
#include <net/genetlink.h>
#include <net/netlink.h>
#include "hans.h"

struct hans_counters sailboat_coloros_hans_counters;
int sailboat_coloros_hans_skip_binder_reply = 1;

static bool native_enabled;
static bool allow_test_client;
static bool allow_system_client;
module_param(native_enabled, bool, 0444);
module_param(allow_test_client, bool, 0444);
module_param(allow_system_client, bool, 0444);

static DEFINE_MUTEX(command_lock);
static DEFINE_SPINLOCK(session_lock);
static u32 daemon_port;
static u64 session_generation;
static bool stopping;
static bool hooks_registered;
static bool network_registered;
static struct proc_dir_entry *status_dir;
static struct genl_family hans_family;

bool hans_session_active(void)
{
	return !READ_ONCE(stopping) && READ_ONCE(daemon_port) != 0;
}

static void hans_disconnect(u32 port, u64 generation)
{
	unsigned long flags;

	spin_lock_irqsave(&session_lock, flags);
	if (daemon_port == port && session_generation == generation) {
		daemon_port = 0;
		session_generation++;
	}
	spin_unlock_irqrestore(&session_lock, flags);
}

int hans_report(int type, int caller_pid, int caller_uid, int target_pid,
		int target_uid, const char *token, int code)
{
	struct hans_message msg;
	struct sk_buff *skb;
	void *header;
	unsigned long flags;
	u64 generation;
	u32 port;
	int ret;

	if (type < 0 || type >= HANS_TYPE_MAX)
		return -EINVAL;
	spin_lock_irqsave(&session_lock, flags);
	port = daemon_port;
	generation = session_generation;
	if (READ_ONCE(stopping))
		port = 0;
	spin_unlock_irqrestore(&session_lock, flags);
	if (!port)
		return -ENOTCONN;

	hans_make_report(&msg, type, caller_pid, caller_uid, target_pid,
			target_uid, token, code);
	skb = genlmsg_new(nla_total_size(sizeof(msg)), GFP_ATOMIC);
	if (!skb) {
		ret = -ENOMEM;
		goto failed;
	}
	header = genlmsg_put(skb, port, 0, &hans_family, 0, HANS_CMD_GENL);
	if (!header) {
		kfree_skb(skb);
		ret = -EMSGSIZE;
		goto failed;
	}
	ret = nla_put(skb, HANS_ATTR_MSG_GENL, sizeof(msg), &msg);
	if (ret) {
		kfree_skb(skb);
		goto failed;
	}
	genlmsg_end(skb, header);
	ret = genlmsg_unicast(&init_net, skb, port);
	if (ret < 0) {
		if (ret == -ECONNREFUSED || ret == -ESRCH)
			hans_disconnect(port, generation);
		goto failed;
	}
	atomic64_inc(&sailboat_coloros_hans_counters.tx[type]);
	return 0;
failed:
	atomic64_inc(&sailboat_coloros_hans_counters.tx_failed);
	return ret;
}

static int hans_cgroup_report(void)
{
	return hans_report(HANS_PKG, -1, -1, -1, -1, "PKG", HANS_USE_CGRPV2);
}

static int hans_loopback(u32 port)
{
	unsigned long flags;
	int ret;

	hans_network_reset();
	spin_lock_irqsave(&session_lock, flags);
	daemon_port = port;
	session_generation++;
	spin_unlock_irqrestore(&session_lock, flags);
	ret = hans_report(HANS_LOOP_BACK, -1, -1, -1, -1,
			  "loop back", HANS_CPUCTL_VERSION);
	if (!ret)
		ret = hans_cgroup_report();
	return ret;
}

static void hans_trace_message(const void *data, int len)
{
	const u8 *p = data;
	char buf[3 * 32];
	int i, pos = 0;

	if (len < 4)
		return;
	for (i = 0; i < min(len, 32); i++)
		pos += scnprintf(buf + pos, sizeof(buf) - pos, "%02x", p[i]);
	pr_info_ratelimited(HANS_TAG "head len=%d %s\n", len, buf);
	pos = 0;
	for (i = max(len - 16, 0); i < len; i++)
		pos += scnprintf(buf + pos, sizeof(buf) - pos, "%02x", p[i]);
	pr_info_ratelimited(HANS_TAG "tail len=%d %s\n", len, buf);
}

static int hans_handler(struct sk_buff *skb, struct genl_info *info)
{
	struct hans_message msg;
	kuid_t uid;
	int ret;

	atomic64_inc(&sailboat_coloros_hans_counters.rx);
	uid = NETLINK_CREDS(skb)->uid;
	pr_info_ratelimited(HANS_TAG "rx uid=%u port=%u attrlen=%d cmd=%u\n",
		__kuid_val(uid), info->snd_portid,
		info->attrs[HANS_ATTR_MSG_GENL] ?
			(int)nla_len(info->attrs[HANS_ATTR_MSG_GENL]) : -1,
		info->genlhdr->cmd);
	if (info->attrs[HANS_ATTR_MSG_GENL] &&
	    nla_len(info->attrs[HANS_ATTR_MSG_GENL]) != HANS_LEGACY_SIZE &&
	    nla_len(info->attrs[HANS_ATTR_MSG_GENL]) != HANS_MESSAGE_SIZE)
		hans_trace_message(nla_data(info->attrs[HANS_ATTR_MSG_GENL]),
				   nla_len(info->attrs[HANS_ATTR_MSG_GENL]));
	if (!(allow_test_client && uid_eq(uid, GLOBAL_ROOT_UID)) &&
	    !(allow_system_client && __kuid_val(uid) == HANS_SYSTEM_UID)) {
		ret = -EACCES;
		goto rejected;
	}
	if (!info->attrs[HANS_ATTR_MSG_GENL]) {
		ret = -EINVAL;
		goto rejected;
	}
	ret = hans_decode_message(&msg, nla_data(info->attrs[HANS_ATTR_MSG_GENL]),
				  nla_len(info->attrs[HANS_ATTR_MSG_GENL]));
	if (ret)
		goto rejected;
	if ((u32)msg.port != info->snd_portid) {
		ret = -EPERM;
		goto rejected;
	}

	mutex_lock(&command_lock);
	if (READ_ONCE(stopping)) {
		ret = -ESHUTDOWN;
		goto unlock;
	}
	if (msg.type == HANS_LOOP_BACK) {
		ret = hans_loopback(info->snd_portid);
		goto unlock;
	}
	if (READ_ONCE(daemon_port) != info->snd_portid) {
		ret = -ENOTCONN;
		goto unlock;
	}
	if (msg.type == HANS_PKG) {
		ret = hans_network_command(msg.target_uid, msg.persistent, msg.pkg_cmd);
		goto unlock;
	}
	switch (msg.target_uid) {
	case HANS_CHECK_CGRPV2:
		ret = hans_cgroup_report();
		break;
	case HANS_SKIP_REPLY_ON:
		WRITE_ONCE(sailboat_coloros_hans_skip_binder_reply, 1);
		ret = 0;
		break;
	case HANS_SKIP_REPLY_OFF:
		WRITE_ONCE(sailboat_coloros_hans_skip_binder_reply, 0);
		ret = 0;
		break;
	default:
		ret = hans_check_transactions(msg.target_uid, msg.type, msg.persistent);
		break;
	}
unlock:
	mutex_unlock(&command_lock);
	if (!ret)
		return 0;
rejected:
	atomic64_inc(&sailboat_coloros_hans_counters.rejected);
	return ret;
}

static const struct nla_policy hans_policy[HANS_ATTR_MSG_GENL + 1] = {
	[HANS_ATTR_MSG_GENL] = { .type = NLA_BINARY, .len = 512 },
};

static int hans_unexpected_cmd(struct sk_buff *skb, struct genl_info *info)
{
	pr_info_ratelimited(HANS_TAG "unexpected cmd=%u version=%u uid=%u attrlen=%d\n",
		info->genlhdr->cmd, info->genlhdr->version,
		__kuid_val(NETLINK_CREDS(skb)->uid),
		info->attrs[HANS_ATTR_MSG_GENL] ?
			(int)nla_len(info->attrs[HANS_ATTR_MSG_GENL]) : -1);
	return -EOPNOTSUPP;
}

static const struct genl_ops hans_ops[] = {
	{
		.cmd = HANS_CMD_GENL,
		.doit = hans_handler,
	},
	{ .cmd = 2, .doit = hans_unexpected_cmd },
	{ .cmd = 3, .doit = hans_unexpected_cmd },
	{ .cmd = 4, .doit = hans_unexpected_cmd },
	{ .cmd = 5, .doit = hans_unexpected_cmd },
	{ .cmd = 6, .doit = hans_unexpected_cmd },
	{ .cmd = 7, .doit = hans_unexpected_cmd },
	{ .cmd = 8, .doit = hans_unexpected_cmd },
};

static struct genl_family hans_family = {
	.name = HANS_FAMILY,
	.version = HANS_FAMILY_VERSION,
	.maxattr = HANS_ATTR_MSG_GENL,
	.policy = hans_policy,
	.module = THIS_MODULE,
	.ops = hans_ops,
	.n_ops = ARRAY_SIZE(hans_ops),
};

static int hans_status_show(struct seq_file *m, void *unused)
{
	int i;

	seq_printf(m, "module=sailboat_coloros_hans\nprototype=1\n");
	seq_printf(m, "native_enabled=0\nallow_test_client=%u\nallow_system_client=%u\n",
		   allow_test_client, allow_system_client);
	seq_printf(m, "binder_ready=%u\nsession_port=%u\nnative_pinned=0\n",
		   hans_binder_ready(), READ_ONCE(daemon_port));
	seq_printf(m, "cpuctl_available=%u\nskip_reply_available=1\nskip_binder_reply=%d\n",
		   IS_ENABLED(CONFIG_CFS_BANDWIDTH),
		   READ_ONCE(sailboat_coloros_hans_skip_binder_reply));
	seq_printf(m, "rx=%lld\nrejected=%lld\ntx_failed=%lld\nscans=%lld\n",
		   atomic64_read(&sailboat_coloros_hans_counters.rx),
		   atomic64_read(&sailboat_coloros_hans_counters.rejected),
		   atomic64_read(&sailboat_coloros_hans_counters.tx_failed),
		   atomic64_read(&sailboat_coloros_hans_counters.scans));
	seq_printf(m, "scan_not_ready=%lld\ntoken_faults=%lld\nnetwork_packets=%lld\n",
		   atomic64_read(&sailboat_coloros_hans_counters.scan_not_ready),
		   atomic64_read(&sailboat_coloros_hans_counters.token_faults),
		   atomic64_read(&sailboat_coloros_hans_counters.network_packets));
	for (i = 0; i < HANS_TYPE_MAX; i++)
		seq_printf(m, "tx_%d=%lld\n", i,
			   atomic64_read(&sailboat_coloros_hans_counters.tx[i]));
	hans_network_status(m);
	return 0;
}

static int hans_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, hans_status_show, NULL);
}

static const struct proc_ops hans_status_ops = {
	.proc_open = hans_status_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static void hans_cleanup(void)
{
	if (network_registered) {
		hans_netfilter_exit();
		network_registered = false;
	}
	if (hooks_registered) {
		hans_unregister_hooks();
		hooks_registered = false;
	}
	if (status_dir) {
		proc_remove(status_dir);
		status_dir = NULL;
	}
}

static int __init sailboat_coloros_hans_init(void)
{
	int ret;

	if (native_enabled) {
		pr_err(HANS_TAG "native mode blocked\n");
		return -EOPNOTSUPP;
	}
	ret = hans_register_hooks();
	if (ret)
		return ret;
	hooks_registered = true;
	ret = hans_netfilter_init();
	if (ret)
		goto failed;
	network_registered = true;
	status_dir = proc_mkdir("sailboat_coloros_hans", NULL);
	if (!status_dir || !proc_create("status", 0444, status_dir, &hans_status_ops)) {
		ret = -ENOMEM;
		goto failed;
	}
	ret = genl_register_family(&hans_family);
	if (ret)
		goto failed;
	pr_info(HANS_TAG "ready test_client=%u system_client=%u\n",
		allow_test_client, allow_system_client);
	return 0;
failed:
	WRITE_ONCE(stopping, true);
	hans_cleanup();
	pr_err(HANS_TAG "init failed rc=%d\n", ret);
	return ret;
}

static void __exit sailboat_coloros_hans_exit(void)
{
	mutex_lock(&command_lock);
	WRITE_ONCE(stopping, true);
	mutex_unlock(&command_lock);
	hans_cleanup();
	genl_unregister_family(&hans_family);
	pr_info(HANS_TAG "unloaded\n");
}

module_init(sailboat_coloros_hans_init);
module_exit(sailboat_coloros_hans_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sailboat; based on OnePlusOSS HANS by zhoukun1");
MODULE_DESCRIPTION("Sailboat ColorOS 5.15 HANS audit prototype; native policy unsupported");
