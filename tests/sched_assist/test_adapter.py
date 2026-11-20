import importlib.util
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import types
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('ux_adapter', ROOT / 'scripts/adapt_sched_assist.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
SOURCE = ROOT / 'src_upstream/oplus_sm8550/vendor/oplus/kernel/cpu/sched/sched_assist'
PORT = ROOT / 'port/sched_assist/src'
TRACE = ROOT / 'out/ux_compat_20261120/evidence.vSixkq/base/trace_sched.h'
KERNEL = pathlib.Path(os.environ['SAILBOAT_TEST_KERNEL_SRC']) if 'SAILBOAT_TEST_KERNEL_SRC' in os.environ else None


class AdapterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.out = self.root / 'driver'
        self.out.mkdir()
        for path in SOURCE.iterdir():
            if path.suffix in ('.c', '.h'):
                shutil.copy2(path, self.out)
        self.kernel = self.root / 'kernel'
        header = self.kernel / 'include/trace/hooks/sched.h'
        header.parent.mkdir(parents=True)
        reference = KERNEL / 'include/trace/hooks/sched.h' if KERNEL else TRACE
        if not reference.is_file():
            self.fail('provide SAILBOAT_TEST_KERNEL_SRC with the matching Android hook headers')
        shutil.copy2(reference, header)
        self.config = self.kernel / '.config'
        self.config.write_text('CONFIG_FAIR_GROUP_SCHED=y\nCONFIG_CFI_CLANG=y\n')

    def tearDown(self):
        self.temp.cleanup()

    def run_adapter(self, sha=None, builtin=False):
        return module.adapt(self.out, PORT, self.kernel, sha or module.UPSTREAM_SHA, builtin)

    def test_real_source_idempotent_and_core_hooks(self):
        first = self.run_adapter()
        self.assertEqual(first, self.run_adapter())
        sa = (self.out / 'sched_assist.c').read_text()
        begin, _, end = module.span(sa, 'register_scheduler_vendor_hooks')
        registered = sa[begin:end]
        self.assertIn('REGISTER_TRACE_VH(android_rvh_enqueue_task,', registered)
        self.assertIn('REGISTER_TRACE_VH(android_rvh_dequeue_task,', registered)
        self.assertNotIn('android_vh_exit_signal', registered)
        self.assertNotIn('post_init_entity_util_avg', registered)
        self.assertNotIn('wake_up_new_task', registered)
        self.assertNotIn('return -EOPNOTSUPP;', sa)
        self.assertIn('sailboat_coloros_ux_reside();', sa)
        self.assertIn('return sailboat_coloros_ux_init_failed(ret);', sa)
        self.assertLess(sa.index('sailboat_coloros_ux_reside();'), sa.index('sched_assist_init_oplus_rq();'))
        self.assertNotIn('module_exit(', sa)
        self.assertIn('set_next_entity(cfs_rq, *se);', (self.out / 'sa_fair.c').read_text())
        self.assertIn('RB_CLEAR_NODE(&ots->exec_time_node)', (self.out / 'sa_priority.c').read_text())
        self.assertIn('sailboat_coloros_ux.o', (self.out / 'Makefile').read_text())
        self.assertEqual(first['upstream_sha'], module.UPSTREAM_SHA)

    def test_missing_anchor_is_transactional(self):
        path = self.out / 'sa_common.h'
        path.write_text(path.read_text().replace('#define OTS_IDX\t\t\t0', '#define OTS_IDX 9'))
        before = {p.name: p.read_bytes() for p in self.out.iterdir()}
        with self.assertRaises(ValueError):
            self.run_adapter()
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.out.iterdir()})

    def test_source_verification_only_normalizes_crlf(self):
        root = self.root / 'cache'
        directory = 'vendor/oplus/kernel/cpu/sched/sched_assist/'
        frame = 'vendor/oplus/kernel/cpu/sched/frame_boost/frame_boost_V1/frame_group.h'
        names = [directory + 'sa_common.c', frame]
        contents = [b'int source;\n', b'int frame;\n']
        for name, data in zip(names, contents):
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data.replace(b'\n', b'\r\n'))
        tree = b''.join(b'100755 blob ' + b'a' * 40 + b'\t' + name.encode() + bytes([0]) for name in names)
        blobs = b''.join(b'a' * 40 + b' blob ' + str(len(data)).encode() + b'\n' + data + b'\n' for data in contents)
        def git_response(command, **kwargs):
            operation = command[3]
            data = (module.UPSTREAM_SHA + '\n').encode() if operation == 'rev-parse' else tree if operation == 'ls-tree' else blobs
            return types.SimpleNamespace(stdout=data)
        with mock.patch.object(module.subprocess, 'run', side_effect=git_response):
            self.assertEqual(module.verify_upstream(root), module.UPSTREAM_SHA)
            (root / names[0]).write_bytes(b'int modified;\r\n')
            with self.assertRaises(ValueError):
                module.verify_upstream(root)

    def test_reject_foreign_snapshot(self):
        with self.assertRaises(ValueError):
            self.run_adapter(sha='0' * 40)
        self.assertFalse((self.out / module.STAMP).exists())

    def test_reject_unsupported_configuration(self):
        for flag in ('SCHED_WALT', 'OPLUS_SYSTEM_KERNEL_QCOM', 'LOCKING_PROTECT'):
            for state in ('y', 'm'):
                self.config.write_text(f'CONFIG_{flag}={state}\n')
                with self.assertRaises(ValueError):
                    self.run_adapter()
        self.assertFalse((self.out / module.STAMP).exists())

    def test_reject_hook_shape_change(self):
        header = self.kernel / 'include/trace/hooks/sched.h'
        header.write_text(header.read_text().replace('bool simple, struct task_struct *prev', 'int simple, struct task_struct *prev'))
        with self.assertRaises(ValueError):
            self.run_adapter()

    def test_reject_tampered_output(self):
        self.run_adapter()
        path = self.out / 'sa_fair.c'
        path.write_text(path.read_text() + '\nint tampered;\n')
        with self.assertRaises(ValueError):
            self.run_adapter()

    @unittest.skipIf(os.name == 'nt', 'shell integration runs in WSL')
    def test_setup_stages_and_repeat_injection(self):
        common = self.kernel
        drivers = common / 'drivers'
        drivers.mkdir()
        (drivers / 'Makefile').write_text('')
        (drivers / 'Kconfig').write_text('')
        config = common / 'arch/arm64/configs/gki_defconfig'
        config.parent.mkdir(parents=True)
        config.write_text('CONFIG_FAIR_GROUP_SCHED=y\n')
        env = dict(os.environ, KERNEL_ROOT=str(common.parent), DEFCONFIG=str(config),
                   SAILBOAT_UPSTREAM_DIR=str(SOURCE.parents[5]))
        target_common = common.parent / 'common'
        common.rename(target_common)
        env['DEFCONFIG'] = str(target_common / 'arch/arm64/configs/gki_defconfig')
        env['CUSTOM_EXTERNAL_MODULE_STAGE'] = 'after_patch'
        env['SAILBOAT_UX_BUILD_MODE'] = 'module'
        for _ in range(2):
            result = subprocess.run(['bash', str(ROOT / 'setup.sh')], env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        env['CUSTOM_EXTERNAL_MODULE_STAGE'] = 'before_build'
        result = subprocess.run(['bash', str(ROOT / 'setup.sh')], env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        lines = pathlib.Path(env['DEFCONFIG']).read_text().splitlines()
        self.assertEqual([line for line in lines if line.startswith('CONFIG_OPLUS_SCHED_ASSIST=')],
                         ['CONFIG_OPLUS_SCHED_ASSIST=m'])
        self.assertIn('tristate', (target_common / 'drivers/sailboat_sched_assist/Kconfig').read_text())
        self.assertEqual((target_common / 'drivers/Makefile').read_text().count('sailboat_sched_assist/'), 1)
        driver = target_common / 'drivers/sailboat_sched_assist'
        self.assertIn('sailboat_coloros_ux_pick(rq, prev, p, se, repick);', (driver / 'sa_fair.c').read_text())
        self.assertIn('bool simple, struct task_struct *prev);', (driver / 'sa_fair.h').read_text())
        fair = (driver / 'sa_fair.c').read_text()
        self.assertIn('bool simple, struct task_struct *prev)', fair)
        self.assertIn('oplus_replace_next_task_fair(rq, p, se, repick, simple, prev);', fair)
        self.assertNotIn('sailboat_coloros_ux_pick(rq, p, se, repick);', fair)
        init = (target_common / 'drivers/sailboat_sched_assist/sched_assist.c').read_text()
        self.assertNotIn('enable_sched_assist(OPLUS_UX_HOOK_MASK)', init)
        late_config = pathlib.Path(env['DEFCONFIG'])
        late_config.write_text(late_config.read_text() + 'CONFIG_OPLUS_SYSTEM_KERNEL_QCOM=y\n')
        result = subprocess.run(['bash', str(ROOT / 'setup.sh')], env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unsupported config', result.stdout + result.stderr)

    def test_builtin_makefile_and_extract_real_allocator(self):
        self.run_adapter(builtin=True)
        self.assertIn('obj-$(CONFIG_OPLUS_SCHED_ASSIST)', (self.out / 'Makefile').read_text())
        text = (self.out / 'sa_oemdata.c').read_text()
        functions = []
        for name in ('init_oplus_task_struct', 'alloc_oplus_task_struct_node',
                     'sailboat_coloros_ux_ensure_ots', 'register_oemdata_hooks'):
            start, _, end = module.span(text, name)
            functions.append(text[start:end])
        output = pathlib.Path(os.environ.get('SAILBOAT_TEST_OUT', ROOT / 'out/ux_host_tests'))
        output.mkdir(parents=True, exist_ok=True)
        (output / 'extracted_ux_init.h').write_text('\n\n'.join(functions) + '\n')
        fragments = []
        for file, name in [('sa_common.c', 'test_task_ux'),
                           ('sa_common.c', 'dequeue_ux_thread'),
                           ('sa_priority.c', 'android_vh_sched_stat_runtime_handler'),
                           ('sa_sysfs.c', 'write_task_ux')]:
            source = (self.out / file).read_text()
            start, _, end = module.span(source, name)
            fragments.append(source[start:end])
        (output / 'extracted_ux_paths.h').write_text('\n\n'.join(fragments) + '\n')
        pick = []
        for name in ('sailboat_coloros_ux_hierarchy', 'sailboat_coloros_ux_pick'):
            source = (self.out / 'sailboat_coloros_ux.c').read_text()
            start, _, end = module.span(source, name)
            pick.append(source[start:end])
        (output / 'extracted_ux_pick.h').write_text('\n\n'.join(pick) + '\n')
        lifecycle = []
        for file, name in [('sailboat_coloros_ux.c', 'sailboat_coloros_ux_reside'),
                           ('sailboat_coloros_ux.c', 'sailboat_coloros_ux_init_stage'),
                           ('sailboat_coloros_ux.c', 'sailboat_coloros_ux_init_failed'),
                           ('sailboat_coloros_ux.c', 'sailboat_coloros_ux_publish_ready'),
                           ('sched_assist.c', 'register_scheduler_vendor_hooks'),
                           ('sched_assist.c', 'oplus_sched_assist_init')]:
            source = (self.out / file).read_text()
            start, _, end = module.span(source, name)
            lifecycle.append(source[start:end])
        (output / 'extracted_ux_lifecycle.h').write_text('\n\n'.join(lifecycle) + '\n')
        query = (self.out / 'sa_sysfs.c').read_text()
        self.assertIn('global_ux_task_pid = pid;\n\t\t\treturn count;', query)
        self.assertIn('char opt_str[OPT_STR_MAX][13]', query)
        self.assertIn('clear all ux type but animator type', query)
        self.assertIn('sailboat_coloros_ux_init_failed(ret)', (self.out / 'sched_assist.c').read_text())


if __name__ == '__main__':
    unittest.main()
