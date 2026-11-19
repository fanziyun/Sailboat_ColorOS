#!/usr/bin/env python3
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]


class LoaderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sailboat_hans_loader_")
        self.root = Path(self.temp.name)
        self.module = self.root / "Sail_Hans"
        shutil.copytree(REPO / "ksu" / "Sail_Hans", self.module)
        (self.module / "lib/modules").mkdir(parents=True)
        (self.module / "lib/modules/sailboat_coloros_hans.ko").write_bytes(b"test module")
        (self.module / "prototype.manifest").write_text(
            "target_kernel=5.15.225-Sailboat-ColorOS-20261118\ntarget_abi_verified=0\nko_sha256=none\n")
        self.bin = self.root / "bin"
        self.bin.mkdir()
        for command, text in {
            "id": "printf '0\\n'",
            "uname": "case \"$1\" in -m) printf 'aarch64\\n';; -r) printf '5.15.225-Sailboat-ColorOS-20261118\\n';; esac",
            "insmod": f"printf 'insmod called\\n' >> '{self.root / 'calls'}'",
            "rmmod": f"printf 'rmmod called\\n' >> '{self.root / 'calls'}'",
            "log": f"printf 'log called\\n' >> '{self.root / 'calls'}'",
        }.items():
            path = self.bin / command
            path.write_text("#!/bin/sh\n" + text + "\n")
            path.chmod(0o755)
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ["PATH"])

    def tearDown(self):
        self.temp.cleanup()

    def run_action(self, *args):
        return subprocess.run(["sh", str(self.module / "action.sh"), *args],
                              env=self.env, text=True, capture_output=True)

    def test_unverified_probe_never_loads(self):
        result = self.run_action("probe")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ABI is not verified", result.stderr)
        self.assertFalse((self.root / "calls").exists())

    def test_native_never_loads(self):
        result = self.run_action("native")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("native mode blocked", result.stderr)
        self.assertFalse((self.root / "calls").exists())

    def test_status_no_mutation(self):
        result = self.run_action("status")
        self.assertEqual(result.returncode, 0)
        self.assertIn("native_load_blocked=1", result.stdout)
        self.assertFalse((self.root / "calls").exists())

    def test_manifest_is_data_not_shell(self):
        marker = self.root / "injected"
        with (self.module / "prototype.manifest").open("a") as output:
            output.write(f"other=$(touch '{marker}')\n")
        self.run_action("preflight")
        self.assertFalse(marker.exists())

    def test_boot_service_does_not_load(self):
        result = subprocess.run(["sh", str(self.module / "service.sh")], env=self.env,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0)
        self.assertNotIn("insmod", (self.root / "calls").read_text())

    def test_unknown_command_is_rejected(self):
        result = self.run_action("force-load")
        self.assertEqual(result.returncode, 2)
        self.assertFalse((self.root / "calls").exists())


if __name__ == "__main__":
    unittest.main()
