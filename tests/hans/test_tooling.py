#!/usr/bin/env python3
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


fetch = load("sailboat_hans_fetch", REPO / "scripts/fetch_hans_ddk.py")
package = load("sailboat_hans_package", REPO / "scripts/package_hans.py")


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="sailboat_hans_archive_")
        self.root = Path(self.temp.name)
        self.sdk = self.root / "sdk"
        self.dest = self.sdk / "kdir"
        self.dest.mkdir(parents=True)

    def tearDown(self):
        self.temp.cleanup()

    def archive(self, entries):
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode="w") as output:
            for name, kind, content in entries:
                member = tarfile.TarInfo(name)
                member.type = kind
                if kind == tarfile.REGTYPE:
                    member.size = len(content)
                    output.addfile(member, io.BytesIO(content))
                else:
                    member.linkname = content
                    output.addfile(member)
        compressed = subprocess.run(["zstd", "-c"], input=buffer.getvalue(),
                                    check=True, capture_output=True).stdout
        path = self.root / "fixture.tar.zst"
        path.write_bytes(compressed)
        return path

    def test_regular_file(self):
        path = self.archive([("kernel/header.h", tarfile.REGTYPE, b"header")])
        fetch.extract(path, self.dest, self.sdk)
        self.assertEqual((self.dest / "kernel/header.h").read_bytes(), b"header")

    def test_traversal_rejected(self):
        path = self.archive([("../../outside", tarfile.REGTYPE, b"bad")])
        with self.assertRaises(RuntimeError):
            fetch.extract(path, self.dest, self.sdk)
        self.assertFalse((self.root / "outside").exists())

    def test_absolute_symlink_rejected(self):
        path = self.archive([("link", tarfile.SYMTYPE, "/tmp/outside")])
        with self.assertRaises(RuntimeError):
            fetch.extract(path, self.dest, self.sdk)

    def test_rebased_symlink_escape_rejected(self):
        path = self.archive([("link", tarfile.SYMTYPE, "/opt/ddk/../../outside")])
        with self.assertRaises(RuntimeError):
            fetch.extract(path, self.dest, self.sdk)
        self.assertFalse((self.dest / "link").exists())

    def test_internal_ddk_link_rebased(self):
        path = self.archive([("source", tarfile.SYMTYPE, "/opt/ddk/src/android13-5.15")])
        fetch.extract(path, self.dest, self.sdk)
        self.assertEqual((self.dest / "source").resolve(), self.sdk / "src/android13-5.15")

    def test_special_member_rejected(self):
        path = self.archive([("pipe", tarfile.FIFOTYPE, "")])
        with self.assertRaises(RuntimeError):
            fetch.extract(path, self.dest, self.sdk)


class PackageTests(unittest.TestCase):
    def test_empty_module_rejected(self):
        with self.assertRaises(ValueError):
            package.elf_metadata(b"")

    def test_truncated_header_rejected(self):
        with self.assertRaises(ValueError):
            package.elf_metadata(b"\x7fELF\x02\x01\x01" + b"\0" * 20)


if __name__ == "__main__":
    unittest.main()
