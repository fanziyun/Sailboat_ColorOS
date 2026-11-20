#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "sailboat_coloros_ux_policy.h"

#define IS_ENABLED(option) 0
#define READ_ONCE(value) (value)
#define unlikely(value) (value)
#define IS_ERR_OR_NULL(pointer) (!(pointer))
#define SA_TYPE_LIGHT 1
#define SA_TYPE_HEAVY 2
#define SA_TYPE_ANIMATOR 4
#define SA_TYPE_ONCE 0x8000
#define SA_TYPE_INHERIT 0x10000
#define SCHED_ASSIST_UX_MASK 0x400f
#define SCHED_ASSIST_UX_PRIORITY_MASK 0xff000000U
#define SCHED_ASSIST_UX_PRIORITY_SHIFT 24
#define SA_OPT_CLEAR 0
#define SA_OPT_SET 128
#define SA_OPT_RESET 256
#define SA_OPT_SET_PRIORITY 512
#define PRIORITY_LEVEL_NUM 11
#define ROOT_UID 0
#define SYSTEM_UID 1000
#define PER_USER_RANGE 100000
#define FIRST_APPLICATION_UID 10000
#define LAST_APPLICATION_UID 19999
#define TASK_RUNNING 0
#define UX_STATE_INHERIT 1
#define DEBUG_FTRACE 1
#define SAILBOAT_UX_RUNTIME 0
#define SAILBOAT_UX_EXPIRED 1
#define atomic64_set(pointer, value) (*(pointer) = (value))
#define smp_mb__after_spinlock() ((void)0)
#define trace_printk(...) ((void)0)

