#!/usr/bin/env python3
import argparse
import hashlib
import json
import pathlib
import subprocess

comp = pathlib.Path(__file__).resolve().parents[1]
root = comp.parents[1]
src = comp / 'src'
parser = argparse.ArgumentParser()
parser.add_argument('--out', type=pathlib.Path, default=root / 'frameboost/dist')
parser.add_argument('--debug', action='store_true')
parser.add_argument('--no-filter', action='store_true')
parser.add_argument('--hold-ms', type=int, default=1500)
parser.add_argument('--touch-name', default='goodix_ts')
args = parser.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)

command = [
    'clang', '--target=aarch64-linux-android34', '-fuse-ld=lld', '-nostdlib',
    '-std=c11', '-Os', '-Wall', '-Wextra', '-Werror', '-fPIE', '-fno-ident',
    '-mbranch-protection=pac-ret+bti', '-fstack-protector-strong',
    '-Wl,-pie,--dynamic-linker=/system/bin/linker64,-e,_start,-z,relro,-z,now,'
    '-z,noexecstack,--hash-style=gnu,--no-undefined,--build-id=sha1',
    '-DFB_HOLD_MS=%d' % args.hold_ms,
    '-DFB_TOUCH_PREFIX="%s"' % args.touch_name,
    str(src / 'main.c'), str(src / 'start.S'),
    '-llog', '-lc', '-o', str(out / 'Sail_FrameBoost'),
]
if args.debug:
    command.insert(command.index('-std=c11'), '-DFB_DEBUG')
if args.no_filter:
    command.insert(command.index('-std=c11'), '-DFB_NO_FILTER')
subprocess.run(command, check=True, cwd='/')
subprocess.run(['llvm-strip', '--strip-unneeded', '--remove-section=.comment',
                str(out / 'Sail_FrameBoost')], check=True, cwd='/')
elf = subprocess.check_output(['llvm-readelf', '-h', '-l', '-d', str(out / 'Sail_FrameBoost')], text=True)
assert 'AArch64' in elf and 'PIE' in elf and 'GNU_RELRO' in elf and 'GNU_STACK' in elf
assert '/system/bin/linker64' in elf

artifact = out / 'Sail_FrameBoost'
report = {
    'kind': 'native AArch64 PIE executable',
    'size': artifact.stat().st_size,
    'sha256': hashlib.sha256(artifact.read_bytes()).hexdigest(),
    'sysfs': '/sys/class/kgsl/kgsl-3d0/min_pwrlevel',
    'touch_device_name': args.touch_name,
    'hold_ms': args.hold_ms,
    'floor_high': '0 (680MHz)',
    'filter': 'none' if args.no_filter else 'launcher-only',
    'compile_command': command,
    'platform_execution_verified': False,
}
(out / 'build.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
print('BUILT', artifact.name, report['size'], report['sha256'], flush=True)
