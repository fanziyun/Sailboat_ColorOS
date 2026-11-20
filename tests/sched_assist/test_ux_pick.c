#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sailboat_coloros_ux_policy.h"

#define READ_ONCE(value) (value)
#define IS_ERR_OR_NULL(pointer) (!(pointer))
#define lockdep_assert_rq_held(rq) ((void)(rq))
#define container_of(pointer, type, member) ((type *)((char *)(pointer) - offsetof(type, member)))
#define SAILBOAT_UX_SELECTED 2
#define SAILBOAT_UX_REJECTED 5
#define SAILBOAT_UX_THROTTLED 6

typedef uint64_t u64;

struct rb_node { struct rb_node *next; };
struct rb_root_cached { struct rb_node *first; };
struct cfs_rq { int throttled; };
struct sched_entity { struct cfs_rq *cfs_rq; int on_rq; };
struct rq;
struct oplus_task_struct;

struct task_struct {
    struct sched_entity se;
    struct rq *rq;
    struct oplus_task_struct *ots;
    int cpu;
    bool queued, migrating, ux, fair;
    atomic_int references;
};

struct oplus_task_struct {
    struct rb_node ux_entry;
    struct task_struct *task;
};

struct oplus_rq {
    struct rb_root_cached ux_list;
    int lock;
    int *ux_list_lock;
    int nr_running;
};

struct rq {
    int cpu;
    struct task_struct *curr;
    struct task_struct *idle;
    u64 android_oem_data1[16];
};

static struct rq test_rq;
static struct cfs_rq cfs_a, cfs_b;
static struct task_struct ux_task, outgoing, idle_task, sentinel;
static struct oplus_task_struct ux_ots;
static int global_sched_assist_enabled = 1;
static int selected_count, throttled_count, rejected_count;
static int removed_count, put_count;

static struct oplus_rq *test_orq(void) { return (void *)test_rq.android_oem_data1; }
static struct task_struct *ots_to_ts(struct oplus_task_struct *ots) { return ots->task; }
static struct cfs_rq *cfs_rq_of(struct sched_entity *se) { return se->cfs_rq; }
static int cpu_of(struct rq *rq) { return rq->cpu; }
static int task_cpu(struct task_struct *task) { return task->cpu; }
static struct rq *task_rq(struct task_struct *task) { return task->rq; }
static bool test_task_is_fair(struct task_struct *task) { return task->fair; }
static bool task_on_rq_queued(struct task_struct *task) { return task->queued; }
static bool task_on_rq_migrating(struct task_struct *task) { return task->migrating; }
static struct oplus_task_struct *get_oplus_task_struct(struct task_struct *task) { return task->ots; }
static bool test_task_ux(struct task_struct *task) { return task->ux; }

static struct rb_node *rb_first_cached(struct rb_root_cached *root) { return root->first; }
static struct rb_node *rb_next(struct rb_node *node) { return node->next; }
#define rb_entry(pointer, type, member) container_of(pointer, type, member)

#define spin_lock_irqsave(lock, flags) do { assert((lock) == test_orq()->ux_list_lock); (flags) = 0; } while (0)
#define spin_unlock_irqrestore(lock, flags) do { (void)(lock); (void)(flags); } while (0)

static void update_ux_timeline_task_removal(struct oplus_rq *rq, struct oplus_task_struct *ots)
{
    assert(rq == test_orq() && ots == &ux_ots);
    removed_count++;
}

static void put_task_struct(struct task_struct *task)
{
    assert(task == &ux_task);
    put_count++;
}

static void sailboat_coloros_ux_note(int counter)
{
    if (counter == SAILBOAT_UX_SELECTED) selected_count++;
    else if (counter == SAILBOAT_UX_REJECTED) rejected_count++;
    else { assert(counter == SAILBOAT_UX_THROTTLED); throttled_count++; }
}

#include "extracted_ux_pick.h"

