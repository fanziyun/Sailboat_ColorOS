#!/usr/bin/env python3
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

SPEC = importlib.util.spec_from_file_location('ux_adapter', Path(__file__).with_name('adapt_sched_assist.py'))
ux = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ux)


def prepare(upstream, sa, destination):
    sha = subprocess.check_output(['git', '-C', str(upstream), 'rev-parse', 'HEAD'], text=True).strip()
    if sha != ux.UPSTREAM_SHA:
        raise ValueError('requires audited 5.15 upstream SHA')
    stamp = json.loads((sa / ux.STAMP).read_text())
    if stamp['upstream_sha'] != sha:
        raise ValueError('sched_assist source contract mismatch')
    headers = {}
    for name, expected in stamp['output_sha256'].items():
        path = sa / name
        if path.suffix == '.h':
            data = path.read_bytes()
            if ux.digest(data) != expected:
                raise ValueError('modified adapted sched_assist header: ' + name)
            headers[name] = data
    if b'#define OTS_IDX\t\t\t2' not in headers.get('sa_common.h', b''):
        raise ValueError('requires adapted OTS_IDX=2')
    files = {}
    base = upstream / 'vendor/oplus/kernel/cpu'
    sources = list((base / 'sched/frame_boost/frame_boost_V1').glob('*.[ch]'))
    sources.append(base / 'sched/sched_assist/sa_oemdata.h')
    sources += [base / 'uad/ua_ioctl' / name for name in (
        'frame_ioctl.h', 'ua_ioctl_common.h', 'ua_ioctl_common.c', 'touch_ioctl.h')]
    sources.append(base / 'uad/ua_ioctl/frame_boost/frame_ioctl_V1.c')
    hashes = {}
    for path in sources:
        data = path.read_bytes()
        relative = path.relative_to(upstream).as_posix()
        committed = subprocess.check_output(['git', '-C', str(upstream), 'show', sha + ':' + relative])
        if data.replace(b'\r\n', b'\n') != committed.replace(b'\r\n', b'\n'):
            raise ValueError('modified upstream input: ' + relative)
        hashes[relative] = hashlib.sha256(committed).hexdigest()
        text = committed.decode('utf-8')
        for old, new in (
            ('<../kernel/oplus_cpu/sched/sched_assist/sa_common.h>', '"sa_common.h"'),
            ('<../sched/sched_assist/sa_common.h>', '"sa_common.h"'),
            ('<../sched/frame_boost/frame_boost.h>', '"frame_boost.h"'),
            ('<../sched/frame_boost/frame_info.h>', '"frame_info.h"'),
            ('"../frame_ioctl.h"', '"frame_ioctl.h"'),
            ('"../ua_ioctl_common.h"', '"ua_ioctl_common.h"'),
            ('#include <../fs/proc/internal.h>\n', ''),
        ):
            if old in text:
                text = ux.sub(text, old, new)
        if path.name == 'frame_ioctl_V1.c':
            text = ux.sub(text,
                '\t\tpentry->uid = GLOBAL_SYSTEM_UID;\n\t\tpentry->gid = GLOBAL_SYSTEM_GID;',
                '\t\tproc_set_user(pentry, GLOBAL_SYSTEM_UID, GLOBAL_SYSTEM_GID);')
        files[path.name] = text.encode('utf-8')
    destination.mkdir(parents=True, exist_ok=False)
    for name, data in headers.items():
        if name not in files:
            (destination / name).write_bytes(data)
    for name, data in files.items():
        (destination / name).write_bytes(data)
    objects = sorted(Path(name).stem + '.o' for name in files if name.endswith('.c'))
    (destination / 'Makefile').write_text(
        'obj-y := ' + ' '.join(objects) + '\n'
        'ccflags-y += -I$(srctree)/ -I$(srctree)/kernel/sched -I$(src)\n'
        'ccflags-y += -DCONFIG_OPLUS_FEATURE_FRAME_BOOST=1 -DCONFIG_OPLUS_FEATURE_SCHED_ASSIST=1\n',
        encoding='utf-8')
    manifest = {'profile': 'sailboat_coloros_fbg_compile_probe', 'upstream_sha': sha,
                'compile_only': True, 'module_loadable': False,
                'input_sha256': hashes, 'objects': objects,
                'limitations': [
                    'Archive-only compile probe. No ko, setup injection or KSU package is generated.',
                    'Feature macros are local compiler inputs, not changes to the target kernel config.',
                    'Includes and public proc_set_user only; runtime/lifecycle bugs are not adapted.',
                    'No proof of linkability, callback ownership, governor integration or ROM consumption.']}
    (destination / 'sailboat_coloros_fbg_probe.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Prepared compile-only probe:', destination)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--upstream', type=Path, required=True)
    parser.add_argument('--sched-assist', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.upstream, args.sched_assist, args.output)


if __name__ == '__main__':
    main()
