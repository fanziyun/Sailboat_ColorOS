#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import re


HOOKS = {
    'binder.h': {
        'android_vh_binder_restore_priority': 'struct binder_transaction *t, struct task_struct *task',
        'android_vh_binder_wait_for_work': 'bool do_proc_work, struct binder_thread *tsk, struct binder_proc *proc',
        'android_vh_sync_txn_recvd': 'struct task_struct *tsk, struct task_struct *from',
    },
    'sched.h': {
        'android_vh_sched_stat_runtime_rt': 'struct task_struct *tsk, u64 delta',
        'android_rvh_flush_task': 'struct task_struct *prev',
        'android_rvh_sched_fork': 'struct task_struct *p',
        'android_rvh_try_to_wake_up': 'struct task_struct *p',
        'android_rvh_new_task_stats': 'struct task_struct *p',
        'android_rvh_schedule': 'struct task_struct *prev, struct task_struct *next, struct rq *rq',
        'android_rvh_select_task_rq_fair': 'struct task_struct *p, int prev_cpu, int sd_flag, int wake_flags, int *new_cpu',
        'android_rvh_can_migrate_task': 'struct task_struct *p, int dst_cpu, int *can_migrate',
        'android_rvh_replace_next_task_fair': 'struct rq *rq, struct task_struct **p, struct sched_entity **se, bool *repick, bool simple, struct task_struct *prev',
    },
}


def declaration(text, name):
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    matches = re.findall(r'\bDECLARE_(?:RESTRICTED_)?HOOK\(\s*' + re.escape(name) +
                         r'\s*,\s*TP_PROTO\(([^()]*)\)', text, flags=re.S)
    if len(matches) != 1:
        raise ValueError(f'{name}: expected one hook declaration, found {len(matches)}')
    return matches[0]


def types_only(proto):
    result = []
    for parameter in proto.split(','):
        parameter = re.sub(r'\b[A-Za-z_]\w*\s*$', '', parameter.strip())
        result.append(re.sub(r'\s+', '', parameter))
    return result


def config_values(text):
    return dict(re.findall(r'^CONFIG_(\w+)=(.*)$', text, flags=re.M))


def audit(source, output, config):
    errors = []
    files = {}
    hooks = []
    for header, expected in HOOKS.items():
        path = source / 'include/trace/hooks' / header
        if not path.is_file():
            errors.append(f'missing {path}')
            continue
        data = path.read_bytes()
        files[str(path)] = hashlib.sha256(data).hexdigest()
        for name, proto in expected.items():
            try:
                actual = declaration(data.decode(), name)
                if types_only(actual) != types_only(proto):
                    raise ValueError(f'{name}: incompatible TP_PROTO: {actual.strip()}')
                hooks.append(name)
            except ValueError as error:
                errors.append(str(error))
    values = config_values(config.read_text())
    for flag in ('SMP', 'PROC_FS', 'ARM64', 'ANDROID_VENDOR_HOOKS'):
        if values.get(flag) != 'y':
            errors.append(f'requires CONFIG_{flag}=y')
    symbols_path = output / 'Module.symvers'
    exports = set()
    if symbols_path.is_file():
        for line in symbols_path.read_text().splitlines():
            fields = line.split()
            if len(fields) >= 2:
                exports.add(fields[1])
        files[str(symbols_path)] = hashlib.sha256(symbols_path.read_bytes()).hexdigest()
    else:
        errors.append(f'missing {symbols_path}')
    missing_exports = sorted(name for name in hooks if '__tracepoint_' + name not in exports)
    if missing_exports:
        errors.append('missing hook exports: ' + ', '.join(missing_exports))
    return {
        'source': str(source), 'kernel_output': str(output),
        'config_sha256': hashlib.sha256(config.read_bytes()).hexdigest(),
        'source_file_sha256': files, 'checked_hook_prototypes': hooks,
        'errors': errors, 'hook_contract_passed': not errors,
        'config': {name: values.get(name, 'n') for name in (
            'OPLUS_SCHED_ASSIST', 'OPLUS_FEATURE_FRAME_BOOST', 'SCHED_WALT',
            'FAIR_GROUP_SCHED', 'CFI_CLANG', 'LTO_CLANG_THIN', 'TRIM_UNUSED_KSYMS')},
        'limitations': [
            'Hook declaration/export checks are not a FrameBoost module build or runtime test.',
            'WALT module presence is separate from CONFIG_SCHED_WALT in the GKI config.',
            'Placement, governor util consumption, ioctl semantics and lifecycle remain separate contracts.',
            'Does not authorize loading against an existing sched_assist provider.'],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--kernel-output', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.report.exists():
        parser.error('use a fresh report path')
    result = audit(args.source, args.kernel_output, args.kernel_output / '.config')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('Hook prototype/export contract:', 'PASS' if not result['errors'] else 'FAIL')
    for error in result['errors']:
        print(error)
    print('Config:', json.dumps(result['config']))
    print('Report:', args.report)
    raise SystemExit(bool(result['errors']))


if __name__ == '__main__':
    main()
