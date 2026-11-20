#!/usr/bin/env python3
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


probe = load('animation_probe_prepare', 'prepare_frame_boost_probe.py')
ux = probe.ux
ROOT = Path(__file__).resolve().parents[1]


def prepare(upstream, base, destination, builtin):
    stamp = json.loads((base / ux.STAMP).read_text())
    inputs = {}
    for path in base.iterdir():
        if path.suffix not in ('.c', '.h') or path.name.endswith('.mod.c'):
            continue
        data = path.read_bytes()
        expected = stamp['output_sha256'].get(path.name)
        if expected:
            if ux.digest(data) != expected:
                raise ValueError('modified SA input: ' + path.name)
        elif path.name != 'frame_group.h':
            original = subprocess.check_output(['git', '-C', str(upstream), 'show',
                ux.UPSTREAM_SHA + ':vendor/oplus/kernel/cpu/sched/sched_assist/' + path.name])
            if data.replace(b'\r\n', b'\n') != original.replace(b'\r\n', b'\n'):
                raise ValueError('modified unchanged SA input: ' + path.name)
        inputs[path.name] = data
    probe.prepare(upstream, base, destination)
    native_names = {path.name for path in destination.iterdir() if path.suffix in ('.c', '.h')}
    for name, data in inputs.items():
        if name not in native_names:
            (destination / name).write_bytes(data)
    texts = {path.name: path.read_text() for path in destination.iterdir() if path.suffix in ('.c', '.h')}
    for name in ('frame_group.c', 'frame_ioctl_V1.c', 'ua_ioctl_common.c', 'frame_sysctl.c',
                 'sched_assist.c', 'sailboat_coloros_ux.c', 'sa_sysfs.c'):
        texts[name] = '#include "sailboat_coloros_fbg.h"\n' + texts[name]

    text = texts['sa_sysfs.c']
    text = ux.sub(text, '\tif (!strncmp(in_str[0], "fg", 2)) {\n\t\tsave_top_app_tgid = pid;',
        '\tif (!strncmp(in_str[0], "fg", 2)) {\n\t\tif (pid && pid != save_top_app_tgid)\n'
        '\t\t\tsailboat_coloros_fbg_launch_boost();\n\t\tsave_top_app_tgid = pid;')
    texts['sa_sysfs.c'] = text

    text = texts['frame_boost.c']
    text = ux.sub(text, 'static int __init oplus_frame_boost_init(void)',
                  'int sailboat_coloros_fbg_native_init(void)')
    text = ux.sub(text, 'module_init(oplus_frame_boost_init);', '')
    text = ux.body(text, 'sailboat_coloros_fbg_native_init', '''\tint ret;

\tret = frame_info_init();
\tif (ret)
\t\treturn ret;
\tret = frame_group_init();
\tif (ret)
\t\treturn ret;
\tstune_boost[BOOST_ED_TASK_MID_DURATION] = 60;
\tstune_boost[BOOST_ED_TASK_MAX_DURATION] = 80;
\tstune_boost[BOOST_ED_TASK_TIME_OUT_DURATION] = 200;
\tstune_boost[BOOST_ED_TASK_MID_UTIL] = 600;
\tstune_boost[BOOST_ED_TASK_MAX_UTIL] = 900;
\tstune_boost[BOOST_SF_FREQ_GPU] = 30;
\tstune_boost[BOOST_SF_MIGR_GPU] = 30;
\treturn 0;''')
    texts['frame_boost.c'] = text

    text = texts['frame_group.c']
    text = ux.sub(text, 'return likely(sysctl_frame_boost_enable);',
                  'return sailboat_coloros_fbg_active();')
    text = ux.sub(text, 'void register_frame_group_vendor_hooks(void)',
                  'int sailboat_coloros_fbg_native_hooks(void)')
    hooks = [('android_vh_binder_restore_priority', 'fbg_binder_restore_priority_hook'),
             ('android_vh_binder_wait_for_work', 'fbg_binder_wait_for_work_hook'),
             ('android_vh_sync_txn_recvd', 'fbg_sync_txn_recvd_hook'),
             ('sched_stat_runtime', 'fbg_update_cfs_util_hook'),
             ('android_vh_sched_stat_runtime_rt', 'fbg_update_rt_util_hook'),
             ('android_rvh_flush_task', 'fbg_flush_task_hook'),
             ('android_rvh_sched_fork', 'fbg_sched_fork_hook'),
             ('android_rvh_try_to_wake_up', 'fbg_try_to_wake_up'),
             ('android_rvh_new_task_stats', 'fbg_new_task_stats'),
             ('sched_switch', 'fbg_update_freq_hook'), ('suspend_resume', 'fbg_update_suspend_resume')]
    body = '\tint ret;\n\n'
    for name, function in hooks:
        body += f'\tret = register_trace_{name}({function}, NULL);\n\tif (ret)\n\t\treturn ret;\n'
    text = ux.body(text, 'sailboat_coloros_fbg_native_hooks', body + '\treturn 0;')
    text = ux.sub(text, 'BUG_ON(!cluster);', 'if (!cluster)\n\t\treturn NULL;')
    text = ux.sub(text, 'GFP_ATOMIC);\n\tif (!cluster)', 'GFP_KERNEL);\n\tif (!cluster)')
    text = ux.sub(text, 'static inline void add_cluster(', 'static inline bool add_cluster(')
    text = ux.in_function(text, 'add_cluster', '\t\t\treturn;', '\t\t\treturn true;')
    text = ux.in_function(text, 'add_cluster', '\tcluster = alloc_new_cluster(cpus);',
        '\tif (num_sched_clusters >= MAX_CLS_NUM)\n\t\treturn false;\n\tcluster = alloc_new_cluster(cpus);\n\tif (!cluster)\n\t\treturn false;')
    text = ux.in_function(text, 'add_cluster', '\tnum_sched_clusters++;',
                         '\tnum_sched_clusters++;\n\treturn true;')
    text = ux.sub(text, '\t\tadd_cluster(&cluster_cpus, &new_head);',
        '\t\tif (!add_cluster(&cluster_cpus, &new_head)) {\n\t\t\tcleanup_clusters(&new_head);\n\t\t\treturn false;\n\t\t}')
    text = ux.body(text, 'get_possible_siblings', '''\tint cpu;
\tunsigned long capacity = arch_scale_cpu_capacity(cpuid);

\tfor_each_possible_cpu(cpu)
\t\tif (arch_scale_cpu_capacity(cpu) == capacity)
\t\t\tcpumask_set_cpu(cpu, cluster_cpus);''')
    text = ux.in_function(text, 'assign_cluster_ids', '\tlist_for_each_entry(cluster, head, list)\n\t\tcluster->id = pos++;',
        '\tlist_for_each_entry(cluster, head, list) {\n\t\tcluster->id = pos;\n\t\tfb_cluster[pos++] = cluster;\n\t}')
    text = ux.in_function(text, 'set_render_thread',
        '\trender = find_task_by_vpid(tid);',
        '\trender = find_task_by_vpid(tid);\n\tif (render && render->tgid != pid)\n\t\trender = NULL;')
    text = ux.in_function(text, 'fbg_skip_migration',
        '\tif (!group_task_fits_cluster_cpus(tsk, cluster))\n\t\treturn false;',
        '\tif (!group_task_fits_cluster_cpus(tsk, cluster))\n\t\treturn false;\n\tif (!cpumask_intersects(&cluster->cpus, tsk->cpus_ptr))\n\t\treturn false;')
    texts['frame_group.c'] = text
    texts['frame_group.h'] = ux.sub(texts['frame_group.h'],
        'void register_frame_group_vendor_hooks(void);', 'int sailboat_coloros_fbg_native_hooks(void);')

    text = texts['frame_sysctl.c']
    for variable in ('sysctl_frame_boost_enable', 'sysctl_frame_boost_debug',
                     'sysctl_slide_boost_enabled', 'sysctl_input_boost_enabled'):
        text = ux.sub(text, 'unsigned int ' + variable + ';', 'int ' + variable + ';')
    text = ux.sub(text, 'void fbg_sysctl_init(void)', 'int sailboat_coloros_fbg_sysctl_init(void)')
    text = ux.sub(text, '.proc_handler\t= proc_dointvec,',
                  '.proc_handler\t= sailboat_coloros_fbg_enable_handler,') if text.count('.proc_handler\t= proc_dointvec,') == 1 else text.replace('.proc_handler\t= proc_dointvec,', '.proc_handler\t= sailboat_coloros_fbg_enable_handler,', 1)
    text = text.replace('.mode\t\t= 0666,', '.mode\t\t= 0644,')
    text = ux.sub(text, 'int sailboat_coloros_fbg_sysctl_init(void)',
                  'static struct ctl_table_header *sailboat_coloros_fbg_sysctl;\n\nint sailboat_coloros_fbg_sysctl_init(void)')
    text = ux.body(text, 'sailboat_coloros_fbg_sysctl_init', '''\tsysctl_frame_boost_enable = 1;
\tsysctl_frame_boost_debug = 0;
\tsysctl_slide_boost_enabled = 0;
\tsysctl_input_boost_enabled = 0;
\tib_last_time = ktime_get();
\tintput_boost_duration = INPUT_BOOST_DURATION;
\thrtimer_init(&ibtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
\tibtimer.function = &input_boost_timeout;
\tsailboat_coloros_fbg_sysctl = register_sysctl_table(fbg_base_table);
\treturn sailboat_coloros_fbg_sysctl ? 0 : -ENOMEM;''')
    text += '''
void sailboat_coloros_fbg_sysctl_cleanup(void)
{
\tif (!sailboat_coloros_fbg_sysctl)
\t\treturn;
\tunregister_sysctl_table(sailboat_coloros_fbg_sysctl);
\tsailboat_coloros_fbg_sysctl = NULL;
\thrtimer_cancel(&ibtimer);
}
'''
    texts['frame_sysctl.c'] = text

    text = texts['frame_ioctl_V1.c']
    for name, privileged in [('ofb_ioctl', 'false'), ('ofb_sys_ioctl', 'true')]:
        start, _, end = ux.span(text, name)
        original = text[start:end]
        changed = ux.sub(original, name + '(', name + '_native(')
        changed = changed.replace('copy_from_user(&data, uarg, sizeof(data))',
                                  'sailboat_coloros_fbg_copy(&data, uarg, sizeof(data))')
        changed = changed.replace('copy_from_user(&stune_data, uarg, sizeof(stune_data))',
                                  'sailboat_coloros_fbg_copy(&stune_data, uarg, sizeof(stune_data))')
        wrapper = f'''\n\nstatic long {name}(struct file *file, unsigned int cmd, unsigned long arg)
{{
\treturn sailboat_coloros_fbg_dispatch(file, cmd, arg, {privileged}, {name}_native);
}}'''
        text = text[:start] + changed + wrapper + text[end:]
    text = ux.body(text, 'proc_stune_boost_write', '\treturn -EOPNOTSUPP;')
    text = ux.body(text, 'frame_ioctl_init', '''\tstruct proc_dir_entry *entry;

\tframe_boost_proc = proc_mkdir(FRAMEBOOST_PROC_NODE, NULL);
\tif (!frame_boost_proc)
\t\treturn -ENOMEM;
\tentry = proc_create("ctrl", 0666, frame_boost_proc, &ofb_ctrl_fops);
\tif (!entry)
\t\tgoto fail;
\tentry = proc_create("sys_ctrl", 0660, frame_boost_proc, &ofb_sys_ctrl_fops);
\tif (!entry)
\t\tgoto fail;
\tproc_set_user(entry, GLOBAL_SYSTEM_UID, GLOBAL_SYSTEM_GID);
\tentry = proc_create("stune_boost", 0444, frame_boost_proc, &ofb_stune_boost_fops);
\tif (!entry)
\t\tgoto fail;
\tentry = proc_create("info", 0444, frame_boost_proc, &ofb_frame_group_info_fops);
\tif (!entry)
\t\tgoto fail;
\treturn 0;
fail:
\tremove_proc_subtree(FRAMEBOOST_PROC_NODE, NULL);
\tframe_boost_proc = NULL;
\treturn -ENOMEM;''')
    text = ux.body(text, 'frame_ioctl_exit', '''\tif (frame_boost_proc) {
\t\tremove_proc_subtree(FRAMEBOOST_PROC_NODE, NULL);
\t\tframe_boost_proc = NULL;
\t}
\treturn 0;''')
    text = ux.sub(text, 'static int fbg_set_task_preferred_cluster(void __user *uarg)',
                  'static int fbg_set_task_preferred_cluster(void *snapshot)')
    text = ux.body(text, 'fbg_set_task_preferred_cluster', '''	struct ofb_ctrl_cluster *data = snapshot;

	return __fbg_set_task_preferred_cluster(data->tid, data->cluster_id);''')
    text = ux.sub(text, 'static long fbg_add_task_to_group(void __user *uarg)',
                  'static long fbg_add_task_to_group(void *snapshot)')
    text = ux.body(text, 'fbg_add_task_to_group', '''	struct ofb_key_thread_info *info = snapshot;
	unsigned int i;

	for (i = 0; i < info->thread_num; i++)
		add_task_to_game_frame_group(info->tid[i], info->add);

	return 0;''')
    text = ux.sub(text, 'static long fbg_notify_frame_start(void __user *uarg)',
                  'static long fbg_notify_frame_start(struct ofb_frame_util_info *info)')
    text = ux.body(text, 'fbg_notify_frame_start', '''	set_frame_state(FRAME_START);
	rollover_frame_group_window(GAME_FRAME_GROUP_ID);
	get_frame_util_info(info);
	return 0;''')
    text = ux.sub(text, 'static long handle_ofb_extra_cmd(unsigned int cmd, void __user *uarg)',
                  'static long handle_ofb_extra_cmd(unsigned int cmd, void *snapshot)')
    text = ux.sub(text, 'return fbg_set_task_preferred_cluster(uarg);',
                  'return fbg_set_task_preferred_cluster(snapshot);')
    text = ux.sub(text, 'return fbg_add_task_to_group(uarg);',
                  'return fbg_add_task_to_group(snapshot);')
    text = ux.sub(text, 'return fbg_notify_frame_start(uarg);',
                  'return fbg_notify_frame_start(snapshot);')
    texts['frame_ioctl_V1.c'] = text

    text = texts['ua_ioctl_common.c']
    text = ux.sub(text, 'static int __init ioctl_common_init(void)', 'int sailboat_coloros_fbg_ua_init(void)')
    text = ux.sub(text, 'static void __exit ioctl_common_exit(void)', 'void sailboat_coloros_fbg_ua_cleanup(void)')
    text = ux.sub(text, 'module_init(ioctl_common_init);', '')
    text = ux.sub(text, 'module_exit(ioctl_common_exit);', '')
    text = ux.body(text, 'sailboat_coloros_fbg_ua_init', '''\tstruct proc_dir_entry *entry;
\tint ret;

\tua_common_proc = proc_mkdir(UA_PROC_NODE, NULL);
\tif (!ua_common_proc)
\t\treturn -ENOMEM;
\tentry = proc_create("ua_ctrl", 0660, ua_common_proc, &cpu_ioctrl_fops);
\tif (!entry) {
\t\tret = -ENOMEM;
\t\tgoto fail;
\t}
\tproc_set_user(entry, KUIDT_INIT(1000), KGIDT_INIT(1000));
\tret = frame_ioctl_init();
\tif (!ret)
\t\treturn 0;
fail:
\tremove_proc_subtree(UA_PROC_NODE, NULL);
\tua_common_proc = NULL;
\treturn ret;''')
    text = ux.body(text, 'sailboat_coloros_fbg_ua_cleanup', '''\tframe_ioctl_exit();
\tif (ua_common_proc) {
\t\tremove_proc_subtree(UA_PROC_NODE, NULL);
\t\tua_common_proc = NULL;
\t}''')
    text = ux.in_function(text, 'cpu_ioctl', '\tlong ret;',
        '\tlong ret = sailboat_coloros_fbg_ioctl_gate(true);\n\n\tif (ret)\n\t\treturn ret;')
    texts['ua_ioctl_common.c'] = text

    texts['sched_assist.c'] = ux.in_function(texts['sched_assist.c'], 'oplus_sched_assist_init',
        '\tupdate_ux_sched_cputopo();', '\tret = sailboat_coloros_fbg_prepare();\n\tif (ret)\n\t\tgoto fail;\n\tupdate_ux_sched_cputopo();')
    texts['sched_assist.c'] = ux.in_function(texts['sched_assist.c'], 'oplus_sched_assist_init',
        '\tpr_info("sailboat_coloros_ux_min: ready, runtime disabled\\n");',
        '\tsailboat_coloros_fbg_publish();\n'
        '\tif (sailboat_coloros_ux_set_enabled(1))\n'
        '\t\tpr_warn("sailboat_coloros_ux_min: default enable failed\\n");\n'
        '\tpr_info("sailboat_coloros_animation_v1: ready, default enabled\\n");')
    texts['sched_assist.c'] = ux.in_function(texts['sched_assist.c'], 'oplus_sched_assist_init',
        'fail:\n\treturn sailboat_coloros_ux_init_failed(ret);',
        'fail:\n\tsailboat_coloros_fbg_failed();\n\treturn sailboat_coloros_ux_init_failed(ret);')
    text = texts['sailboat_coloros_ux.c']
    text = ux.in_function(text, 'sailboat_coloros_ux_status_show',
        '\tint cpu, index;', '\tint cpu, index;\n\n\tsailboat_coloros_fbg_status(file);')
    texts['sailboat_coloros_ux.c'] = text
    for path in (ROOT / 'port/frame_boost/src').iterdir():
        if path.suffix in ('.c', '.h'):
            texts[path.name] = path.read_text()
    identity = ux.digest((json.dumps({name: ux.digest(text.encode()) for name, text in sorted(texts.items())},
                                    sort_keys=True)).encode())
    texts['sailboat_coloros_ux_build.h'] = (
        '/* SPDX-License-Identifier: GPL-2.0-only */\n#define SAILBOAT_COLOROS_UX_SOURCE_ID "' + identity + '"\n')
    fbg_objects = ['cluster_boost', 'frame_boost', 'frame_debug', 'frame_group', 'frame_info',
                   'frame_ioctl_V1', 'frame_sysctl', 'ua_ioctl_common',
                   'sailboat_coloros_fbg_backend', 'sailboat_coloros_fbg_ioctl']
    objects = ux.OBJECTS + fbg_objects
    obj = 'obj-$(CONFIG_OPLUS_SCHED_ASSIST)' if builtin else 'obj-m'
    texts['Makefile'] = (obj + ' += sailboat_sched_assist.o\nsailboat_sched_assist-y := ' +
        ' '.join(name + '.o' for name in objects) + '\n'
        'ccflags-y += -I$(srctree)/ -I$(srctree)/kernel/sched -I$(src)\n'
        'ccflags-y += -DCONFIG_OPLUS_FEATURE_FRAME_BOOST=1 -DCONFIG_OPLUS_FEATURE_SCHED_ASSIST=1\n')
    for name, text in texts.items():
        (destination / name).write_bytes(text.encode())
    manifest = {'profile': 'sailboat_coloros_animation_v1', 'upstream_sha': ux.UPSTREAM_SHA,
                'source_id': identity, 'base_source_id': stamp['source_id'],
                'default_enabled': True, 'hot_disable': False, 'builtin': builtin,
                'output_sha256': {name: ux.digest(text.encode()) for name, text in texts.items()}}
    (destination / 'sailboat_coloros_animation_profile.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Prepared animation profile (requires matching no-SA base):', destination)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--upstream', type=Path, required=True)
    parser.add_argument('--base-sa', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--builtin', action='store_true')
    args = parser.parse_args()
    prepare(args.upstream, args.base_sa, args.output, args.builtin)


if __name__ == '__main__':
    main()
