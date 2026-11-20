#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "sailboat_coloros_ux_policy.h"

#define READ_ONCE(value) (value)
#define WRITE_ONCE(target, value) ((target) = (value))
#define smp_store_release(target, value) (*(target) = (value))
#define smp_load_acquire(target) (*(target))
#define pr_err(...) ((void)0)
#define pr_info(...) ((void)0)
#define OPLUS_OEM_DATA_SIZE_TEST(a, b) ((void)0)
#define __init
#define THIS_MODULE ((void *)1)
#define SAILBOAT_UX_ALLOC_FAILED 0

static bool sailboat_coloros_ux_ready, sailboat_coloros_ux_resident;
static int sailboat_coloros_init_state, sailboat_coloros_init_errno, sailboat_coloros_init_stage;
static int global_sched_assist_enabled;
static int sailboat_coloros_ux_counters[1];
static int atomic64_read(const int *value) { return *value; }
#ifdef MODULE
static int references;
static void __module_get(void *module) { assert(module == THIS_MODULE); references++; }
#endif
static int fail_stage, fail_hook, hook_attempts, live_hooks, publications;
static int sailboat_coloros_ux_prepare(void)
{
    assert(!sailboat_coloros_ux_resident && !publications && !live_hooks);
    return fail_stage == SAILBOAT_UX_PREFLIGHT ? -EBUSY : 0;
}
static void sched_assist_init_oplus_rq(void)
{
    assert(sailboat_coloros_ux_resident);
    assert(sailboat_coloros_init_stage == SAILBOAT_UX_RQ_INIT);
    publications++;
}
static int sa_oemdata_init(void)
{
    assert(sailboat_coloros_ux_resident && publications);
    return fail_stage == SAILBOAT_UX_OEM_INIT ? -ENOMEM : 0;
}
static void update_ux_sched_cputopo(void) { }
static int test_register(const char *hook)
{
    assert(hook && sailboat_coloros_ux_resident && publications);
    hook_attempts++;
    if (fail_stage == SAILBOAT_UX_HOOK_INIT && hook_attempts == fail_hook)
        return -EBUSY;
    live_hooks++;
    return 0;
}
#define REGISTER_TRACE_VH(hook, handler) do { ret = test_register(#hook); if (ret) return ret; } while (0)
static int oplus_sched_assist_proc_init(void)
{
    assert(live_hooks == 7 && sailboat_coloros_ux_resident);
    return fail_stage == SAILBOAT_UX_PROC_INIT ? -ENOMEM : 0;
}
#include "extracted_ux_lifecycle.h"

static void reset(int failure, int hook)
{
    fail_stage = failure;
    fail_hook = hook;
    publications = hook_attempts = live_hooks = 0;
    sailboat_coloros_ux_ready = sailboat_coloros_ux_resident = false;
    sailboat_coloros_init_state = sailboat_coloros_init_errno = sailboat_coloros_init_stage = 0;
    global_sched_assist_enabled = 0;
    sailboat_coloros_ux_counters[0] = failure == SAILBOAT_UX_READY_INIT;
#ifdef MODULE
    references = 0;
#endif
}
int main(void)
{
    int failure, hook;
    reset(SAILBOAT_UX_PREFLIGHT, 0);
    assert(oplus_sched_assist_init() == -EBUSY);
    assert(!publications && !live_hooks && !sailboat_coloros_ux_resident);
    assert(!sailboat_coloros_ux_ready && sailboat_coloros_init_state == -1);
#ifdef MODULE
    assert(!references);
#endif
    for (failure = SAILBOAT_UX_OEM_INIT; failure <= SAILBOAT_UX_READY_INIT; failure++) {
        for (hook = 1; hook <= 7; hook++) {
            reset(failure, hook);
            assert(oplus_sched_assist_init() == 0);
            assert(publications == 1 && sailboat_coloros_ux_resident);
            assert(!sailboat_coloros_ux_ready && !global_sched_assist_enabled);
            assert(sailboat_coloros_init_state == -1 && sailboat_coloros_init_errno < 0);
            assert(sailboat_coloros_init_stage == failure);
            assert(sailboat_coloros_ux_publish_ready() == -EIO);
            if (failure == SAILBOAT_UX_HOOK_INIT)
                assert(live_hooks == hook - 1);
            assert(sailboat_coloros_ux_transition(sailboat_coloros_ux_ready, 0, 1) == -EAGAIN);
#ifdef MODULE
            assert(references == 1);
#endif
        }
    }
    reset(-1, 0);
    assert(oplus_sched_assist_init() == 0);
    assert(publications == 1 && live_hooks == 7);
    assert(sailboat_coloros_ux_ready && sailboat_coloros_init_state == 1);
    assert(global_sched_assist_enabled == 0 && sailboat_coloros_init_errno == 0);
#ifdef MODULE
    assert(references == 1);
    puts("UX resident module init: preflight rejection and all irreversible failure stages passed");
#else
    puts("UX built-in init: same stage and failure state protocol passed");
#endif
    return 0;
}
