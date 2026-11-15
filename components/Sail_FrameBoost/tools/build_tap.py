#!/usr/bin/env python3
import argparse
import hashlib
import json
import pathlib
import subprocess

comp = pathlib.Path(__file__).resolve().parents[1]
root = comp.parents[1]
tools = comp / 'tools'
parser = argparse.ArgumentParser()
parser.add_argument('--out', type=pathlib.Path, default=root / 'frameboost/dist')
args = parser.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)

command = [
    'clang', '--target=aarch64-linux-android34', '-fuse-ld=lld', '-nostdlib',
    '-std=c11', '-Os', '-Wall', '-Wextra', '-Werror', '-fPIE', '-fno-ident',
    '-mbranch-protection=pac-ret+bti', '-fstack-protector-strong',
    '-Wl,-pie,--dynamic-linker=/system/bin/linker64,-e,_start,-z,relro,-z,now,'
    '-z,noexecstack,--hash-style=gnu,--no-undefined,--build-id=sha1',
    '-I', str(comp / 'src'),
    str(tools / 'uinput_tap.c'), str(tools / 'uinput_tap_start.S'),
    '-llog', '-lc', '-o', str(out / 'uinput_tap'),
]
subprocess.run(command, check=True, cwd='/')
subprocess.run(['llvm-strip', '--strip-unneeded', '--remove-section=.comment',
                str(out / 'uinput_tap')], check=True, cwd='/')
elf = subprocess.check_output(['llvm-readelf', '-h', '-l', '-d', str(out / 'uinput_tap')], text=True)
assert 'AArch64' in elf and 'PIE' in elf and 'GNU_RELRO' in elf and 'GNU_STACK' in elf
assert '/system/bin/linker64' in elf

artifact = out / 'uinput_tap'
report = {
    'kind': 'native AArch64 PIE executable',
    'size': artifact.stat().st_size,
    'sha256': hashlib.sha256(artifact.read_bytes()).hexdigest(),
    'device_name': 'goodix_test',
    'trigger': 'echo > /data/local/tmp/tapfifo',
    'compile_command': command,
}
(out / 'build_tap.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
print('BUILT', artifact.name, report['size'], report['sha256'], flush=True)