typedef uint64_t u64;
struct rb_node { uintptr_t parent; };
struct task_struct {
    int pid, tgid, __state;
    char comm[16];
    struct { u64 sum_exec_runtime; } se;
    atomic_int references;
};
struct oplus_task_struct {
    struct rb_node ux_entry, exec_time_node;
    int ux_state, ux_priority, ux_depth;
    u64 inherit_ux, total_exec, vruntime, preset_vruntime, inherit_ux_start, enqueue_time;
};
struct oplus_rq {
    int nr_running;
    u64 load_weight;
    int lock;
    int *ux_list_lock;
};
struct rq { u64 android_oem_data1[16]; };
struct rq_flags { int unused; };
static struct rq test_rq;
static struct oplus_task_struct ots;
static struct task_struct target, caller;
static struct task_struct *current = &caller;
static pthread_mutex_t rq_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int rq_held, ux_held;
static int global_sched_assist_enabled = 1, global_debug_enabled;
static int fail_ensure, runtime_count, expired_count, gate_error;
static const int weights[] = {1024, 1277, 1586, 1991, 2501, 3121, 3906, 4904, 6100, 7620, 9548};
static struct oplus_rq *test_orq(void) { return (void *)test_rq.android_oem_data1; }
static struct rq *task_rq(struct task_struct *task) { assert(task == &target); return &test_rq; }
static struct rq *task_rq_lock(struct task_struct *task, struct rq_flags *flags)
{
    (void)flags;
    assert(task == &target && !rq_held);
    assert(pthread_mutex_lock(&rq_mutex) == 0);
    rq_held = 1;
    return &test_rq;
}
static void task_rq_unlock(struct rq *rq, struct task_struct *task, struct rq_flags *flags)
{
    (void)flags;
    assert(rq == &test_rq && task == &target && rq_held && !ux_held);
    rq_held = 0;
    assert(pthread_mutex_unlock(&rq_mutex) == 0);
}
#define spin_lock_irqsave(lock, flags) do { assert((lock) == test_orq()->ux_list_lock && rq_held && !ux_held); (flags) = 0; ux_held = 1; } while (0)
#define spin_unlock_irqrestore(lock, flags) do { (void)(lock); (void)(flags); assert(ux_held); ux_held = 0; } while (0)
static struct oplus_task_struct *get_oplus_task_struct(struct task_struct *task)
{
    assert(task == &target);
    return &ots;
}
static struct oplus_task_struct *sailboat_coloros_ux_ensure_ots(struct task_struct *task)
{
    assert(task == &target && rq_held);
    return fail_ensure ? NULL : &ots;
}
static int oplus_get_ux_state(struct task_struct *task)
{
    assert(task == &target && rq_held);
    sched_yield();
    return ots.ux_state;
}
static bool test_task_is_fair(struct task_struct *task) { return task == &target; }
static unsigned int ux_task_exec_limit(struct task_struct *task) { assert(task == &target); return 100; }
static void oplus_set_enqueue_time(struct task_struct *task, u64 time) { assert(task == &target); ots.enqueue_time = time; }
static bool oplus_rbnode_empty(struct rb_node *node) { return !node->parent || node->parent == (uintptr_t)node; }
static void update_ux_timeline_task_removal(struct oplus_rq *rq, struct oplus_task_struct *value)
{
    assert(rq == test_orq() && value == &ots && rq_held && ux_held);
    assert(!oplus_rbnode_empty(&ots.ux_entry) && !oplus_rbnode_empty(&ots.exec_time_node));
    assert(rq->nr_running == 1 && rq->load_weight == (u64)weights[ots.ux_priority]);
    rq->nr_running--;
    rq->load_weight -= weights[ots.ux_priority];
    ots.ux_entry.parent = (uintptr_t)&ots.ux_entry;
    ots.exec_time_node.parent = (uintptr_t)&ots.exec_time_node;
}
static void update_ux_timeline_task_tick(struct oplus_rq *rq, struct oplus_task_struct *value)
{
    assert(rq == test_orq() && value == &ots && ux_held);
}
static void update_vruntime_task_detach(struct oplus_rq *rq, struct oplus_task_struct *value)
{
    assert(rq == test_orq() && value == &ots && ux_held);
    ots.vruntime = 0;
    ots.preset_vruntime = 0;
}
static int get_ux_state_type(struct task_struct *task) { assert(task == &target); return 0; }
static u64 get_max_inherit_gran(struct task_struct *task) { assert(task == &target); return 100; }
static u64 calc_delta_fair(u64 delta, int priority) { assert(priority >= 0 && priority < 11); return delta; }
static void sailboat_coloros_ux_note(int counter)
{
    if (counter == SAILBOAT_UX_RUNTIME) runtime_count++;
    else { assert(counter == SAILBOAT_UX_EXPIRED); expired_count++; }
}
static void get_task_struct(struct task_struct *task) { atomic_fetch_add(&task->references, 1); }
static void put_task_struct(struct task_struct *task) { assert(atomic_fetch_sub(&task->references, 1) > 1); }
static void rcu_read_lock(void) { }
static void rcu_read_unlock(void) { }
static struct task_struct *find_task_by_vpid(int tid) { return tid == target.pid ? &target : NULL; }
struct test_uid { unsigned int val; };
static struct test_uid current_uid(void) { return (struct test_uid){0}; }
static int sailboat_coloros_ux_write_gate(bool system_only) { (void)system_only; return gate_error; }
static u64 oplus_get_inherit_ux(struct task_struct *task) { assert(task == &target && rq_held); return ots.inherit_ux; }
static void oplus_set_ux_state_lock(struct task_struct *task, int value, bool take_lock)
{
    assert(task == &target && rq_held && !take_lock);
    ots.ux_state = value;
}
static void sailboat_coloros_ux_clear_inherit_locked(struct task_struct *task)
{
    assert(task == &target && rq_held);
    ots.inherit_ux = 0;
    ots.ux_depth = 0;
    oplus_set_ux_state_lock(task, 0, false);
}
#include "extracted_ux_paths.h"

