/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/uaccess.h>
#include <linux/rcupdate.h>
#include "hans.h"
#ifdef CONFIG_CFS_BANDWIDTH
#include "sched.h"
#endif

static struct hlist_head *binder_procs;
static struct mutex *binder_procs_lock;
static unsigned long registered_hooks;

static bool hans_cpuctl_task(struct task_struct *task)
{
#ifdef CONFIG_CFS_BANDWIDTH
	struct task_group *group = READ_ONCE(task->sched_task_group);

	return group && READ_ONCE(group->cfs_bandwidth.quota) != RUNTIME_INF;
#else
	return false;
#endif
}

bool hans_binder_ready(void)
{
	return smp_load_acquire(&binder_procs_lock) && READ_ONCE(binder_procs);
}

static void hans_binder_preset(void *unused, struct hlist_head *head,
			       struct mutex *lock)
{
	if (!head || !lock)
		return;
	if (!READ_ONCE(binder_procs_lock)) {
		WRITE_ONCE(binder_procs, head);
		smp_store_release(&binder_procs_lock, lock);
	}
}

static void hans_async_token(struct binder_transaction_data *tr,
			     char token[HANS_TOKEN_SIZE])
{
	unsigned char data[HANS_TOKEN_SIZE] = { 0 };
	const void __user *src = (const void __user *)(uintptr_t)tr->data.ptr.buffer;
	size_t len = min_t(size_t, tr->data_size, sizeof(data));
	size_t offset, count = 0;
	unsigned long uncopied;

	memset(token, 0, HANS_TOKEN_SIZE);
	if (len <= HANS_PARCEL_OFFSET || !access_ok(src, len))
		goto failed;
	pagefault_disable();
	uncopied = __copy_from_user_inatomic(data, src, len);
	pagefault_enable();
	if (uncopied)
		goto failed;
	for (offset = HANS_PARCEL_OFFSET; offset + 1 < len && count + 1 < HANS_TOKEN_SIZE;
	     offset += 2) {
		if (!data[offset])
			break;
		token[count++] = data[offset];
	}
	return;
failed:
	atomic64_inc(&sailboat_coloros_hans_counters.token_faults);
}

static void hans_binder_trans(void *unused, struct binder_proc *target,
		struct binder_proc *source, struct binder_thread *thread,
		struct binder_transaction_data *tr)
{
	char token[HANS_TOKEN_SIZE];
	uid_t uid;

	if (!hans_session_active() || !target || !source || !tr ||
	    !target->tsk || !source->tsk)
		return;
	uid = __kuid_val(task_uid(target->tsk));
	if (!(tr->flags & TF_ONE_WAY) && hans_cpuctl_task(target->tsk) &&
	    (uid >= HANS_MIN_APP_UID || uid == HANS_SYSTEM_UID))
		hans_report(HANS_SYNC_BINDER_CPUCTL, task_tgid_nr(source->tsk),
			__kuid_val(task_uid(source->tsk)), task_tgid_nr(target->tsk),
			uid, "SYNC_BINDER_CPUCTL", -1);
	if (uid < HANS_MIN_APP_UID || source->pid == target->pid ||
	    !hans_task_frozen(target->tsk))
		return;
	if (!(tr->flags & TF_ONE_WAY)) {
		hans_report(HANS_SYNC_BINDER, task_tgid_nr(source->tsk),
			__kuid_val(task_uid(source->tsk)), task_tgid_nr(target->tsk),
			uid, "SYNC_BINDER", -1);
		return;
	}
	hans_async_token(tr, token);
	hans_report(HANS_ASYNC_BINDER, task_tgid_nr(source->tsk),
		__kuid_val(task_uid(source->tsk)), task_tgid_nr(target->tsk),
		uid, token, tr->code);
}

static void hans_binder_reply(void *unused, struct binder_proc *target,
		struct binder_proc *source, struct binder_thread *thread,
		struct binder_transaction_data *tr)
{
	if (!hans_session_active() || !target || !source || !target->tsk || !source->tsk)
		return;
	if (__kuid_val(task_uid(target->tsk)) <= 2000 && source->pid != target->pid &&
	    hans_task_frozen(target->tsk))
		hans_report(HANS_SYNC_BINDER, task_tgid_nr(source->tsk),
			__kuid_val(task_uid(source->tsk)), task_tgid_nr(target->tsk),
			__kuid_val(task_uid(target->tsk)), "SYNC_BINDER_REPLY", -1);
}