static void reset(void)
{
    memset(&test_rq, 0, sizeof(test_rq));
    memset(&cfs_a, 0, sizeof(cfs_a));
    memset(&cfs_b, 0, sizeof(cfs_b));
    memset(&ux_task, 0, sizeof(ux_task));
    memset(&outgoing, 0, sizeof(outgoing));
    memset(&idle_task, 0, sizeof(idle_task));
    memset(&sentinel, 0, sizeof(sentinel));
    memset(&ux_ots, 0, sizeof(ux_ots));
    global_sched_assist_enabled = 1;
    selected_count = throttled_count = rejected_count = 0;
    removed_count = put_count = 0;
    test_rq.cpu = 3;
    test_rq.curr = &outgoing;
    test_rq.idle = &idle_task;
    test_orq()->ux_list_lock = &test_orq()->lock;
    test_orq()->ux_list.first = &ux_ots.ux_entry;
    test_orq()->nr_running = 1;
    ux_task.fair = true;
    outgoing.fair = true;
    ux_task.cpu = 3;
    ux_task.rq = &test_rq;
    ux_task.ots = &ux_ots;
    ux_task.queued = true;
    ux_task.ux = true;
    ux_task.se.on_rq = 1;
    ux_task.se.cfs_rq = &cfs_a;
    ux_ots.task = &ux_task;
    outgoing.cpu = 3;
    outgoing.rq = &test_rq;
    outgoing.se.cfs_rq = &cfs_a;
    idle_task.cpu = 3;
    idle_task.rq = &test_rq;
}

static void pick_once(struct task_struct *prev, struct task_struct **picked, bool *repick)
{
    struct sched_entity *entity = NULL;

    *picked = &sentinel;
    *repick = false;
    sailboat_coloros_ux_pick(&test_rq, prev, picked, &entity, repick);
    if (*repick)
        assert(*picked == &ux_task && entity == &ux_task.se);
}

int main(void)
{
    struct task_struct *picked;
    bool repick;

    reset();
    pick_once(&outgoing, &picked, &repick);
    assert(picked == &ux_task && repick && selected_count == 1);
    assert(!throttled_count && !rejected_count && !removed_count && !put_count);

    reset();
    ux_task.se.cfs_rq = &cfs_b;
    pick_once(&outgoing, &picked, &repick);
    assert(picked == &sentinel && !repick && throttled_count == 1);
    assert(!selected_count && !rejected_count && !removed_count && !put_count);

    reset();
    test_rq.curr = &idle_task;
    idle_task.se.cfs_rq = NULL;
    ux_task.se.cfs_rq = &cfs_b;
    pick_once(&outgoing, &picked, &repick);
    assert(picked == &sentinel && !repick && throttled_count == 1);
    assert(!selected_count && !rejected_count && !removed_count && !put_count);

    reset();
    test_rq.curr = &outgoing;
    idle_task.se.cfs_rq = NULL;
    ux_task.se.cfs_rq = &cfs_b;
    pick_once(&idle_task, &picked, &repick);
    assert(picked == &ux_task && repick && selected_count == 1);

    reset();
    pick_once(NULL, &picked, &repick);
    assert(picked == &sentinel && !repick && !selected_count && !removed_count);

    reset();
    outgoing.se.cfs_rq = NULL;
    pick_once(&outgoing, &picked, &repick);
    assert(picked == &sentinel && !repick && !selected_count && !removed_count);

    reset();
    test_rq.curr = &idle_task;
    pick_once(&outgoing, &picked, &repick);
    assert(picked == &ux_task && repick && selected_count == 1);

    reset();
    ux_task.se.on_rq = 0;
    pick_once(&outgoing, &picked, &repick);
    assert(picked == &sentinel && !repick && throttled_count == 1);
    assert(!removed_count && !put_count);

    reset();
    ux_task.queued = false;
    pick_once(&outgoing, &picked, &repick);
    assert(picked == &sentinel && !repick && rejected_count == 1);
    assert(removed_count == 1 && put_count == 1 && !throttled_count);

    reset();
    ux_task.cpu = 4;
    pick_once(&outgoing, &picked, &repick);
    assert(rejected_count == 1 && removed_count == 1 && put_count == 1);

    reset();
    global_sched_assist_enabled = 0;
    pick_once(&outgoing, &picked, &repick);
    assert(picked == &sentinel && !repick);
    assert(!selected_count && !throttled_count && !rejected_count && !removed_count);

    puts("UX pick tests: same-cfs select, prev-based cross-cfs skip, idle/null-prev/null-cfs reject, hierarchy skip, stale removal, disabled gate passed (host scheduler stubs)");
    return 0;
}
