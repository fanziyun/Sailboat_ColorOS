#!/usr/bin/env python3
import argparse
import hashlib
import pathlib
import shutil
import sys

comp = pathlib.Path(__file__).resolve().parents[1]
root = comp.parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--tree', type=pathlib.Path, required=True)
parser.add_argument('--precompiled-dir', type=pathlib.Path)
parser.add_argument('--binary', type=pathlib.Path,
                    default=root / 'frameboost/dist/Sail_FrameBoost')
args = parser.parse_args()

tree = args.tree.resolve()
binary = args.binary.resolve()
rc_src = comp / 'config/Sail_FrameBoost.rc'
cil_src = comp / 'config/system_ext_sepolicy.cil.part'
fc_entry = '/system_ext/bin/Sail_FrameBoost u:object_r:Sail_FrameBoost_exec:s0'

cil = tree / 'etc/selinux/system_ext_sepolicy.cil'
fc = tree / 'etc/selinux/system_ext_file_contexts'
for path in (cil, fc):
    if not path.is_file():
        sys.exit('missing %s: --tree must point at a complete system_ext tree' % path)

for sub in ('bin', 'etc/init'):
    (tree / sub).mkdir(parents=True, exist_ok=True)

shutil.copyfile(binary, tree / 'bin/Sail_FrameBoost')
(tree / 'bin/Sail_FrameBoost').chmod(0o755)
shutil.copyfile(rc_src, tree / 'etc/init/Sail_FrameBoost.rc')
(tree / 'etc/init/Sail_FrameBoost.rc').chmod(0o644)

fragment = cil_src.read_text().rstrip('\n') + '\n'
current = cil.read_text()
if '(type Sail_FrameBoost)' in current:
    cil_changed = False
else:
    cil.write_text(current.rstrip('\n') + '\n' + fragment)
    cil_changed = True

fc_text = fc.read_text()
if fc_entry in fc_text:
    fc_changed = False
else:
    fc.write_text(fc_text.rstrip('\n') + '\n' + fc_entry + '\n')
    fc_changed = True

dropped = []
if cil_changed:
    if args.precompiled_dir is None:
        sys.exit('CIL modified: pass --precompiled-dir <tree>/system/etc/selinux '
                 'so the stale precompiled policy is dropped and init rebuilds it')
    pdir = args.precompiled_dir.resolve()
    if not pdir.is_dir():
        sys.exit('not a directory: %s' % pdir)
    for path in sorted(pdir.glob('precompiled_sepolicy*')):
        path.unlink()
        dropped.append(path.name)
    if not dropped:
        sys.exit('no precompiled_sepolicy* found in %s: init would keep using a '
                 'policy that lacks Sail_FrameBoost' % pdir)

print('tree      ', tree)
print('binary    ', (tree / 'bin/Sail_FrameBoost').stat().st_size,
      hashlib.sha256((tree / 'bin/Sail_FrameBoost').read_bytes()).hexdigest())
print('rc        ', (tree / 'etc/init/Sail_FrameBoost.rc').stat().st_size)
print('cil       ', cil.stat().st_size, 'merged' if cil_changed else 'already present')
print('file_ctx  ', 'added' if fc_changed else 'already present')
print('prebuilt  ', 'dropped ' + ' '.join(dropped) if dropped else 'kept')
