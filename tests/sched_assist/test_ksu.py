import hashlib
import importlib.util
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('package_ux', ROOT / 'scripts/package_ux.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)
RELEASE = '5.15.225-Sailboat-ColorOS-UXmodBase-test'
SOURCE_ID = 'a' * 64


def module_fixture():
    names = b'\0.shstrtab\0.modinfo\0__versions\0.strtab\0'
    info = (f'name=sailboat_sched_assist\0vermagic={RELEASE} SMP modversions aarch64\0'
            f'sailboat_ux_source_id={SOURCE_ID}\0').encode()
    parts = [b'', names, info, struct.pack('<Q56s', 1, b'module_layout') * 2, b'\0__cfi_check\0']
    data = bytearray(64)
    sections = []
    for index, part in enumerate(parts):
        name = ['', '.shstrtab', '.modinfo', '__versions', '.strtab'][index]
        offset = len(data)
        data.extend(part)
        sections.append((names.index(name.encode()) if name else 0, 1 if index else 0, 0, 0,
                         offset, len(part), 0, 0, 1, 0))
    section_offset = len(data)
    for section in sections:
        data.extend(struct.pack('<IIQQQQIIQQ', *section))
    data[:64] = struct.pack('<16sHHIQQQIHHHHHH', b'\x7fELF\x02\x01\x01' + bytes(9),
                            1, 183, 1, 0, 0, section_offset, 0, 64, 0, 0, 64, len(parts), 1)
    return bytes(data)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = pathlib.Path(self.temp.name)
        self.ko = self.path / 'ux.ko'
        self.ko.write_bytes(module_fixture())
        self.report = dict(schema=1, target_build_verified=True, release=RELEASE,
                           module_sha256=hashlib.sha256(self.ko.read_bytes()).hexdigest(),
                           notes_sha256='b' * 64, source_id=SOURCE_ID, sa_config='m', crc_count=2)
        self.proof = self.path / 'target.json'
        self.proof.write_text(json.dumps(self.report))

    def tearDown(self):
        self.temp.cleanup()

    def test_deterministic_pure_ux_package(self):
        first, second = self.path / 'first.zip', self.path / 'second.zip'
        result = packager.package(self.ko, self.proof, first)
        packager.package(self.ko, self.proof, second)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        self.assertFalse(result['device_tested'])
        self.assertFalse(result['hot_unload'])
        with zipfile.ZipFile(first) as archive:
            self.assertIn('lib/modules/sailboat_sched_assist.ko', archive.namelist())
            self.assertNotIn('boost.sh', archive.namelist())
            self.assertNotIn('enable_ux_on_boot', archive.namelist())
            self.assertEqual(archive.read('service.sh'), (ROOT / 'ksu/Sail_UX/service.sh').read_bytes())
            self.assertIn(b'target_build_verified=1', archive.read('ux.manifest'))

    def test_unverified_mismatched_and_builtin_rejected(self):
        for key, value in [('target_build_verified', False), ('module_sha256', '0' * 64),
                           ('source_id', 'f' * 64), ('sa_config', 'y'), ('crc_count', 0),
                           ('release', '5.15.202-sdk')]:
            report = dict(self.report, **{key: value})
            self.proof.write_text(json.dumps(report))
            with self.assertRaises(ValueError):
                packager.package(self.ko, self.proof, self.path / 'invalid.zip')
            self.assertFalse((self.path / 'invalid.zip').exists())


@unittest.skipIf(os.name == 'nt', 'Android-shell loader behavior runs under WSL sh')
class LoaderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = pathlib.Path(self.temp.name)
        self.module = self.path / 'Sail_UX'
        shutil.copytree(ROOT / 'ksu/Sail_UX', self.module)
        self.fs = self.path / 'fs'
        self.proc = self.fs / 'proc/oplus_scheduler/sched_assist'
        self.param = self.fs / 'sys/module/sailboat_sched_assist/parameters'
        notes = self.fs / 'sys/kernel/notes'
        notes.parent.mkdir(parents=True)
        notes.write_bytes(b'matching-build-notes')
        boot = self.fs / 'proc/sys/kernel/random/boot_id'
        boot.parent.mkdir(parents=True)
        boot.write_text('abcdef-1234\n')
        ko = self.module / 'lib/modules/sailboat_sched_assist.ko'
        ko.parent.mkdir(parents=True)
        ko.write_bytes(b'loader-fixture-not-a-kernel-module')
        manifest = (f'format=1\ntarget_release={RELEASE}\nko_sha256={hashlib.sha256(ko.read_bytes()).hexdigest()}\n'
                    f'notes_sha256={hashlib.sha256(notes.read_bytes()).hexdigest()}\n'
                    f'source_id={SOURCE_ID}\ntarget_build_verified=1\n')
        (self.module / 'ux.manifest').write_text(manifest)
        script = self.module / 'uxctl.sh'
        script.write_text(re.sub(r'/(?:proc|sys)/', lambda match: str(self.fs) + match[0], script.read_text()))
        commands = self.path / 'bin'
        commands.mkdir()
        self.command(commands, 'id', "printf '0\\n'")
        self.command(commands, 'uname', 'if [ "$1" = -m ]; then printf "aarch64\\n"; else printf "%s\\n" "$UX_TEST_RELEASE"; fi')
        self.command(commands, 'insmod', '''printf 'attempt\n' >> "$UX_TEST_PATH/attempts"
[ "${UX_TEST_FAIL_LOAD:-0}" != 1 ] || exit 1
p="$UX_TEST_PATH/fs/sys/module/sailboat_sched_assist/parameters"
r="$UX_TEST_PATH/fs/proc/oplus_scheduler/sched_assist"
mkdir -p "$p" "$r"
printf '%s\n' "${UX_TEST_INIT_STATE:-1}" > "$p/init_state"
printf '%s\n' "${UX_TEST_ERRNO:-0}" > "$p/init_errno"
printf '%s\n' "$UX_TEST_SOURCE_ID" > "$p/source_id"
printf '5\n' > "$p/init_stage"
printf 'profile=sailboat_coloros_ux_min ready=1 active=0 hot_disable=0\n' > "$r/ux_status"
printf 'enabled=0\n' > "$r/sched_assist_enabled"
''')
        self.env = dict(os.environ, PATH=str(commands) + ':' + os.environ['PATH'],
                        UX_TEST_PATH=str(self.path), UX_TEST_RELEASE=RELEASE, UX_TEST_SOURCE_ID=SOURCE_ID)

    def command(self, directory, name, body):
        path = directory / name
        path.write_text('#!/bin/sh\n' + body + '\n')
        path.chmod(0o755)

    def tearDown(self):
        self.temp.cleanup()

    def run_control(self, command):
        return subprocess.run(['sh', str(self.module / 'uxctl.sh'), command], env=self.env,
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def attempted(self):
        return (self.path / 'attempts').exists()

    def test_matching_base_loads_off_and_requires_explicit_enable(self):
        result = self.run_control('load')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.proc / 'sched_assist_enabled').read_text(), 'enabled=0\n')
        self.assertNotEqual(self.run_control('load').returncode, 0)
        self.assertEqual((self.path / 'attempts').read_text().splitlines(), ['attempt'])
        self.assertEqual(self.run_control('status').returncode, 0)

    def test_wrong_kernel_or_notes_rejected_before_load(self):
        self.env['UX_TEST_RELEASE'] = '5.15.225-old-builtin'
        self.assertNotEqual(self.run_control('load').returncode, 0)
        self.assertFalse(self.attempted())
        self.env['UX_TEST_RELEASE'] = RELEASE
        (self.fs / 'sys/kernel/notes').write_bytes(b'not-the-base')
        self.assertNotEqual(self.run_control('load').returncode, 0)
        self.assertFalse(self.attempted())

    def test_existing_builtin_and_disabled_package_rejected(self):
        self.proc.mkdir(parents=True)
        self.assertNotEqual(self.run_control('load').returncode, 0)
        self.assertFalse(self.attempted())
        self.proc.rmdir()
        (self.module / 'disable').touch()
        self.assertNotEqual(self.run_control('load').returncode, 0)
        self.assertFalse(self.attempted())

    def test_failed_init_is_not_success_and_no_retry(self):
        self.env.update(UX_TEST_INIT_STATE='-1', UX_TEST_ERRNO='-12')
        result = self.run_control('load')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('FAILED', result.stdout)
        self.assertNotEqual(self.run_control('enable').returncode, 0)
        self.assertEqual((self.proc / 'sched_assist_enabled').read_text(), 'enabled=0\n')
        self.assertNotEqual(self.run_control('load').returncode, 0)
        self.assertEqual((self.path / 'attempts').read_text().splitlines(), ['attempt'])

    def test_insmod_error_attempted_only_once(self):
        self.env['UX_TEST_FAIL_LOAD'] = '1'
        self.assertNotEqual(self.run_control('load').returncode, 0)
        self.assertNotEqual(self.run_control('load').returncode, 0)
        self.assertEqual((self.path / 'attempts').read_text().splitlines(), ['attempt'])

    def test_updated_package_cannot_control_old_running_module(self):
        self.assertEqual(self.run_control('load').returncode, 0)
        (self.param / 'source_id').write_text('f' * 64 + '\n')
        self.assertNotEqual(self.run_control('enable').returncode, 0)
        self.assertNotEqual(self.run_control('arm').returncode, 0)
        self.assertFalse((self.module / 'enable_ux_on_boot').exists())

    def test_manifest_is_data_not_shell_and_tampering_rejected(self):
        path = self.module / 'ux.manifest'
        path.write_text(path.read_text() + 'execute=touch planted\n')
        self.assertNotEqual(self.run_control('load').returncode, 0)
        self.assertFalse(self.attempted())
        self.assertFalse((self.module / 'planted').exists())


if __name__ == '__main__':
    unittest.main()
