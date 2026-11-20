#!/usr/bin/env python3
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import posixpath
import re
import struct
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('ux_packager', ROOT / 'scripts/package_ux.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


def sha(path):
    hasher = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            hasher.update(block)
    return hasher.hexdigest()


def sections(path, wanted):
    result = {}
    with path.open('rb') as stream:
        header = stream.read(64)
        fields = struct.unpack('<16sHHIQQQIHHHHHH', header)
        if header[:7] != b'\x7fELF\x02\x01\x01' or fields[11] != 64:
            raise ValueError('not ELF64 LE: ' + str(path))
        stream.seek(fields[6])
        entries = [struct.unpack('<IIQQQQIIQQ', stream.read(64)) for _ in range(fields[12])]
        names_entry = entries[fields[13]]
        stream.seek(names_entry[4])
        names = stream.read(names_entry[5])
        for entry in entries:
            name = names[entry[0]:names.index(b'\0', entry[0])].decode()
            if name in wanted:
                if entry[5] > 16 * 1024 * 1024:
                    raise ValueError('inspection section too large')
                stream.seek(entry[4])
                result[name] = stream.read(entry[5])
    if set(result) != set(wanted):
        raise ValueError('missing sections: ' + str(set(wanted) - result.keys()))
    return result


def verify(ko, out, dist, clang):
    config = dict(line.split('=', 1) for line in (out / '.config').read_text().splitlines()
                  if line.startswith('CONFIG_') and '=' in line)
    required = {'CONFIG_OPLUS_SCHED_ASSIST': 'm', 'CONFIG_CFI_CLANG': 'y',
                'CONFIG_MODVERSIONS': 'y', 'CONFIG_LTO_CLANG_THIN': 'y',
                'CONFIG_TRIM_UNUSED_KSYMS': 'y'}
    if any(config.get(key) != value for key, value in required.items()):
        raise ValueError('target build configuration is not the audited module profile')
    data = ko.read_bytes()
    metadata = packager.module_metadata.elf_metadata(data, expected_name='sailboat_sched_assist')
    release = metadata['vermagic'].split()[0]
    if '-UXmodBase-' not in release:
        raise ValueError('expected a distinct UXmodBase kernel release')
    ids = re.findall(rb'(?:^|\x00)sailboat_ux_source_id=([0-9a-f]{64})\x00', data)
    if len(ids) != 1:
        raise ValueError('invalid module source identity')
    version_data = sections(ko, {'__versions'})['__versions']
    if not version_data or len(version_data) % 64:
        raise ValueError('invalid symbol version section')
    versions = {}
    for index in range(0, len(version_data), 64):
        crc, name = struct.unpack_from('<Q56s', version_data, index)
        name = name.split(b'\0', 1)[0].decode()
        if not name or name in versions:
            raise ValueError('duplicate/empty versioned symbol')
        versions[name] = crc
    expected = {}
    for line in (out / 'Module.symvers').read_text().splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[2] in ('vmlinux', 'kernel/vmlinux'):
            expected[fields[1]] = int(fields[0], 16)
    imports_text = subprocess.check_output([str(clang / 'llvm-nm'), '-u', str(ko)], text=True)
    imports = {line.split()[-1] for line in imports_text.splitlines() if re.search(r'\bU\s+\S+$', line)}
    missing = (imports | {'module_layout'}) - versions.keys()
    mismatch = {name for name, crc in versions.items() if expected.get(name) != crc}
    if missing or mismatch:
        raise ValueError(f'target CRC mismatch/missing: {sorted(mismatch | missing)}')
    vmlinux = out / 'vmlinux'
    notes = sections(vmlinux, {'.notes'})['.notes']
    symbols = subprocess.check_output([str(clang / 'llvm-nm'), '--defined-only', str(vmlinux)], text=True)
    forbidden = {'oplus_replace_next_task_fair', 'sailboat_coloros_nodes_proc_init', 'sailboat_coloros_ux_reside'}
    if forbidden & {line.split()[-1] for line in symbols.splitlines() if line.split()}:
        raise ValueError('sched_assist is linked into the purported module base')
    image = (dist / 'Image').read_bytes()
    if ('Linux version ' + release + ' ').encode() not in image:
        raise ValueError('Image and module releases differ')
    for marker in (b'profile=sailboat_coloros_ux_min ready=', b'sailboat_coloros_ux_min: ready, runtime disabled'):
        if marker in image:
            raise ValueError('UX implementation found in base Image')
    begin = image.find(b'IKCFG_ST')
    end = image.find(b'IKCFG_ED', begin + 8)
    if begin < 0 or end < 0:
        raise ValueError('Image has no embedded config')
    embedded = gzip.decompress(image[begin + 8:end]).decode()
    if 'CONFIG_OPLUS_SCHED_ASSIST=m\n' not in embedded:
        raise ValueError('embedded config is not module mode')
    report = dict(schema=1, target_build_verified=True, release=release,
                  sa_config='m', module_sha256=sha(ko), image_sha256=sha(dist / 'Image'),
                  notes_sha256=hashlib.sha256(notes).hexdigest(), source_id=ids[0].decode(),
                  crc_count=len(versions), strong_import_count=len(imports), cfi=True,
                  config_sha256=sha(out / '.config'), symvers_sha256=sha(out / 'Module.symvers'),
                  toolchain_sha256=sha(clang / 'clang'), device_tested=False)
    return report, notes, imports_text


def archive_filter(member):
    parts = Path(member.name).parts
    if any(part in ('.git', '__pycache__') for part in parts):
        return None
    if member.name.startswith('sdk/out/'):
        name = Path(member.name).name
        if name in ('source', 'vmlinux', 'Image', 'Image.gz', 'Image.lz4') or name.endswith(('.o', '.a', '.ko', '.cmd', '.d', '.su')):
            return None
    if member.issym() or member.islnk():
        resolved = posixpath.normpath(posixpath.join(posixpath.dirname(member.name), member.linkname))
        if member.linkname.startswith('/') or not resolved.startswith('sdk/'):
            return None
    if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
        return None
    member.uid = member.gid = 0
    member.uname = member.gname = ''
    return member


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--kernel-out', type=Path, required=True)
    parser.add_argument('--source-dir', type=Path, required=True)
    parser.add_argument('--dist-dir', type=Path, required=True)
    parser.add_argument('--clang-bin', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--module', type=Path)
    parser.add_argument('--no-sdk', action='store_true')
    args = parser.parse_args()
    ko = args.module or args.kernel_out / 'drivers/sailboat_sched_assist/sailboat_sched_assist.ko'
    report, notes, imports = verify(ko, args.kernel_out, args.dist_dir, args.clang_bin)
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / ko.name).write_bytes(ko.read_bytes())
    (args.output / 'kernel-notes.bin').write_bytes(notes)
    (args.output / 'imports.txt').write_text(imports)
    proof = args.output / 'target-build.json'
    proof.write_text(json.dumps(report, indent=2) + '\n')
    (args.output / 'Module.symvers').write_bytes((args.kernel_out / 'Module.symvers').read_bytes())
    (args.output / 'kernel.config').write_bytes((args.kernel_out / '.config').read_bytes())
    packager.package(ko, proof, args.output / ('Sail_UX-' + report['release'] + '.zip'))
    if args.no_sdk:
        print(json.dumps(report, indent=2))
        return
    whitelist_line = next(line for line in (args.kernel_out / '.config').read_text().splitlines()
                          if line.startswith('CONFIG_UNUSED_KSYMS_WHITELIST='))
    whitelist = Path(whitelist_line.split('=', 1)[1].strip('"'))
    if not whitelist.is_file():
        raise ValueError('missing actual KMI whitelist: ' + str(whitelist))
    sdk = args.output / ('Sail_UX-' + report['release'] + '-sdk.tar.gz')
    with tarfile.open(sdk, 'w:gz', dereference=False) as archive:
        archive.add(args.source_dir, arcname='sdk/source', filter=archive_filter)
        archive.add(args.kernel_out, arcname='sdk/out', filter=archive_filter)
        archive.add(args.kernel_out / 'vmlinux', arcname='sdk/vmlinux')
        archive.add(args.dist_dir / 'Image', arcname='sdk/dist/Image')
        archive.add(args.clang_bin.parent, arcname='sdk/clang', filter=archive_filter)
        archive.add(whitelist, arcname='sdk/abi_symbollist.raw')
        for file in ['rebuild_ux_sdk.sh', 'relocate_ux_sdk.py']:
            archive.add(ROOT / 'scripts' / file, arcname='sdk/' + file)
        archive.add(proof, arcname='sdk/target-build.json')
    records = {path.name: sha(path) for path in sorted(args.output.iterdir()) if path.is_file()}
    (args.output / 'SHA256SUMS').write_text(''.join(value + '  ' + name + '\n' for name, value in records.items()))
    print(json.dumps(dict(report, sdk_sha256=sha(sdk)), indent=2))


if __name__ == '__main__':
    main()
