/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/hashtable.h>
#include <linux/jiffies.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/slab.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/tcp.h>
#include "hans.h"

#define HANS_UID_HASH_BITS 6
#define HANS_MAX_UIDS 4096

struct hans_uid_state {
	uid_t uid;
	bool armed;
	bool persistent;
	bool seen_data;
	unsigned long last_data;
	u64 arm_generation;
	struct hlist_node node;
};

static DEFINE_HASHTABLE(uid_map, HANS_UID_HASH_BITS);
static DEFINE_SPINLOCK(map_lock);
static unsigned int uid_count;
static u64 next_arm_generation;

static struct hans_uid_state *hans_find_uid(uid_t uid)
{
	struct hans_uid_state *entry;

	hash_for_each_possible(uid_map, entry, node, uid) {
		if (entry->uid == uid)
			return entry;
	}
	return NULL;
}

void hans_network_reset(void)
{
	struct hans_uid_state *entry;
	struct hlist_node *tmp;
	unsigned long flags;
	int bucket;

	spin_lock_irqsave(&map_lock, flags);
	hash_for_each_safe(uid_map, bucket, tmp, entry, node) {
		hash_del(&entry->node);
		kfree(entry);
	}
	uid_count = 0;
	spin_unlock_irqrestore(&map_lock, flags);
}

static void hans_rearm_failed(uid_t uid, u64 generation)
{
	struct hans_uid_state *entry;
	unsigned long flags;

	spin_lock_irqsave(&map_lock, flags);
	entry = hans_find_uid(uid);
	if (entry && entry->arm_generation == generation)
		entry->armed = true;
	spin_unlock_irqrestore(&map_lock, flags);
}

int hans_network_command(uid_t uid, int persistent, int cmd)
{
	struct hans_uid_state *entry, *allocated = NULL;
	unsigned long flags;
	u64 generation = 0;
	bool immediate = false;
	int ret = 0;

	if (cmd == HANS_DEL_ALL) {
		hans_network_reset();
		return 0;
	}
	if (uid < HANS_MIN_APP_UID || persistent < 0 || persistent > 2 ||
	    (cmd != HANS_ADD_UID && cmd != HANS_DEL_UID))
		return -EINVAL;
	if (cmd == HANS_ADD_UID && persistent != 2) {
		allocated = kzalloc(sizeof(*allocated), GFP_KERNEL);
		if (!allocated)
			return -ENOMEM;
		allocated->uid = uid;
	}
	spin_lock_irqsave(&map_lock, flags);
	entry = hans_find_uid(uid);
	if (!entry && allocated) {
		if (uid_count >= HANS_MAX_UIDS) {
			ret = -ENOSPC;
			goto unlock;
		}
		entry = allocated;
		allocated = NULL;
		hash_add(uid_map, &entry->node, uid);
		uid_count++;
	}
	if (!entry)
		goto unlock;
	if (cmd == HANS_DEL_UID) {
		entry->armed = false;
		entry->arm_generation = ++next_arm_generation;
	} else if (persistent == 2) {
		entry->persistent = false;
		entry->seen_data = false;
	} else if (persistent == 1) {
		entry->persistent = true;
	} else {
		entry->armed = true;
		entry->arm_generation = ++next_arm_generation;
		generation = entry->arm_generation;
		if (entry->persistent && entry->seen_data &&
		    time_before(jiffies, entry->last_data + HZ)) {
			entry->armed = false;
			immediate = true;
		}
	}
	if (!entry->armed && !entry->persistent && !immediate) {
		hash_del(&entry->node);
		uid_count--;
		kfree(entry);
	}
unlock:
	spin_unlock_irqrestore(&map_lock, flags);
	kfree(allocated);
	if (immediate) {
		ret = hans_report(HANS_PKG, -1, uid, -1, uid, "PKG", -1);
		if (ret)
			hans_rearm_failed(uid, generation);
	}
	return ret;
}

