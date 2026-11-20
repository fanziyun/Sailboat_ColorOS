#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include "frame_ioctl.h"

#define __user
#define DEFINE_MUTEX(name) int name
struct file { int unused; };
struct task_struct { int tgid; };
static struct task_struct task = { .tgid = 123 };
static struct task_struct *current = &task;
static bool compat, fail_copy, active = true, mutate;
static int gate_calls, dispatch_calls, note_calls, adopt_calls;
static struct ofb_ctrl_data input;
static union {
    struct ofb_ctrl_cluster cluster;
    struct ofb_key_thread_info key_threads;
    struct ofb_frame_util_info frame_util;
} extra_input;
static int sailboat_coloros_fbg_ioctl_lock;
static bool in_compat_syscall(void) { return compat; }
static unsigned long copy_from_user(void *destination, const void *source, size_t size)
{
    if (fail_copy || !source) return size;
    memcpy(destination, source, size);
    return 0;
}
static unsigned long copy_to_user(void *destination, const void *source, size_t size)
{
    if (!destination || !source) return size;
    memcpy(destination, source, size);
    return 0;
}
static void mutex_lock(int *lock) { assert(lock == &sailboat_coloros_fbg_ioctl_lock); }
static void mutex_unlock(int *lock) { assert(lock == &sailboat_coloros_fbg_ioctl_lock); }
static int sailboat_coloros_fbg_ioctl_gate(bool privileged)
{
    (void)privileged;
    gate_calls++;
    if (mutate) { input.stage = 218; input.vsyncNs = 0; }
    return active ? 0 : -EAGAIN;
}
static void sailboat_coloros_fbg_note_ioctl(unsigned int command, int stage, bool privileged)
{
    (void)command; (void)stage; (void)privileged;
    note_calls++;
}
static void sailboat_coloros_fbg_adopt_frame(int pid, int tid)
{
    assert(pid == 123 && tid == 123);
    adopt_calls++;
}
#include "extracted_fbg_ioctl.h"

static long native(struct file *file, unsigned int command, unsigned long argument)
{
    (void)file;
    if (command == CMD_ID_BOOST_HIT) {
        struct ofb_ctrl_data *snapshot = (void *)argument;
        assert(snapshot != &input);
        assert(snapshot->pid == 123);
    } else if (command == CMD_ID_SET_TASK_PREFERED_CLUSTER) {
        struct ofb_ctrl_cluster *snapshot = (void *)argument;
        assert(snapshot != &extra_input.cluster && snapshot->tid == 42 && snapshot->cluster_id == 1);
    } else if (command == CMD_ID_ADD_TASK_TO_GROUP) {
        struct ofb_key_thread_info *snapshot = (void *)argument;
        assert(snapshot != &extra_input.key_threads && snapshot->thread_num == 2 && snapshot->add == 1);
    } else if (command == CMD_ID_NOTIFY_FRAME_START) {
        struct ofb_frame_util_info *snapshot = (void *)argument;
        assert(snapshot != &extra_input.frame_util);
        snapshot->frame_scale = 321;
        snapshot->frame_busy = 76;
    } else {
        assert(0 && "unexpected ioctl command");
    }
    dispatch_calls++;
    return command == CMD_ID_NOTIFY_FRAME_START ? 0 : 17;
}
static long call_with(unsigned int command, bool privileged, void *argument)
{
    return sailboat_coloros_fbg_dispatch(NULL, command, (unsigned long)argument, privileged, native);
}
static long call(unsigned int command, bool privileged)
{
    return call_with(command, privileged, &input);
}
static void reset(void)
{
    memset(&input, 0, sizeof(input));
    memset(&extra_input, 0, sizeof(extra_input));
    input.pid = 123;
    input.tid = 123;
    input.stage = BOOST_FRAME_START;
    input.vsyncNs = 8333333;
    active = true;
    compat = fail_copy = mutate = false;
    gate_calls = dispatch_calls = note_calls = adopt_calls = 0;
}
int main(void)
{
    reset();
    assert(call(CMD_ID_BOOST_HIT, false) == 17 && dispatch_calls == 1 && note_calls == 1);
    assert(adopt_calls == 1);
    reset(); input.stage = BOOST_OBTAIN_VIEW;
    assert(call(CMD_ID_BOOST_HIT, false) == 17 && !adopt_calls);
    reset(); input.stage = BOOST_MOVE_FG;
    assert(call(CMD_ID_BOOST_HIT, true) == 17 && !adopt_calls);
    reset(); mutate = true;
    assert(call(CMD_ID_BOOST_HIT, false) == 17 && input.stage == 218);
    reset(); active = false;
    assert(call(CMD_ID_BOOST_HIT, false) == -EAGAIN && !dispatch_calls && note_calls == 1);
    reset(); compat = true;
    assert(call(CMD_ID_BOOST_HIT, false) == -EOPNOTSUPP && !gate_calls);
    reset(); fail_copy = true;
    assert(call(CMD_ID_BOOST_HIT, false) == -EFAULT && !dispatch_calls);
    reset(); input.pid = 456;
    assert(call(CMD_ID_BOOST_HIT, false) == -EPERM && !gate_calls);
    reset(); input.stage = 218;
    assert(call(CMD_ID_BOOST_HIT, false) == -EOPNOTSUPP && !dispatch_calls);
    reset(); input.vsyncNs = 4294967296LL;
    assert(call(CMD_ID_SET_FPS, false) == -ERANGE && !dispatch_calls);
    reset(); input.stage = BOOST_ADD_FRAME_TASK; input.related_depth = 5;
    assert(call(CMD_ID_BOOST_HIT, true) == -ERANGE && !dispatch_calls);
    reset();
    assert(call(CMD_ID_BOOST_HIT ^ (1U << 16), false) == -EOPNOTSUPP && !dispatch_calls);
    reset();
    assert(call_with(CMD_ID_SET_TASK_PREFERED_CLUSTER, false, &extra_input.cluster) == -EPERM);
    assert(call_with(CMD_ID_SET_TASK_PREFERED_CLUSTER, true, &extra_input.cluster) == -ERANGE);
    reset(); extra_input.cluster.tid = 42; extra_input.cluster.cluster_id = 1;
    assert(call_with(CMD_ID_SET_TASK_PREFERED_CLUSTER, true, &extra_input.cluster) == 17);
    reset(); extra_input.key_threads.thread_num = MAX_KEY_THREAD_NUM + 1;
    assert(call_with(CMD_ID_ADD_TASK_TO_GROUP, true, &extra_input.key_threads) == -ERANGE);
    reset(); extra_input.key_threads.thread_num = 2; extra_input.key_threads.add = 1;
    assert(call_with(CMD_ID_ADD_TASK_TO_GROUP, true, &extra_input.key_threads) == 17);
    reset();
    assert(call_with(CMD_ID_NOTIFY_FRAME_START, true, &extra_input.frame_util) == 0);
    assert(extra_input.frame_util.frame_scale == 321 && extra_input.frame_util.frame_busy == 76);
    puts("FrameBoost real ioctl dispatch: snapshot/TOCTOU, identity, standard and extra ABI, compat, disabled and range gates passed");
    return 0;
}
