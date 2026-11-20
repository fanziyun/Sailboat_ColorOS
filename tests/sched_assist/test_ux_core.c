#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sailboat_coloros_ux_policy.h"

#define IS_ENABLED(option) 0
#define GFP_ATOMIC 0
#define SAILBOAT_UX_ALLOC_FAILED 0
#define OTS_IDX 2
#define NUMA_NO_NODE (-1)
typedef uint64_t u64;
struct task_struct { u64 android_oem_data1[6]; };
struct rb_node { uintptr_t parent; };
struct list_head { struct list_head *next, *prev; };
struct oplus_task_struct {
    struct rb_node ux_entry, exec_time_node;
    struct list_head fbg_list;
    int64_t inherit_ux;
    int ux_priority, ux_nice, target_process;
    bool update_running_start_time;
    int fbg_list_entry_lock, preferred_cluster_id, fbg_depth;
    unsigned long im_flag;
    int binder_async_ux_enable, binder_thread_mode;
    uint64_t total_exec;
    struct task_struct *task;
};
#define RB_CLEAR_NODE(n) ((n)->parent = (uintptr_t)(n))
#define INIT_LIST_HEAD(n) ((n)->next = (n), (n)->prev = (n))
#define atomic64_set(p, v) (*(p) = (v))
#define raw_spin_lock_init(p) (*(p) = 0)
static struct oplus_task_struct storage;
static void *oplus_task_struct_cachep;
static int fail_alloc, allocation_failures;
static void *kmem_cache_alloc(void *cache, int flags)
{
    (void)cache;
    (void)flags;
    return fail_alloc ? NULL : &storage;
}
static void sailboat_coloros_ux_note(int counter)
{
    assert(counter == SAILBOAT_UX_ALLOC_FAILED);
    allocation_failures++;
}
static struct oplus_task_struct rival;
static int freed, inject_rival;
static struct oplus_task_struct *get_oplus_task_struct(struct task_struct *task)
{
    return (void *)(uintptr_t)task->android_oem_data1[OTS_IDX];
}
static void kmem_cache_free(void *cache, void *pointer)
{
    (void)cache;
    assert(pointer == &storage);
    freed++;
}
#define IS_ERR(pointer) ((uintptr_t)(pointer) >= (uintptr_t)-4095)
static u64 cmpxchg(u64 *slot, u64 old, u64 value)
{
    u64 previous;
    if (inject_rival)
        *slot = (u64)(uintptr_t)&rival;
    previous = *slot;
    if (previous == old)
        *slot = value;
    return previous;
}
static int free_failure, dup_failure, free_live, dup_live, synchronized;
static void android_vh_free_task_handler(void *unused, struct task_struct *task)
{
    (void)unused;
    (void)task;
}
static void android_vh_dup_task_struct_handler(void *unused, struct task_struct *task,
                                             struct task_struct *original)
{
    (void)unused;
    (void)task;
    (void)original;
}
static int register_trace_android_vh_free_task(void (*fn)(void *, struct task_struct *), void *data)
{
    assert(fn == android_vh_free_task_handler && data == NULL);
    if (free_failure)
        return -ENOMEM;
    free_live = 1;
    return 0;
}
static int register_trace_android_vh_dup_task_struct(void (*fn)(void *, struct task_struct *, struct task_struct *), void *data)
{
    assert(fn == android_vh_dup_task_struct_handler && data == NULL && free_live);
    if (dup_failure)
        return -ENOMEM;
    dup_live = 1;
    return 0;
}
static void unregister_trace_android_vh_free_task(void (*fn)(void *, struct task_struct *), void *data)
{
    assert(fn == android_vh_free_task_handler && data == NULL);
    free_live = 0;
}
static void tracepoint_synchronize_unregister(void) { synchronized++; }
#include "extracted_ux_init.h"

int main(void)
{
    int ready, active, value, mask;
    for (ready = 0; ready < 2; ready++) {
        for (active = 0; active < 2; active++) {
            for (value = -1; value < 5; value++) {
                int expected = (value != 0 && value != 1) ? -EINVAL :
                    !ready ? -EAGAIN : active && !value ? -EBUSY : 0;
                assert(sailboat_coloros_ux_transition(ready, active, value) == expected);
            }
        }
    }
    for (mask = 0; mask < 64; mask++) {
        int valid = (mask & 31) == 31;
        int expected = !valid ? SAILBOAT_UX_STALE :
            (mask & 32) ? SAILBOAT_UX_PICK : SAILBOAT_UX_SKIP;
        assert(sailboat_coloros_ux_candidate(mask & 1, mask & 2, mask & 4,
            mask & 8, mask & 16, mask & 32) == expected);
    }
    for (value = -1; value < 256; value++) {
        int expected = value >= 0 && value != 64 && value < 128;
        assert(sailboat_coloros_ux_imflag_valid(value, 64, 64) == expected);
    }
    assert(sailboat_coloros_ux_value_valid(132, 0xff00c38fU, 24, 11));
    assert(sailboat_coloros_ux_value_valid(0, 0xff00c38fU, 24, 11));
    assert(!sailboat_coloros_ux_value_valid(11U << 24, 0xff00c38fU, 24, 11));
    assert(!sailboat_coloros_ux_value_valid(1U << 16, 0xff00c38fU, 24, 11));
    memset(&storage, 0xa5, sizeof(storage));
    assert(alloc_oplus_task_struct_node(0) == &storage);
    assert(storage.im_flag == 0 && storage.binder_async_ux_enable == 0);
    assert(storage.binder_thread_mode == 0 && storage.total_exec == 0);
    assert(storage.ux_priority == -1 && storage.ux_nice == -1);
    assert(storage.ux_entry.parent == (uintptr_t)&storage.ux_entry);
    assert(storage.exec_time_node.parent == (uintptr_t)&storage.exec_time_node);
    assert(storage.fbg_list.next == &storage.fbg_list);
    storage.im_flag = 1UL << 9;
    storage.binder_thread_mode = 7;
    storage.total_exec = UINT64_MAX;
    assert(alloc_oplus_task_struct_node(0) == &storage);
    assert(storage.im_flag == 0 && storage.binder_thread_mode == 0);
    assert(storage.total_exec == 0 && storage.inherit_ux == 0);
    fail_alloc = 1;
    assert(alloc_oplus_task_struct_node(0) == NULL);
    assert(allocation_failures == 1);
    {
        struct task_struct task = {0};
        fail_alloc = 0;
        assert(sailboat_coloros_ux_ensure_ots(&task) == &storage);
        assert(storage.task == &task);
        storage.im_flag = 8;
        assert(sailboat_coloros_ux_ensure_ots(&task) == &storage);
        assert(storage.im_flag == 8);
        task.android_oem_data1[OTS_IDX] = 0;
        inject_rival = 1;
        assert(sailboat_coloros_ux_ensure_ots(&task) == &rival);
        assert(freed == 1);
    }
    free_failure = 1;
    assert(register_oemdata_hooks() == -ENOMEM);
    assert(!free_live && !dup_live && !synchronized);
    free_failure = 0;
    dup_failure = 1;
    assert(register_oemdata_hooks() == -ENOMEM);
    assert(!free_live && !dup_live && synchronized == 1);
    dup_failure = 0;
    assert(register_oemdata_hooks() == 0);
    assert(free_live && dup_live);
    puts("UX C tests: transition/candidates, real OTS reuse/OOM/CAS and hook failure cleanup passed");
    return 0;
}
