import argparse
import hashlib
import importlib.util
from pathlib import Path, PurePosixPath
import posixpath
import shutil
import tarfile

_spec = importlib.util.spec_from_file_location(
    'relocate_ux_sdk', Path(__file__).with_name('relocate_ux_sdk.py'))
relocate = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(relocate)

parser = argparse.ArgumentParser()
parser.add_argument('archive', type=Path)
parser.add_argument('destination', type=Path)
args = parser.parse_args()
with args.archive.open('rb') as probe:
    if probe.read(2) != b'\x1f\x8b':
        raise ValueError('archive is not a gzip tar')
args.destination.mkdir(parents=True, exist_ok=False)
skipped = []
count = 0
size = 0

def safe(member, destination):
    global size
    name = PurePosixPath(member.name)
    if name.is_absolute() or '..' in name.parts or chr(92) in member.name or ':' in member.name:
        raise ValueError('unsafe archive path: ' + member.name)
    if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
        skipped.append(member.name)
        return None
    if member.issym() or member.islnk():
        target = member.linkname
        resolved = posixpath.normpath(posixpath.join(posixpath.dirname(member.name), target) if member.issym() else target)
        if target.startswith('/') or chr(92) in target or resolved == '..' or resolved.startswith('../') or ':' in target:
            skipped.append(member.name + ' -> ' + target)
            return None
    size += member.size
    if size > 24 * 1024**3:
        raise ValueError('archive expansion exceeds limit')
    return tarfile.data_filter(member, destination)

try:
    with tarfile.open(args.archive, 'r|gz') as archive:
        for member in archive:
            archive.extract(member, args.destination, filter=safe)
            count += 1
except BaseException:
    shutil.rmtree(args.destination, ignore_errors=True)
    raise
source = args.destination / 'common'
out = args.destination / 'out/android13-5.15/common'
for file in [source / 'Makefile', out / '.config', out / 'Module.symvers', out / 'vmlinux']:
    if not file.is_file():
        raise ValueError('missing required file: ' + str(file))
original = (out / '.config').read_bytes()
(args.destination / 'original-kernel.config').write_bytes(original)
whitelist = out / 'abi_symbollist.raw'
if not whitelist.is_file():
    raise ValueError('missing generated whitelist')
relocate.rewrite_whitelist(out, whitelist)
relocate.write_entry_makefile(out, source, 'Local entry for matching CI kernel materials')
relocate.link_source(out, source)
(args.destination / 'skipped-links.txt').write_bytes(('\n'.join(skipped) + '\n').encode('utf-8'))
print('Extracted archive entries:', count)
print('Expanded bytes:', size)
print('Skipped external links:', len(skipped))
print('Original config SHA256:', hashlib.sha256(original).hexdigest())
print('Kernel source:', source)
print('Prepared output:', out)
print('Only build paths relocated; no feature configs changed.')
