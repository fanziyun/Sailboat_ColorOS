#!/usr/bin/env python3
import argparse
import hashlib
import json
import pathlib
import re
import subprocess

UPSTREAM_SHA = '0241347436d48df011e2a6f6fe2d8740c26a379c'
OBJECTS = ['sched_assist', 'sa_common', 'sa_oemdata', 'sa_sysfs', 'sa_priority',
           'sa_fair', 'sa_exec', 'sa_jankinfo', 'sailboat_coloros_nodes', 'sailboat_coloros_ux']
STAMP = 'sailboat_coloros_ux_profile.json'


def verify_upstream(root):
    def git(*args, data=None):
        result = subprocess.run(['git', '-C', str(root), *args], input=data,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
        return result.stdout
    sha = git('rev-parse', 'HEAD').decode().strip()
    if sha != UPSTREAM_SHA:
        raise ValueError('upstream cache has a different commit')
    directory = 'vendor/oplus/kernel/cpu/sched/sched_assist/'
    frame = 'vendor/oplus/kernel/cpu/sched/frame_boost/frame_boost_V1/frame_group.h'
    records = git('ls-tree', '-r', '-z', sha, '--', directory, frame)
    paths = []
    for record in records.split(bytes([0])):
        if not record:
            continue
        metadata, name = record.split(b'\t', 1)
        path = name.decode()
        if metadata.split()[1] != b'blob':
            raise ValueError('unexpected upstream object type')
        if path == frame or (path.startswith(directory) and pathlib.PurePosixPath(path).suffix in ('.c', '.h')):
            paths.append(path)
    expected = set(paths)
    actual = {p.relative_to(root).as_posix() for p in (root / directory).iterdir()
              if p.suffix in ('.c', '.h')}
    actual.add(frame)
    if actual != expected or not paths:
        raise ValueError('upstream source file inventory differs from pinned tree')
    data = git('cat-file', '--batch', data=''.join(sha + ':' + p + '\n' for p in paths).encode())
    offset = 0
    for path in paths:
        end = data.index(b'\n', offset)
        header = data[offset:end].split()
        if len(header) != 3 or header[1] != b'blob':
            raise ValueError('invalid git object response')
        size = int(header[2])
        original = data[end + 1:end + 1 + size]
        working = (root / path).read_bytes()
        if original.replace(b'\r\n', b'\n') != working.replace(b'\r\n', b'\n'):
            raise ValueError('modified upstream source: ' + path)
        offset = end + 2 + size
    return sha


def digest(data):
    return hashlib.sha256(data).hexdigest()


def sub(text, old, new):
    count = text.count(old)
    if count != 1:
        raise ValueError(f'expected one anchor, got {count}: {old[:100]!r}')
    return text.replace(old, new, 1)


def span(text, name):
    matches = list(re.finditer(r'(?m)^[\w \t*]+\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', text))
    if len(matches) != 1:
        raise ValueError(f'{name}: expected one definition, got {len(matches)}')
    start = matches[0].end() - 1
    depth, state, index = 1, 'code', start + 1
    while index < len(text):
        ch, nxt = text[index], text[index:index + 2]
        if state == 'line':
            if ch == '\n':
                state = 'code'
        elif state == 'comment':
            if nxt == '*/':
                state, index = 'code', index + 1
        elif state in ('"', "'"):
            if ch == chr(92):
                index += 1
            elif ch == state:
                state = 'code'
        elif nxt in ('//', '/*'):
            state, index = ('line' if nxt == '//' else 'comment'), index + 1
        elif ch in ('"', "'"):
            state = ch
        elif ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
            if not depth:
                return matches[0].start(), start, index + 1
        index += 1
    raise ValueError(f'{name}: unbalanced definition')


def body(text, name, replacement):
    _, start, end = span(text, name)
    return text[:start] + '{\n' + replacement.rstrip() + '\n}' + text[end:]


def in_function(text, name, old, new):
    begin, _, end = span(text, name)
    return text[:begin] + sub(text[begin:end], old, new) + text[end:]


def wrap_gate(text, name, signature, system_only):
    begin, _, end = span(text, name)
    original = text[begin:end]
    renamed = sub(original, name + '(', name + '_upstream(')
    wrapper = '\n\n' + signature + '\n{\n\tint ret = sailboat_coloros_ux_write_gate(' + system_only + ');\n\n\tif (ret)\n\t\treturn ret;\n\treturn ' + name + '_upstream(file, buf, count, ppos);\n}'
    return text[:begin] + renamed + wrapper + text[end:]


def validate_kernel(kernel, config=None):
    hooks = (kernel / 'include/trace/hooks/sched.h').read_text()
    proto = re.search(r'DECLARE_RESTRICTED_HOOK\(android_rvh_replace_next_task_fair,\s*TP_PROTO\((.*?)\),', hooks, re.S)
    expected = 'struct rq *rq, struct task_struct **p, struct sched_entity **se, bool *repick, bool simple, struct task_struct *prev'
    if not proto or re.sub(r'\s+', '', proto[1]) != re.sub(r'\s+', '', expected):
        raise ValueError('unsupported replace_next_task_fair prototype')
    configs = [p for p in [kernel / '.config', kernel / 'include/generated/autoconf.h'] if p.exists()]
    if config is not None:
        if not config.is_file():
            raise ValueError('requested kernel config is missing')
        configs.append(config)
    forbidden = ['OPLUS_SYSTEM_KERNEL_QCOM', 'SCHED_WALT', 'LOCKING_PROTECT',
                 'OPLUS_FEATURE_FRAME_BOOST', 'OPLUS_FEATURE_LOADBALANCE',
                 'OPLUS_FEATURE_PIPELINE', 'OPLUS_CPU_AUDIO_PERF',
                 'OPLUS_FEATURE_SCHED_SPREAD', 'OPLUS_FEATURE_CPU_JANKINFO',
                 'OPLUS_FEATURE_FAKE_CAP', 'OPLUS_LOCKING_STRATEGY',
                 'OPLUS_FEATURE_AUDIO_OPT', 'OPLUS_CAMERA_UX']
    for path in configs:
        text = path.read_text()
        for flag in forbidden:
            if re.search(r'(?m)^(?:CONFIG_' + flag + r'=[ym]|#define CONFIG_' + flag + r'(?:_MODULE)? 1)$', text):
                raise ValueError(f'unsupported config: CONFIG_{flag}')


def adapt(out, port, kernel, source_sha, builtin, config=None):
    if source_sha != UPSTREAM_SHA:
        raise ValueError('upstream SHA does not match the audited snapshot')
    validate_kernel(kernel, config)
    adapter_hash = digest(pathlib.Path(__file__).read_bytes())
    port_hashes = {p.name: digest(p.read_bytes()) for p in sorted(port.iterdir()) if p.suffix in ('.c', '.h')}
    stamp_path = out / STAMP
    if stamp_path.exists():
        stamp = json.loads(stamp_path.read_text())
        if stamp['adapter_sha256'] != adapter_hash or stamp['port_sha256'] != port_hashes or stamp['builtin'] != builtin:
            raise ValueError('stale profile; use a fresh output directory')
        for name, wanted in stamp['output_sha256'].items():
            if digest((out / name).read_bytes()) != wanted:
                raise ValueError(f'modified adapted file: {name}')
        return stamp
    names = ['sched_assist.c', 'sa_common.c', 'sa_common.h', 'sa_fair.c',
             'sa_oemdata.c', 'sa_priority.c', 'sa_sysfs.c']
    texts = {name: (out / name).read_text(encoding='utf-8') for name in names}
    originals = {name: digest(text.encode()) for name, text in texts.items()}
    for name in ['sched_assist.c', 'sa_common.c', 'sa_fair.c', 'sa_oemdata.c', 'sa_priority.c', 'sa_sysfs.c']:
        texts[name] = sub(texts[name], '#include "sa_common.h"', '#include "sa_common.h"\n#include "sailboat_coloros_ux.h"') if '#include "sa_common.h"' in texts[name] else sub(texts[name], '#include "sa_sysfs.h"', '#include "sa_sysfs.h"\n#include "sailboat_coloros_ux.h"')
    for name in ['sa_common.c', 'sa_fair.c']:
        texts[name] = sub(texts[name], '#include <../kernel/oplus_cpu/sched/frame_boost/frame_group.h>', '#include "frame_group.h"')
    texts['sa_common.h'] = sub(texts['sa_common.h'], '#define OTS_IDX\t\t\t0', '#define OTS_IDX\t\t\t2')

    text = texts['sched_assist.c']
    text = sub(text, 'static void android_rvh_wake_up_new_task_handler', 'static __maybe_unused void android_rvh_wake_up_new_task_handler')
    hooks = [('android_rvh_sched_fork', 'android_rvh_sched_fork_handler'),
             ('android_rvh_enqueue_task', 'android_rvh_enqueue_task_handler'),
             ('android_rvh_dequeue_task', 'android_rvh_dequeue_task_handler'),
             ('android_rvh_check_preempt_tick', 'android_rvh_check_preempt_tick_handler'),
             ('android_rvh_check_preempt_wakeup', 'android_rvh_check_preempt_wakeup_handler'),
             ('android_rvh_replace_next_task_fair', 'android_rvh_replace_next_task_fair_handler'),
             ('sched_stat_runtime', 'android_vh_sched_stat_runtime_handler')]
    text = body(text, 'register_scheduler_vendor_hooks', '\tint ret;\n\n' + ''.join('\tREGISTER_TRACE_VH(' + h + ', ' + f + ');\n' for h, f in hooks) + '\treturn 0;')
    text = body(text, 'enable_sched_assist', '\t/* sailboat_coloros_ux_min: only the explicit root control may activate UX. */\n\t(void)step;')
    text = sub(text, 'typedef unsigned long (*profile_event_register_t)', 'typedef int (*profile_event_register_t)')
    text = body(text, 'oplus_sched_assist_init', '''\tint ret;
#ifdef MODULE
\treturn -EOPNOTSUPP;
#endif
\tOPLUS_OEM_DATA_SIZE_TEST(struct oplus_rq, struct rq);
\tret = sailboat_coloros_ux_prepare();
\tif (ret)
\t\tgoto fail;
\tsched_assist_init_oplus_rq();
\tret = sa_oemdata_init();
\tif (ret)
\t\tgoto fail;
\tupdate_ux_sched_cputopo();
\tret = register_scheduler_vendor_hooks();
\tif (ret)
\t\tgoto fail;
\tret = oplus_sched_assist_proc_init();
\tif (ret)
\t\tgoto fail;
\tret = sailboat_coloros_ux_publish_ready();
\tif (ret)
\t\tgoto fail;
\tpr_info("sailboat_coloros_ux_min: ready, runtime disabled\\n");
\treturn 0;
fail:
\tpr_err("sailboat_coloros_ux_min: init failed %d, runtime disabled\\n", ret);
\treturn ret;''')
    texts['sched_assist.c'] = text

    text = texts['sa_common.c']
    text = in_function(text, 'sched_assist_init_oplus_rq', 'orq->ux_list_lock = kmalloc(sizeof(spinlock_t), GFP_KERNEL);', 'orq->ux_list_lock = sailboat_coloros_ux_rq_lock(cpu);')
    text = in_function(text, 'oplus_set_ux_state_lock', '\tif (need_lock_rq)\n\t\trq = task_rq_lock(t, &flags);', '\tif (!READ_ONCE(global_sched_assist_enabled))\n\t\treturn;\n\n\tif (need_lock_rq)\n\t\trq = task_rq_lock(t, &flags);')
    text = in_function(text, 'oplus_set_ux_state_lock', '\tots = get_oplus_task_struct(t);', '\tots = sailboat_coloros_ux_ensure_ots(t);')
    text = in_function(text, 'oplus_set_ux_state_lock', '\tif (ux_state == ots->ux_state)\n\t\tgoto out;', '\tif (ux_state == ots->ux_state)\n\t\tgoto out;\n\tsailboat_coloros_ux_note(SAILBOAT_UX_SET);')
    text = in_function(text, 'android_rvh_sched_fork_handler', '\tinit_task_ux_info(p);', '\tif (sailboat_coloros_ux_ensure_ots(p))\n\t\tinit_task_ux_info(p);')
    text = in_function(text, 'android_rvh_enqueue_task_handler', '\tqueue_ux_thread(rq, p, 1);', '\tif (READ_ONCE(global_sched_assist_enabled) && !get_oplus_task_struct(p))\n\t\tsailboat_coloros_ux_ensure_ots(p);\n\tqueue_ux_thread(rq, p, 1);')
    once_block = '''\t\tif (ots->ux_state & SA_TYPE_ONCE) {
\t\t\tatomic64_set(&ots->inherit_ux, 0);
\t\t\tots->ux_depth = 0;
\t\t\tots->ux_state = 0;
\t\t\tif (unlikely(global_debug_enabled & DEBUG_FTRACE))
\t\t\t\ttrace_printk("dequeue once task=%-12s pid=%d inherit_start=%llu\\n",
\t\t\t\t\tp->comm, p->pid, ots->inherit_ux_start);
\t\t}
'''
    text = in_function(text, 'dequeue_ux_thread', once_block, '')
    text = in_function(text, 'dequeue_ux_thread', '\tif (p->__state != TASK_RUNNING) {', '\tif (ots->ux_state & SA_TYPE_ONCE) {\n\t\tatomic64_set(&ots->inherit_ux, 0);\n\t\tots->ux_depth = 0;\n\t\tots->ux_state = 0;\n\t}\n\tif (p->__state != TASK_RUNNING) {')
    text = in_function(text, 'test_task_ux', '(ots->total_exec > limit)', '(ots->total_exec >= limit)')
    texts['sa_common.c'] = text
    texts['sa_common.c'] = in_function(texts['sa_common.c'], 'set_im_flag_with_bit', '\tif (im_flag < 0)', '\tif (!sailboat_coloros_ux_imflag_valid(im_flag, IM_FLAG_CLEAR, BITS_PER_LONG))')
    texts['sa_fair.c'] = body(texts['sa_fair.c'], 'oplus_replace_next_task_fair', '\tsailboat_coloros_ux_pick(rq, p, se, repick);')

    text = texts['sa_oemdata.c']
    anchor = 'static inline struct oplus_task_struct *alloc_oplus_task_struct_node(int node)'
    text = sub(text, anchor, 'static void init_oplus_task_struct(void *ptr);\n\n' + anchor)
    text = body(text, 'alloc_oplus_task_struct_node', '''\tstruct oplus_task_struct *ots;

\tots = kmem_cache_alloc(oplus_task_struct_cachep, GFP_ATOMIC);
\tif (ots)
\t\tinit_oplus_task_struct(ots);
\telse
\t\tsailboat_coloros_ux_note(SAILBOAT_UX_ALLOC_FAILED);
\treturn ots;''')
    _, _, allocator_end = span(text, 'alloc_oplus_task_struct_node')
    text = text[:allocator_end] + '''

struct oplus_task_struct *sailboat_coloros_ux_ensure_ots(struct task_struct *task)
{
\tstruct oplus_task_struct *ots, *previous;

\tots = get_oplus_task_struct(task);
\tif (ots)
\t\treturn ots;
\tots = alloc_oplus_task_struct_node(NUMA_NO_NODE);
\tif (!ots)
\t\treturn NULL;
\tots->task = task;
\tprevious = (struct oplus_task_struct *)cmpxchg(
\t\t&task->android_oem_data1[OTS_IDX], 0, (u64)ots);
\tif (previous) {
\t\tkmem_cache_free(oplus_task_struct_cachep, ots);
\t\treturn IS_ERR(previous) ? NULL : previous;
\t}
\treturn ots;
}
''' + text[allocator_end:]
    text = body(text, 'android_vh_dup_task_struct_handler', '\tif (tsk && orig)\n\t\tsailboat_coloros_ux_ensure_ots(tsk);')
    node_begin, _, node_end = span(text, 'oplus_tsk_fork_get_node')
    text = text[:node_begin] + text[node_end:]
    text = sub(text, '/* called from kernel_clone() to get node information for about to be created task */\n', '')
    text = body(text, 'alloc_ots_mem_for_all_threads', '''\tstruct task_struct *group, *task;
\tint cpu;

\tread_lock(&tasklist_lock);
\tfor_each_process_thread(group, task)
\t\tsailboat_coloros_ux_ensure_ots(task);
\tfor_each_possible_cpu(cpu)
\t\tsailboat_coloros_ux_ensure_ots(cpu_rq(cpu)->idle);
\tread_unlock(&tasklist_lock);''')
    text = body(text, 'register_oemdata_hooks', '''\tint ret;

\tret = register_trace_android_vh_free_task(android_vh_free_task_handler, NULL);
\tif (ret)
\t\treturn ret;
\tret = register_trace_android_vh_dup_task_struct(android_vh_dup_task_struct_handler, NULL);
\tif (ret) {
\t\tunregister_trace_android_vh_free_task(android_vh_free_task_handler, NULL);
\t\ttracepoint_synchronize_unregister();
\t}
\treturn ret;''')
    text = in_function(text, 'android_vh_free_task_handler', '\tWRITE_ONCE(tsk->android_oem_data1[OTS_IDX], 0);', '\tif (WARN_ON_ONCE(!oplus_rbnode_empty(&ots->ux_entry) ||\n\t\t\t !oplus_rbnode_empty(&ots->exec_time_node)))\n\t\treturn;\n\tWRITE_ONCE(tsk->android_oem_data1[OTS_IDX], 0);')
    text = body(text, 'sa_oemdata_init', '''\tint ret;

\toplus_task_struct_cachep = kmem_cache_create("oplus_task_struct",
\t\tsizeof(struct oplus_task_struct), 0, SLAB_ACCOUNT, NULL);
\tif (!oplus_task_struct_cachep)
\t\treturn -ENOMEM;
\tret = register_oemdata_hooks();
\tif (ret) {
\t\tkmem_cache_destroy(oplus_task_struct_cachep);
\t\toplus_task_struct_cachep = NULL;
\t\treturn ret;
\t}
\talloc_ots_mem_for_all_threads();
\treturn 0;''')
    texts['sa_oemdata.c'] = text

    text = texts['sa_priority.c']
    text = in_function(text, 'insert_task_to_ux_timeline', '\torq->nr_running++;', '\torq->nr_running++;\n\tsailboat_coloros_ux_note(SAILBOAT_UX_ENQUEUE);')
    text = in_function(text, 'update_ux_timeline_task_removal', '\trb_erase_cached(&ots->exec_time_node, &orq->exec_timeline);', '\trb_erase_cached(&ots->exec_time_node, &orq->exec_timeline);\n\tRB_CLEAR_NODE(&ots->exec_time_node);\n\tsailboat_coloros_ux_note(SAILBOAT_UX_DEQUEUE);')
    text = in_function(text, 'android_vh_sched_stat_runtime_handler', '\trq = task_rq(task);', '\tif (!READ_ONCE(global_sched_assist_enabled))\n\t\treturn;\n\trq = task_rq(task);')
    text = in_function(text, 'android_vh_sched_stat_runtime_handler', '\t\tots->total_exec += delta;', '\t\tsailboat_coloros_ux_note(SAILBOAT_UX_RUNTIME);\n\t\tots->total_exec += delta;')
    text = in_function(text, 'android_vh_sched_stat_runtime_handler', '\t\tif (ots->total_exec >= limit) {', '\t\tif (ots->total_exec >= limit) {\n\t\t\tsailboat_coloros_ux_note(SAILBOAT_UX_EXPIRED);')
    texts['sa_priority.c'] = text

    text = texts['sa_sysfs.c']
    text = sub(text, '#include "sa_sysfs.h"', '#include "sa_sysfs.h"\n#include "sa_priority.h"\n#include "sailboat_coloros_nodes.h"')
    allowed = 'SCHED_ASSIST_UX_MASK | SA_TYPE_ONCE | SA_OPT_SET | SA_OPT_RESET | SA_OPT_SET_PRIORITY | SCHED_ASSIST_UX_PRIORITY_MASK'
    native_check = '\tif (!sailboat_coloros_ux_value_valid(ux_value, ' + allowed + ', SCHED_ASSIST_UX_PRIORITY_SHIFT, PRIORITY_LEVEL_NUM))\n\t\treturn -EINVAL;\n'
    text_check = '\tif (!sailboat_coloros_ux_value_valid(ux_state, ' + allowed + ', SCHED_ASSIST_UX_PRIORITY_SHIFT, PRIORITY_LEVEL_NUM))\n\t\treturn -EINVAL;\n\tif (ux_state & SA_OPT_RESET)\n\t\treturn -EOPNOTSUPP;\n'
    text = in_function(text, 'proc_sched_assist_enabled_write', '\tglobal_sched_assist_enabled = val;', '\terr = sailboat_coloros_ux_set_enabled(val);\n\tif (err)\n\t\treturn err;')
    text = in_function(text, 'write_task_ux', '\trcu_read_lock();', native_check + '\tret = sailboat_coloros_ux_write_gate(fromSysOrApp);\n\tif (ret)\n\t\treturn ret;\n\trcu_read_lock();')
    text = body(text, 'proc_lowend_plat_opt_write', '\treturn -EOPNOTSUPP;')
    text = in_function(text, 'proc_ux_task_write', '\tmutex_lock(&sa_ux_mutex);', text_check + '\tif (pid <= 0 || pid > PID_MAX_LIMIT || ux_state < 0 ||\n\t    strcmp(opt_str[OPT_STR_TYPE], "p"))\n\t\treturn -EINVAL;\n\terr = sailboat_coloros_ux_write_gate(true);\n\tif (err)\n\t\treturn err;\n\tmutex_lock(&sa_ux_mutex);')
    text = in_function(text, 'proc_ux_task_write', '\t\treturn -EFAULT;\n\t}', '\t\treturn -EINVAL;\n\t}')
    text = in_function(text, 'proc_ux_task_write', '\t\t\tif (pid > 0 && pid <= PID_MAX_DEFAULT)\n\t\t\t\tglobal_ux_task_pid = pid;', '\t\t\tif (pid <= 0 || pid > PID_MAX_LIMIT)\n\t\t\t\treturn -EINVAL;\n\t\t\tglobal_ux_task_pid = pid;\n\t\t\treturn count;')
    text = in_function(text, 'proc_ux_task_write', '\t\t\tif (ux_task) {\n\t\t\t\tux_orig', '\t\t\terr = ux_task ? 0 : -ESRCH;\n\t\t\tif (ux_task) {\n\t\t\t\tux_orig')
    text = in_function(text, 'proc_ux_task_write', '\treturn count;\n}', '\treturn err ? err : count;\n}')
    text = in_function(text, 'proc_ux_task_write', 'pid <= PID_MAX_DEFAULT', 'pid <= PID_MAX_LIMIT')
    text = in_function(text, 'proc_ux_task_write', '\t\t\t\tux_orig = oplus_get_ux_state(ux_task);', '''\t\t\t\tstruct rq_flags flags;
\t\t\t\tstruct rq *rq = task_rq_lock(ux_task, &flags);

\t\t\t\tif (!sailboat_coloros_ux_ensure_ots(ux_task)) {
\t\t\t\t\ttask_rq_unlock(rq, ux_task, &flags);
\t\t\t\t\tput_task_struct(ux_task);
\t\t\t\t\terr = -ENOMEM;
\t\t\t\t\tgoto out_ux;
\t\t\t\t}
\t\t\t\tux_orig = oplus_get_ux_state(ux_task);''')
    text = in_function(text, 'proc_ux_task_write', 'clear_all_inherit_type(ux_task);', 'sailboat_coloros_ux_clear_inherit_locked(ux_task);')
    begin, _, end = span(text, 'proc_ux_task_write')
    fragment = text[begin:end]
    setter = 'oplus_set_ux_state_lock(ux_task, ux_orig, true);'
    if fragment.count(setter) != 3:
        raise ValueError('unexpected text UX setter count')
    text = text[:begin] + fragment.replace(setter, 'oplus_set_ux_state_lock(ux_task, ux_orig, false);') + text[end:]
    text = in_function(text, 'proc_ux_task_write', '\t\t\t\tput_task_struct(ux_task);\n\t\t\t}', '\t\t\t\ttask_rq_unlock(rq, ux_task, &flags);\n\t\t\t\tput_task_struct(ux_task);\n\t\t\t}')
    text = in_function(text, 'proc_ux_task_write', '\tmutex_unlock(&sa_ux_mutex);', 'out_ux:\n\tmutex_unlock(&sa_ux_mutex);')
    text = in_function(text, 'write_task_ux', '\tstruct task_struct *ux_task, *task;', '\tstruct task_struct *ux_task, *task;\n\tstruct rq *rq;\n\tstruct rq_flags flags;')
    text = in_function(text, 'write_task_ux', '''\t\t\tstruct oplus_task_struct *ots;
\t\t\tots = get_oplus_task_struct(task);
\t\t\tif (IS_ERR_OR_NULL(ots)) {
\t\t\t\tret = -EFAULT;
\t\t\t}  else {''', '\t\t\t{')
    text = in_function(text, 'write_task_ux', '\t\t\t\t\tux_orig = ots->ux_state;\n', '')
    text = in_function(text, 'write_task_ux', '\t\tint ux_state = -1;', '''\t\tint ux_state = -1;

\t\trq = task_rq_lock(ux_task, &flags);
\t\tif (!sailboat_coloros_ux_ensure_ots(ux_task)) {
\t\t\ttask_rq_unlock(rq, ux_task, &flags);
\t\t\tput_task_struct(ux_task);
\t\t\treturn -ENOMEM;
\t\t}
\t\tux_orig = oplus_get_ux_state(ux_task);''')
    text = in_function(text, 'write_task_ux', 'clear_all_inherit_type(ux_task);', 'sailboat_coloros_ux_clear_inherit_locked(ux_task);')
    text = in_function(text, 'write_task_ux', 'oplus_set_ux_state_lock(ux_task, ux_state, true);', 'oplus_set_ux_state_lock(ux_task, ux_state, false);')
    text = in_function(text, 'write_task_ux', '\t\tput_task_struct(ux_task);\n\t\tret = ux_state;', '\t\ttask_rq_unlock(rq, ux_task, &flags);\n\t\tput_task_struct(ux_task);\n\t\tret = need_update ? ux_state : ux_orig;')
    text = in_function(text, 'im_flag_set_handle', 'struct oplus_task_struct *ots = get_oplus_task_struct(task);', 'struct oplus_task_struct *ots = sailboat_coloros_ux_ensure_ots(task);')
    text = in_function(text, 'im_flag_set_handle', '\tif (IS_ERR_OR_NULL(ots))\n\t\treturn 0;', '\tif (IS_ERR_OR_NULL(ots))\n\t\treturn -ENOMEM;')
    text = in_function(text, 'im_flag_set_handle', '\t\tint ux_state = oplus_get_ux_state(task);', '\t\tstruct rq_flags flags;\n\t\tstruct rq *rq = task_rq_lock(task, &flags);\n\t\tint ux_state = oplus_get_ux_state(task);')
    text = in_function(text, 'im_flag_set_handle', 'oplus_set_ux_state_lock(task, ux_state | SA_TYPE_HEAVY, true);', 'oplus_set_ux_state_lock(task, ux_state | SA_TYPE_HEAVY, false);\n\t\ttask_rq_unlock(rq, task, &flags);')
    for flag_writer in ('proc_im_flag_write', 'proc_im_flag_app_write'):
        text = in_function(text, flag_writer, '\tmutex_lock(&sa_im_mutex);', '\tif (!sailboat_coloros_ux_imflag_valid(im_flag, IM_FLAG_CLEAR, BITS_PER_LONG))\n\t\treturn -EINVAL;\n\tmutex_lock(&sa_im_mutex);')
    for name, privileged in [('proc_debug_enabled_write', 'true'), ('proc_im_flag_write', 'true'), ('proc_im_flag_app_write', 'false'), ('proc_sched_assist_scene_write', 'true'), ('proc_sched_impt_task_write', 'true')]:
        signature = 'static ssize_t ' + name + '(struct file *file, const char __user *buf,\n\t\tsize_t count, loff_t *ppos)'
        text = wrap_gate(text, name, signature, privileged)
    text = in_function(text, 'oplus_sched_assist_proc_init', '\tgoto err_creat_d_sched_assist;\n\t}\n', '\tgoto err_creat_d_sched_assist;\n\t}\n\n\tif (sailboat_coloros_nodes_proc_init(d_sched_assist))\n\t\tgoto err_creat_d_sched_assist;\n')
    texts['sa_sysfs.c'] = text

    for path in port.iterdir():
        if path.suffix in ('.c', '.h'):
            texts[path.name] = path.read_text(encoding='utf-8')
    obj = 'obj-$(CONFIG_OPLUS_SCHED_ASSIST)' if builtin else 'obj-m'
    target = 'sailboat_sched_assist' if builtin else 'sail_sched_assist'
    texts['Makefile'] = obj + ' += ' + target + '.o\n' + target + '-y := ' + ' '.join(name + '.o' for name in OBJECTS) + '\nccflags-y += -I$(srctree)/ -I$(srctree)/kernel/sched -I$(src)\n'
    outputs = {name: digest(text.encode()) for name, text in texts.items()}
    stamp = {'profile': 'sailboat_coloros_ux_min', 'upstream_sha': source_sha,
             'adapter_sha256': adapter_hash, 'port_sha256': port_hashes, 'builtin': builtin,
             'original_sha256': originals, 'output_sha256': outputs}
    for name, text in texts.items():
        (out / name).write_text(text, encoding='utf-8')
    stamp_path.write_text(json.dumps(stamp, indent=2) + '\n', encoding='utf-8')
    return stamp


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--verify-upstream', type=pathlib.Path)
    parser.add_argument('--out-dir', type=pathlib.Path)
    parser.add_argument('--port-dir', type=pathlib.Path)
    parser.add_argument('--kernel-dir', type=pathlib.Path)
    parser.add_argument('--source-sha')
    parser.add_argument('--builtin', action='store_true')
    parser.add_argument('--config', type=pathlib.Path)
    args = parser.parse_args()
    try:
        if args.verify_upstream is not None:
            print(verify_upstream(args.verify_upstream))
            return
        if any(value is None for value in (args.out_dir, args.port_dir, args.kernel_dir, args.source_sha)):
            parser.error('adaptation requires out-dir, port-dir, kernel-dir and source-sha')
        result = adapt(args.out_dir, args.port_dir, args.kernel_dir, args.source_sha, args.builtin, args.config)
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
    print(result['profile'] + ': adapted ' + result['upstream_sha'])


if __name__ == '__main__':
    main()
