#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
import re

parser = argparse.ArgumentParser()
parser.add_argument('sdk', type=Path)
args = parser.parse_args()
root = args.sdk.resolve()
source, out = root / 'source', root / 'out'
if not (source / 'Makefile').is_file() or not (out / 'Module.symvers').is_file():
    raise SystemExit('incomplete UX SDK')
proof = json.loads((root / 'target-build.json').read_text())
if proof.get('target_build_verified') is not True or proof.get('sa_config') != 'm':
    raise SystemExit('unverified/non-module SDK')
whitelist = root / 'abi_symbollist.raw'
if not whitelist.is_file():
    raise SystemExit('missing preserved KMI symbol list')
for relative, pattern, replacement in [
    ('.config', r'^CONFIG_UNUSED_KSYMS_WHITELIST=.*$', f'CONFIG_UNUSED_KSYMS_WHITELIST="{whitelist}"'),
    ('include/config/auto.conf', r'^CONFIG_UNUSED_KSYMS_WHITELIST=.*$', f'CONFIG_UNUSED_KSYMS_WHITELIST="{whitelist}"'),
    ('include/generated/autoconf.h', r'^#define CONFIG_UNUSED_KSYMS_WHITELIST .*$', f'#define CONFIG_UNUSED_KSYMS_WHITELIST "{whitelist}"'),
]:
    path = out / relative
    if path.is_file():
        text, count = re.subn(pattern, replacement, path.read_text(), flags=re.M)
        if count != 1:
            raise SystemExit('unexpected whitelist shape in ' + relative)
        path.write_text(text)
(out / 'Makefile').write_text('# Generated relocated UX SDK entry\ninclude ' + str(source / 'Makefile') + '\n')
link = out / 'source'
if link.is_symlink():
    link.unlink()
if link.exists():
    raise SystemExit('unexpected output source entry')
link.symlink_to('../source', target_is_directory=True)
link = out / 'vmlinux'
if link.is_symlink():
    link.unlink()
if not link.exists():
    link.symlink_to('../vmlinux')
print('Relocated paths only; kernel feature configuration unchanged:', root)
