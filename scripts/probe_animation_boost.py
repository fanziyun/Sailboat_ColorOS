#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shlex
import subprocess


PROPERTIES = ('vendor.boostfwk.version', 'ro.build.fingerprint',
              'ro.vendor.build.fingerprint')
INTERFACES = ('/proc/oplus_scheduler/sched_assist', '/proc/oplus_frame_boost',
              '/proc/oplus_cpu', '/proc/sys/fbg')
ROOTS = ('/system/', '/system_ext/', '/odm/', '/vendor/')
INTEREST = re.compile(r'oplus|uifirst|osense|frameboost|surfaceflinger|libgui|performance', re.I)
ELF_STRINGS = re.compile(rb'[\x20-\x7e]{5,}')
ABI_STRING = re.compile(r'oplus_frame_boost|oplus_cpu|/proc/.*(?:ux|fbg)|'
                        r'frame_boost|enableAnimationCtrl|boostfwk|uifirst|osense', re.I)


def consumer_pids(text):
    result = []
    for line in text.splitlines():
        fields = line.split()
        if len(fields) < 2 or not fields[0].isdecimal():
            continue
        name = fields[-1]
        if (name in ('surfaceflinger', 'system_server') or
                re.search(r'oplus.*(?:performance|uifirst|osense)', name, re.I)):
            result.append((int(fields[0]), name))
    return sorted(set(result))


def mapped_files(text):
    result = set()
    for line in text.splitlines():
        fields = line.split(maxsplit=5)
        if len(fields) != 6:
            continue
        path = fields[5]
        if (path.startswith(ROOTS) and '..' not in PurePosixPath(path).parts and
                re.fullmatch(r'/[A-Za-z0-9_./@+:-]+', path) and INTEREST.search(path)):
            result.add(path)
    return sorted(result)


def abi_strings(data):
    return [{'offset': match.start(), 'text': match[0].decode('ascii')}
            for match in ELF_STRINGS.finditer(data) if ABI_STRING.search(match[0].decode('ascii'))]


class Probe:
    def __init__(self, serial, destination):
        self.serial = serial
        self.destination = destination
        self.records = {}

    def shell(self, command):
        result = subprocess.run(['adb', '-s', self.serial, 'shell',
                                 'su -c ' + shlex.quote(command)],
                                capture_output=True, timeout=45)
        return {'returncode': result.returncode,
                'stdout': result.stdout.decode('utf-8', errors='replace'),
                'stderr': result.stderr.decode('utf-8', errors='replace')}

    def capture(self, name, command):
        record = self.shell(command)
        self.records[name] = record
        return record['stdout']

    def pull(self, remote, index):
        size = self.shell('stat -c %s ' + shlex.quote(remote))
        if size['returncode'] or not size['stdout'].strip().isdecimal():
            return {'remote': remote, 'error': 'cannot determine file size', 'stat': size}
        if not 0 < int(size['stdout'].strip()) <= 64 * 1024**2:
            return {'remote': remote, 'error': 'file size outside 1..64 MiB'}
        folder = self.destination / 'files' / f'{index:03d}'
        folder.mkdir(parents=True, exist_ok=False)
        local = folder / PurePosixPath(remote).name
        result = subprocess.run(['adb', '-s', self.serial, 'pull', remote, str(local)],
                                capture_output=True, timeout=90)
        if result.returncode:
            return {'remote': remote, 'error': 'adb pull failed',
                    'stderr': result.stderr.decode('utf-8', errors='replace')}
        data = local.read_bytes()
        return {'remote': remote, 'local': local.relative_to(self.destination).as_posix(),
                'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
                'elf': data.startswith(b'\x7fELF'), 'abi_strings': abi_strings(data)}

    def run(self, max_files):
        self.destination.mkdir(parents=True, exist_ok=False)
        try:
            self.capture('uname', 'uname -a')
            self.capture('modules', 'cat /proc/modules')
            self.capture('interfaces', 'ls -ld ' + ' '.join(INTERFACES))
            self.capture('scheduler_nodes', 'ls -l ' + INTERFACES[0])
            self.capture('ux_status', 'cat ' + INTERFACES[0] + '/ux_status')
            for prop in PROPERTIES:
                self.capture(prop, 'getprop ' + prop)
            self.capture('governors', 'cat /sys/devices/system/cpu/cpufreq/policy*/scaling_governor')
            processes = self.capture('processes', 'ps -A -o PID,NAME')
            paths = set()
            consumers = []
            for pid, name in consumer_pids(processes):
                maps = self.capture(f'maps_{pid}', f'cat /proc/{pid}/maps')
                paths.update(mapped_files(maps))
                self.capture(f'fds_{pid}', f'ls -l /proc/{pid}/fd')
                consumers.append({'pid': pid, 'name': name})
            selected = sorted(paths)[:max_files]
            binaries = [self.pull(path, index) for index, path in enumerate(selected)]
            report = {'serial': self.serial, 'read_only_device_probe': True,
                      'consumers': consumers, 'records': self.records, 'binaries': binaries,
                      'uncollected_paths': sorted(paths)[max_files:],
                      'limits': ['A string match is not proof of a live ioctl or animation event.',
                                 'Missing paths and empty properties remain missing; no service is restarted.',
                                 'No sysctl, module, tracing, SELinux or UI state was changed.']}
            (self.destination / 'report.json').write_text(
                json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
            print('Device kernel:', self.records['uname']['stdout'].strip())
            print('vendor.boostfwk.version:', repr(self.records['vendor.boostfwk.version']['stdout'].strip()))
            print('Consumers:', consumers)
            for binary in binaries:
                print(binary['remote'], 'strings=' + str(len(binary.get('abi_strings', []))),
                      binary.get('error', ''))
            print('Evidence:', self.destination / 'report.json')
        except BaseException:
            (self.destination / 'partial.json').write_text(
                json.dumps(self.records, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
            raise


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--serial', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--max-files', type=int, default=24)
    args = parser.parse_args()
    if not 0 <= args.max_files <= 64:
        parser.error('--max-files must be between 0 and 64')
    Probe(args.serial, args.output).run(args.max_files)


if __name__ == '__main__':
    main()
