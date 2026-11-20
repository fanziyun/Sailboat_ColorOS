#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
import re


def rewrite_whitelist(out, whitelist):
    escaped = str(whitelist).replace('\\', '\\\\').replace('"', '\\"')
    for relative, pattern, replacement in [
        ('.config', r'^CONFIG_UNUSED_KSYMS_WHITELIST=.*$', f'CONFIG_UNUSED_KSYMS_WHITELIST="{escaped}"'),
        ('include/config/auto.conf', r'^CONFIG_UNUSED_KSYMS_WHITELIST=.*$', f'CONFIG_UNUSED_KSYMS_WHITELIST="{escaped}"'),
        ('include/generated/autoconf.h', r'^#define CONFIG_UNUSED_KSYMS_WHITELIST .*$', f'#define CONFIG_UNUSED_KSYMS_WHITELIST "{escaped}"'),
    ]:
        path = out / relative
        if path.is_file():
            text, count = re.subn(pattern, lambda _: replacement, path.read_bytes().decode('utf-8'), flags=re.M)
            if count != 1:
                raise ValueError('unexpected whitelist shape in ' + relative)
            path.write_bytes(text.encode('utf-8'))


def write_entry_makefile(out, source, title):
    (out / 'Makefile').write_bytes(('# ' + title + '\ninclude ' + str(source / 'Makefile') + '\n').encode('utf-8'))


def link_source(out, target):
    link = out / 'source'
    if link.is_symlink():
        link.unlink()
    if link.exists():
        raise ValueError('unexpected output source entry')
    link.symlink_to(target, target_is_directory=True)


def relocate(root):
    root = root.resolve()
    source, out = root / 'source', root / 'out'
    if not (source / 'Makefile').is_file() or not (out / 'Module.symvers').is_file():
        raise ValueError('incomplete UX SDK')
    proof = json.loads((root / 'target-build.json').read_text())
    if proof.get('target_build_verified') is not True or proof.get('sa_config') != 'm':
        raise ValueError('unverified/non-module SDK')
    whitelist = root / 'abi_symbollist.raw'
    if not whitelist.is_file():
        raise ValueError('missing preserved KMI symbol list')
    rewrite_whitelist(out, whitelist)
    write_entry_makefile(out, source, 'Generated relocated UX SDK entry')
    link_source(out, '../source')
    link = out / 'vmlinux'
    if link.is_symlink():
        link.unlink()
    if not link.exists():
        link.symlink_to('../vmlinux')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('sdk', type=Path)
    args = parser.parse_args()
    try:
        relocate(args.sdk)
    except ValueError as error:
        raise SystemExit(str(error))
    print('Relocated paths only; kernel feature configuration unchanged:', args.sdk.resolve())


if __name__ == '__main__':
    main()
