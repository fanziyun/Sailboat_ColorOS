import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('animation_probe', ROOT / 'scripts/probe_animation_boost.py')
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)


class ProbeTests(unittest.TestCase):
    def test_consumers_are_numeric_and_scoped(self):
        text = ('PID NAME\n2864 surfaceflinger\n20 system_server\n'
                '2703 vendor-oplus-hardware-performance-V1-service\n'
                '33 other\nxx system_server\n')
        self.assertEqual(probe.consumer_pids(text), [(20, 'system_server'),
                         (2703, 'vendor-oplus-hardware-performance-V1-service'),
                         (2864, 'surfaceflinger')])

    def test_maps_are_scoped_and_deduplicated(self):
        prefix = '1000-2000 r--p 00000000 fd:00 1 '
        paths = ['/system/lib64/libgui.so', '/system/lib64/libgui.so',
                 '/data/local/tmp/liboplus.so', '/system/lib64/../badoplus.so',
                 '/system/lib64/libc.so', '/system/lib64/liboplus.so (deleted)',
                 '/odm/bin/hw/vendor-oplus-hardware-performance-V1-service']
        self.assertEqual(probe.mapped_files('\n'.join(prefix + path for path in paths)),
                         ['/odm/bin/hw/vendor-oplus-hardware-performance-V1-service',
                          '/system/lib64/libgui.so'])

    def test_strings_have_offsets_not_execution(self):
        data = b'\x7fELF\0/proc/oplus_frame_boost/sys_ctrl\0irrelevant\0enableAnimationCtrl\0'
        result = probe.abi_strings(data)
        self.assertEqual([entry['text'] for entry in result],
                         ['/proc/oplus_frame_boost/sys_ctrl', 'enableAnimationCtrl'])
        self.assertEqual(result[0]['offset'], 5)

    def test_empty_probe_data_is_not_a_consumer(self):
        self.assertEqual(probe.consumer_pids('permission denied'), [])
        self.assertEqual(probe.mapped_files(''), [])
        self.assertEqual(probe.abi_strings(b'\x00' * 100), [])


if __name__ == '__main__':
    unittest.main()
