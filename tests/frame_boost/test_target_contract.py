import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('fbg_audit', ROOT / 'scripts/audit_frame_boost_target.py')
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class HookContractTests(unittest.TestCase):
    def test_declaration_ignores_comments_and_parameter_names(self):
        text = ('/* DECLARE_HOOK(h, TP_PROTO(int ignored), TP_ARGS(ignored)); */\n'
                'DECLARE_RESTRICTED_HOOK(h, TP_PROTO(struct task_struct * task,\n'
                'unsigned long value), TP_ARGS(task, value), 1);')
        self.assertEqual(audit.types_only(audit.declaration(text, 'h')),
                         audit.types_only('struct task_struct *p, unsigned long delta'))

    def test_pointer_depth_and_scalar_types_are_significant(self):
        self.assertNotEqual(audit.types_only('struct task_struct **p'),
                            audit.types_only('struct task_struct *p'))
        self.assertNotEqual(audit.types_only('unsigned int value'),
                            audit.types_only('unsigned long value'))

    def test_missing_or_duplicate_hook_fails(self):
        with self.assertRaises(ValueError):
            audit.declaration('', 'h')
        text = 'DECLARE_HOOK(h, TP_PROTO(int value), TP_ARGS(value));\n'
        with self.assertRaises(ValueError):
            audit.declaration(text * 2, 'h')

    def test_config_has_explicit_disabled_default(self):
        self.assertEqual(audit.config_values('# CONFIG_X is not set\nCONFIG_Y=m\n'), {'Y': 'm'})

    def test_missing_target_does_not_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = root / '.config'
            config.write_text('CONFIG_ARM64=y\n')
            result = audit.audit(root, root, config)
            self.assertFalse(result['hook_contract_passed'])
            self.assertTrue(any('binder.h' in error for error in result['errors']))
            self.assertTrue(any('Module.symvers' in error for error in result['errors']))


if __name__ == '__main__':
    unittest.main()