static void hans_binder_alloc(void *unused, size_t size,
			      size_t *free_async_space, int is_async)
{
	struct binder_alloc *alloc;
	struct task_struct *task;
	size_t available;

	if (!hans_session_active() || !is_async || !free_async_space)
		return;
	alloc = container_of(free_async_space, struct binder_alloc, free_async_space);
	available = *free_async_space;
	if (size > (SIZE_MAX / 3) - sizeof(struct binder_buffer))
		return;
	if (available >= 3 * (size + sizeof(struct binder_buffer)) && available >= 100 * 1024)
		return;
	rcu_read_lock();
	task = find_task_by_vpid(alloc->pid);
	if (task && hans_task_frozen(task))
		hans_report(HANS_ASYNC_BINDER, task_tgid_nr(current),
			__kuid_val(task_uid(current)), task_tgid_nr(task),
			__kuid_val(task_uid(task)), "free_buffer_full", -1);
	rcu_read_unlock();
}

static void hans_signal(void *unused, int sig, struct task_struct *source,
			struct task_struct *target)
{
	bool terminal = sig == SIGKILL || sig == SIGTERM || sig == SIGABRT || sig == SIGQUIT;

	if (!hans_session_active() || !source || !target)
		return;
	if ((terminal && hans_task_frozen(target)) ||
	    ((terminal || sig == SIGIO) && hans_cpuctl_task(target)))
		hans_report(HANS_SIGNAL, task_tgid_nr(source), __kuid_val(task_uid(source)),
			task_tgid_nr(target), __kuid_val(task_uid(target)), "signal", sig);
}

struct hans_pending {
	int from_pid;
	int from_uid;
	int to_pid;
	int uid;
	int code;
	const char *reason;
};

static void hans_pending_unknown(struct hans_pending *event, int uid, const char *reason)
{
	event->from_pid = -1;
	event->from_uid = -1;
	event->to_pid = -1;
	event->uid = uid;
	event->code = 1;
	event->reason = reason;
}

static void hans_transaction_ids(struct binder_transaction *tr, struct hans_pending *event)
{
	event->from_uid = __kuid_val(tr->sender_euid);
	if (tr->from && tr->from->task)
		event->from_pid = task_tgid_nr(tr->from->task);
	else if (tr->from && tr->from->proc)
		event->from_pid = tr->from->proc->pid;
	if (tr->to_thread && tr->to_thread->task)
		event->to_pid = task_tgid_nr(tr->to_thread->task);
	else if (tr->to_proc && tr->to_proc->tsk)
		event->to_pid = task_tgid_nr(tr->to_proc->tsk);
}

static bool hans_scan_work(struct list_head *head, struct hans_pending *event, bool detail)
{
	struct binder_work *work;
	struct binder_transaction *tr;
	bool need_reply;

	list_for_each_entry(work, head, entry) {
		event->from_pid = -1;
		event->from_uid = -1;
		event->to_pid = -1;
		if (work->type == BINDER_WORK_TRANSACTION_COMPLETE || work->type == BINDER_WORK_NODE)
			continue;
		if (work->type != BINDER_WORK_TRANSACTION)
			return true;
		tr = container_of(work, struct binder_transaction, work);
		spin_lock(&tr->lock);
		if (tr->flags & TF_ONE_WAY) {
			spin_unlock(&tr->lock);
			continue;
		}
		need_reply = tr->need_reply;
		if (READ_ONCE(sailboat_coloros_hans_skip_binder_reply) && !need_reply) {
			spin_unlock(&tr->lock);
			continue;
		}
		if (detail) {
			hans_transaction_ids(tr, event);
			if (event->from_uid == event->uid) {
				spin_unlock(&tr->lock);
				continue;
			}
		}
		event->code = need_reply ? HANS_TRANS_BINDER_TRANSACTION : HANS_TRANS_BINDER_REPLY;
		spin_unlock(&tr->lock);
		return true;
	}
	return false;
}

static bool hans_scan_proc(struct binder_proc *proc, int uid, bool detail,
			   struct hans_pending *event)
{
	struct rb_node *node;
	struct binder_thread *thread;
	struct binder_transaction *tr;
	bool need_reply;
	bool found = false;

