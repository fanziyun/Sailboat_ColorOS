#!/usr/bin/env python3
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import stat
import zipfile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('sailboat_module_metadata', ROOT / 'scripts/package_hans.py')
module_metadata = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module_metadata)


def package(ko, verification, output):
    data = ko.read_bytes()
    metadata = module_metadata.elf_metadata(data, expected_name='sailboat_sched_assist')
    report = json.loads(verification.read_text())
    digest = hashlib.sha256(data).hexdigest()
    ids = re.findall(rb'(?:^|\x00)sailboat_ux_source_id=([0-9a-f]{64})\x00', data)
    if len(ids) != 1:
        raise ValueError('missing/ambiguous module source identity')
    source_id = ids[0].decode()
    if report.get('schema') != 1 or report.get('target_build_verified') is not True:
        raise ValueError('target build has not been verified')
    if report.get('module_sha256') != digest or report.get('source_id') != source_id:
        raise ValueError('module does not match the verified build')
    release = report.get('release', '')
    if not re.fullmatch(r'5[.]15[.][A-Za-z0-9._+-]+', release) or '-UXmodBase-' not in release:
        raise ValueError('not a module-test kernel release')
    if metadata['vermagic'].split()[0] != release:
        raise ValueError('module vermagic and target release differ')
    notes = report.get('notes_sha256', '')
    if not re.fullmatch('[0-9a-f]{64}', notes) or report.get('sa_config') != 'm':
        raise ValueError('missing matching no-SA base evidence')
    if report.get('crc_count', 0) < 2:
        raise ValueError('no verified symbol CRC evidence')
    manifest = (f'format=1\ntarget_release={release}\nko_sha256={digest}\n'
                f'notes_sha256={notes}\nsource_id={source_id}\ntarget_build_verified=1\n')
    template = ROOT / 'ksu/Sail_UX'
    files = [(path.name, path.read_bytes(), 0o755 if path.suffix == '.sh' else 0o644)
             for path in sorted(template.iterdir()) if path.is_file() and not path.is_symlink()]
    files += [('ux.manifest', manifest.encode(), 0o644),
              ('lib/modules/sailboat_sched_assist.ko', data, 0o644)]
    output.parent.mkdir(parents=True, exist_ok=True)
    summary_path = output.with_suffix('.json')
    if output.exists() or summary_path.exists():
        raise ValueError('refusing to overwrite an existing package/report')
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, content, mode in files:
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = (stat.S_IFREG | mode) << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, content)
    summary = dict(metadata, module_sha256=digest, source_id=source_id, release=release,
                   notes_sha256=notes, zip_sha256=hashlib.sha256(output.read_bytes()).hexdigest(),
                   autoload=True, policy_default='OFF', hot_unload=False, device_tested=False)
    with summary_path.open('x', encoding='utf-8') as stream:
        stream.write(json.dumps(summary, ensure_ascii=False, indent=2) + '\n')
    return summary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('ko', type=Path)
    parser.add_argument('verification', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print(json.dumps(package(args.ko, args.verification, args.output), indent=2))


if __name__ == '__main__':
    main()