static bool hans_tcp_packet(struct sk_buff *skb, bool *has_data)
{
	struct iphdr ip_storage;
	struct tcphdr tcp_storage;
	const struct iphdr *ip;
	const struct tcphdr *tcp;
	int network_offset = skb_network_offset(skb);
	unsigned int offset, thoff, packet_end;
#if IS_ENABLED(CONFIG_IPV6)
	struct ipv6hdr ipv6_storage;
	const struct ipv6hdr *ip6;
	unsigned short fragment = 0;
#endif

	*has_data = false;
	if (network_offset < 0 || network_offset > skb->len)
		return false;
	offset = network_offset;
	ip = skb_header_pointer(skb, offset, sizeof(ip_storage), &ip_storage);
	if (!ip)
		return false;
	if (ip->version == 4) {
		if (ip->ihl < 5 || ip->protocol != IPPROTO_TCP ||
		    (ntohs(ip->frag_off) & IP_OFFSET))
			return false;
		thoff = offset + ip->ihl * 4;
		packet_end = offset + ntohs(ip->tot_len);
	} else if (ip->version == 6) {
#if IS_ENABLED(CONFIG_IPV6)
		ip6 = skb_header_pointer(skb, offset, sizeof(ipv6_storage), &ipv6_storage);
		if (!ip6)
			return false;
		thoff = 0;
		if (ipv6_find_hdr(skb, &thoff, -1, &fragment, NULL) != IPPROTO_TCP || fragment)
			return false;
		packet_end = offset + sizeof(*ip6) + ntohs(ip6->payload_len);
#else
		return false;
#endif
	} else {
		return false;
	}
	tcp = skb_header_pointer(skb, thoff, sizeof(tcp_storage), &tcp_storage);
	if (!tcp || tcp->doff < 5 || packet_end > skb->len ||
	    thoff > packet_end || tcp->doff * 4 > packet_end - thoff)
		return false;
	*has_data = packet_end - thoff > tcp->doff * 4;
	return true;
}

static unsigned int hans_network_input(void *priv, struct sk_buff *skb,
				       const struct nf_hook_state *state)
{
	struct hans_uid_state *entry;
	struct sock *sk;
	unsigned long flags;
	bool has_data, notify = false;
	u64 generation = 0;
	uid_t uid;
	int ret;

	if (!hans_session_active() || !skb || !state || !state->in ||
	    !hans_tcp_packet(skb, &has_data))
		return NF_ACCEPT;
	sk = skb_to_full_sk(skb);
	if (!sk || !sk_fullsock(sk))
		return NF_ACCEPT;
	uid = __kuid_val(sock_i_uid(sk));
	if (uid < HANS_MIN_APP_UID)
		return NF_ACCEPT;
	atomic64_inc(&sailboat_coloros_hans_counters.network_packets);
	spin_lock_irqsave(&map_lock, flags);
	entry = hans_find_uid(uid);
	if (entry) {
		if (entry->persistent && has_data) {
			entry->last_data = jiffies;
			entry->seen_data = true;
		}
		if (entry->armed) {
			generation = entry->arm_generation;
			entry->armed = false;
			notify = true;
		}
	}
	spin_unlock_irqrestore(&map_lock, flags);
	if (notify) {
		ret = hans_report(HANS_PKG, -1, -1, -1, uid, "PKG", -1);
		if (ret) {
			hans_rearm_failed(uid, generation);
		} else {
			spin_lock_irqsave(&map_lock, flags);
			entry = hans_find_uid(uid);
			if (entry && entry->arm_generation == generation &&
			    !entry->armed && !entry->persistent) {
				hash_del(&entry->node);
				uid_count--;
				kfree(entry);
			}
			spin_unlock_irqrestore(&map_lock, flags);
		}
	}
	return NF_ACCEPT;
}

static const struct nf_hook_ops hans_nf_ops[] = {
	{
		.hook = hans_network_input,
		.pf = NFPROTO_IPV4,
		.hooknum = NF_INET_LOCAL_IN,
		.priority = NF_IP_PRI_SELINUX_LAST + 1,
	},
#if IS_ENABLED(CONFIG_IPV6)
	{
		.hook = hans_network_input,
		.pf = NFPROTO_IPV6,
		.hooknum = NF_INET_LOCAL_IN,
		.priority = NF_IP6_PRI_SELINUX_LAST + 1,
	},
#endif
};

static int hans_net_init(struct net *net)
{
	return nf_register_net_hooks(net, hans_nf_ops, ARRAY_SIZE(hans_nf_ops));
}

static void hans_net_exit(struct net *net)
{
	nf_unregister_net_hooks(net, hans_nf_ops, ARRAY_SIZE(hans_nf_ops));
}

static struct pernet_operations hans_pernet_ops = {
	.init = hans_net_init,
	.exit = hans_net_exit,
};

int hans_netfilter_init(void)
{
	return register_pernet_subsys(&hans_pernet_ops);
}

void hans_netfilter_exit(void)
{
	unregister_pernet_subsys(&hans_pernet_ops);
	hans_network_reset();
}

void hans_network_status(struct seq_file *m)
{
	struct hans_uid_state *entry;
	unsigned long flags;
	unsigned int armed = 0, persistent = 0, total;
	int bucket;

	spin_lock_irqsave(&map_lock, flags);
	hash_for_each(uid_map, bucket, entry, node) {
		armed += entry->armed;
		persistent += entry->persistent;
	}
	total = uid_count;
	spin_unlock_irqrestore(&map_lock, flags);
	seq_printf(m, "network_uids=%u\nnetwork_armed=%u\nnetwork_persistent=%u\n",
		   total, armed, persistent);
}