	spin_lock(&proc->inner_lock);
	for (node = rb_first(&proc->threads); node; node = rb_next(node)) {
		thread = rb_entry(node, struct binder_thread, rb_node);
		hans_pending_unknown(event, uid, "FROZEN_TRANS_THREAD");
		if (hans_scan_work(&thread->todo, event, detail)) {
			found = true;
			goto out;
		}
		tr = thread->transaction_stack;
		if (!tr)
			continue;
		spin_lock(&tr->lock);
		if (tr->to_thread == thread && !(tr->flags & TF_ONE_WAY)) {
			need_reply = tr->need_reply;
			if (READ_ONCE(sailboat_coloros_hans_skip_binder_reply) && !need_reply) {
				spin_unlock(&tr->lock);
				continue;
			}
			hans_pending_unknown(event, uid, "FROZEN_TRANS_STACK");
			if (detail) {
				hans_transaction_ids(tr, event);
				if (event->from_uid == uid) {
					spin_unlock(&tr->lock);
					continue;
				}
			}
			event->code = need_reply ? HANS_TRANS_BINDER_TRANSACTION
						 : HANS_TRANS_BINDER_REPLY;
			found = true;
			spin_unlock(&tr->lock);
			goto out;
		}
		spin_unlock(&tr->lock);
	}
	hans_pending_unknown(event, uid, "FROZEN_TRANS_PROC");
	found = hans_scan_work(&proc->todo, event, detail);
out:
	spin_unlock(&proc->inner_lock);
	return found;
}

int hans_check_transactions(uid_t uid, int type, int check_type)
{
	struct mutex *lock = smp_load_acquire(&binder_procs_lock);
	struct hlist_head *head = READ_ONCE(binder_procs);
	struct binder_proc *proc;
	struct hans_pending event;
	int ret = 0;

	if (!head || !lock) {
		atomic64_inc(&sailboat_coloros_hans_counters.scan_not_ready);
		return -EAGAIN;
	}
	atomic64_inc(&sailboat_coloros_hans_counters.scans);
	mutex_lock(lock);
	hlist_for_each_entry(proc, head, proc_node) {
		if (!proc->tsk || __kuid_val(task_uid(proc->tsk)) != uid)
			continue;
		if (hans_scan_proc(proc, uid, check_type == HANS_CHECK_ALL, &event)) {
			ret = hans_report(type, event.from_pid, event.from_uid, event.to_pid,
					  event.uid, event.reason, event.code);
			if (ret)
				break;
		}
	}
	mutex_unlock(lock);
	return ret;
}

void hans_unregister_hooks(void)
{
	if (test_bit(4, &registered_hooks))
		unregister_trace_android_vh_do_send_sig_info(hans_signal, NULL);
	if (test_bit(3, &registered_hooks))
		unregister_trace_android_vh_binder_alloc_new_buf_locked(hans_binder_alloc, NULL);
	if (test_bit(2, &registered_hooks))
		unregister_trace_android_vh_binder_reply(hans_binder_reply, NULL);
	if (test_bit(1, &registered_hooks))
		unregister_trace_android_vh_binder_trans(hans_binder_trans, NULL);
	if (test_bit(0, &registered_hooks))
		unregister_trace_android_vh_binder_preset(hans_binder_preset, NULL);
	tracepoint_synchronize_unregister();
	registered_hooks = 0;
}

int hans_register_hooks(void)
{
	int ret;

	ret = register_trace_android_vh_binder_preset(hans_binder_preset, NULL);
	if (ret)
		return ret;
	set_bit(0, &registered_hooks);
	ret = register_trace_android_vh_binder_trans(hans_binder_trans, NULL);
	if (ret)
		goto failed;
	set_bit(1, &registered_hooks);
	ret = register_trace_android_vh_binder_reply(hans_binder_reply, NULL);
	if (ret)
		goto failed;
	set_bit(2, &registered_hooks);
	ret = register_trace_android_vh_binder_alloc_new_buf_locked(hans_binder_alloc, NULL);
	if (ret)
		goto failed;
	set_bit(3, &registered_hooks);
	ret = register_trace_android_vh_do_send_sig_info(hans_signal, NULL);
	if (ret)
		goto failed;
	set_bit(4, &registered_hooks);
	return 0;
failed:
	hans_unregister_hooks();
	return ret;
}