static void reset(void)
{
    memset(&test_rq, 0, sizeof(test_rq));
    memset(&ots, 0, sizeof(ots));
    memset(&target, 0, sizeof(target));
    target.pid = 42;
    target.tgid = 41;
    atomic_init(&target.references, 1);
    test_orq()->ux_list_lock = &test_orq()->lock;
    ots.ux_entry.parent = (uintptr_t)&ots.ux_entry;
    ots.exec_time_node.parent = (uintptr_t)&ots.exec_time_node;
}
static void link_task(void)
{
    ots.ux_entry.parent = ots.exec_time_node.parent = 1;
    test_orq()->nr_running = 1;
    test_orq()->load_weight = weights[ots.ux_priority];
    get_task_struct(&target);
}
static void *set_label(void *argument)
{
    int value = *(int *)argument;
    for (int index = 0; index < 2000; index++)
        assert(write_task_ux(target.tgid, target.pid, SA_OPT_SET | value, true) >= 0);
    return NULL;
}
int main(void)
{
    struct rq_flags flags;
    struct rq *rq;
    reset();
    ots.ux_state = SA_TYPE_ONCE | SA_TYPE_LIGHT;
    ots.ux_priority = 2;
    link_task();
    rq = task_rq_lock(&target, &flags);
    android_vh_sched_stat_runtime_handler(NULL, &target, 100, 0);
    assert(expired_count == 1 && runtime_count == 1 && ots.total_exec == 100);
    assert(!test_task_ux(&target));
    assert(test_orq()->nr_running == 0 && test_orq()->load_weight == 0);
    target.__state = 1;
    dequeue_ux_thread(rq, &target);
    assert(ots.ux_state == 0 && ots.total_exec == 0 && !test_task_ux(&target));
    task_rq_unlock(rq, &target, &flags);
    assert(atomic_load(&target.references) == 1);

    reset();
    ots.ux_state = SA_TYPE_HEAVY;
    ots.ux_priority = 10;
    link_task();
    target.__state = 1;
    rq = task_rq_lock(&target, &flags);
    dequeue_ux_thread(rq, &target);
    assert(ots.ux_state == SA_TYPE_HEAVY && ots.total_exec == 0);
    assert(test_orq()->load_weight == 0 && test_orq()->nr_running == 0);
    task_rq_unlock(rq, &target, &flags);
    assert(atomic_load(&target.references) == 1);

    reset();
    fail_ensure = 1;
    assert(write_task_ux(target.tgid, target.pid, SA_OPT_SET | SA_TYPE_LIGHT, true) == -ENOMEM);
    assert(!rq_held && atomic_load(&target.references) == 1);
    fail_ensure = 0;
    gate_error = -EAGAIN;
    assert(write_task_ux(target.tgid, target.pid, SA_OPT_SET | SA_TYPE_LIGHT, true) == -EAGAIN);
    gate_error = 0;
    assert(write_task_ux(target.tgid, 999, SA_OPT_SET | SA_TYPE_LIGHT, true) == -ESRCH);
    {
        pthread_t first, second;
        int light = SA_TYPE_LIGHT, heavy = SA_TYPE_HEAVY;
        assert(pthread_create(&first, NULL, set_label, &light) == 0);
        assert(pthread_create(&second, NULL, set_label, &heavy) == 0);
        assert(pthread_join(first, NULL) == 0 && pthread_join(second, NULL) == 0);
        assert(ots.ux_state == (SA_TYPE_LIGHT | SA_TYPE_HEAVY));
        assert(atomic_load(&target.references) == 1);
    }
    assert(write_task_ux(target.tgid, target.pid, SA_OPT_RESET | SA_OPT_SET_PRIORITY, true) == 0);
    assert(ots.ux_state == 0);
    puts("UX real C path tests: budget -> ONCE sleep cleanup, dequeue accounting, OOM, 4000 concurrent label updates passed (host kernel stubs)");
    return 0;
}
