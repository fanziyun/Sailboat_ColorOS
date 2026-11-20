import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('fbg_probe', ROOT / 'scripts/prepare_frame_boost_probe.py')
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)
UPSTREAM = ROOT / 'src_upstream/oplus_sm8550'
SA_PATH = 'vendor/oplus/kernel/cpu/sched/sched_assist/'


class CompileProbeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.sa = self.root / 'sa'
        self.sa.mkdir()
        original = subprocess.check_output(['git', '-C', str(UPSTREAM), 'show',
                                            probe.ux.UPSTREAM_SHA + ':' + SA_PATH + 'sa_common.h'])
        adapted = original.replace(b'#define OTS_IDX\t\t\t0', b'#define OTS_IDX\t\t\t2')
        self.assertNotEqual(original, adapted)
        (self.sa / 'sa_common.h').write_bytes(adapted)
        self.stamp = {'upstream_sha': probe.ux.UPSTREAM_SHA,
                      'output_sha256': {'sa_common.h': probe.ux.digest(adapted)}}
        (self.sa / probe.ux.STAMP).write_text(json.dumps(self.stamp))
        self.destination = self.root / 'probe'

    def tearDown(self):
        self.temporary.cleanup()

    def test_generates_archive_only_and_public_proc_api(self):
        probe.prepare(UPSTREAM, self.sa, self.destination)
        makefile = (self.destination / 'Makefile').read_text()
        self.assertIn('obj-y :=', makefile)
        self.assertNotIn('obj-m', makefile)
        self.assertIn('frame_ioctl_V1.o', makefile)
        self.assertNotIn('touch_ioctl.o', makefile)
        ioctl = (self.destination / 'frame_ioctl_V1.c').read_text()
        self.assertIn('proc_set_user(pentry, GLOBAL_SYSTEM_UID, GLOBAL_SYSTEM_GID)', ioctl)
        self.assertNotIn('pentry->uid', ioctl)
        self.assertNotIn('pentry->gid', ioctl)
        self.assertNotIn('fs/proc/internal.h', ioctl)
        self.assertIn('#include "frame_ioctl.h"', ioctl)
        self.assertTrue((self.destination / 'sa_oemdata.h').is_file())
        manifest = json.loads((self.destination / 'sailboat_coloros_fbg_probe.json').read_text())
        self.assertFalse(manifest['module_loadable'])
        self.assertTrue(manifest['compile_only'])
        self.assertEqual(len(manifest['objects']), 8)

    def test_rejects_modified_sa_header_before_output(self):
        with (self.sa / 'sa_common.h').open('ab') as stream:
            stream.write(b'\nmodified\n')
        with self.assertRaisesRegex(ValueError, 'modified adapted sched_assist header'):
            probe.prepare(UPSTREAM, self.sa, self.destination)
        self.assertFalse(self.destination.exists())

    def test_refuses_existing_output_without_overwrite(self):
        self.destination.mkdir()
        marker = self.destination / 'marker'
        marker.write_text('keep')
        with self.assertRaises(FileExistsError):
            probe.prepare(UPSTREAM, self.sa, self.destination)
        self.assertEqual(marker.read_text(), 'keep')


if __name__ == '__main__':
    unittest.main()
