#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import stat
import struct
import zipfile


def elf_metadata(data):
    if len(data) < 64 or data[:7] != b"\x7fELF\x02\x01\x01":
        raise ValueError("expected ELF64 little-endian module")
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    if header[1] != 1 or header[2] != 183:
        raise ValueError("expected AArch64 relocatable module")
    shoff, shsize, count, names_index = header[6], header[11], header[12], header[13]
    if shsize != 64 or not count or names_index >= count or shoff + count * shsize > len(data):
        raise ValueError("invalid ELF section table")
    sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shsize) for i in range(count)]
    ns = sections[names_index]
    if ns[4] + ns[5] > len(data):
        raise ValueError("invalid section names")
    names = data[ns[4]:ns[4] + ns[5]]
    result = {}
    for section in sections:
        end = names.find(b"\0", section[0])
        if section[0] >= len(names) or end < 0:
            raise ValueError("invalid section name offset")
        name = names[section[0]:end].decode("ascii")
        if section[1] == 8:
            continue
        if section[4] + section[5] > len(data):
            raise ValueError("section exceeds module file")
        result[name] = data[section[4]:section[4] + section[5]]
    if ".modinfo" not in result or not result.get("__versions"):
        raise ValueError("missing modinfo or symbol versions")
    modinfo = result[".modinfo"].decode("ascii").split("\0")
    fields = dict(entry.split("=", 1) for entry in modinfo if "=" in entry)
    if fields.get("name") != "sailboat_coloros_hans":
        raise ValueError("wrong module name")
    vermagic = fields.get("vermagic", "")
    if not vermagic.startswith("5.15.") or "modversions" not in vermagic:
        raise ValueError("expected 5.15 modversions module")
    if b"__cfi_check\0" not in result.get(".strtab", b""):
        raise ValueError("CFI check symbol is missing")
    return {"vermagic": vermagic, "name": fields["name"], "cfi_check": True,
            "versions_bytes": len(result["__versions"])}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ko", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--target-kernel", default="5.15.225-Sailboat-ColorOS-20261118")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    data = args.ko.read_bytes()
    metadata = elf_metadata(data)
    digest = hashlib.sha256(data).hexdigest()
    if "\n" in args.target_kernel or "=" in args.target_kernel or not args.target_kernel.startswith("5.15."):
        raise ValueError("invalid target kernel")
    manifest = (f"format=1\ntarget_kernel={args.target_kernel}\ntarget_abi_verified=0\n"
                f"ko_sha256={digest}\nvermagic={metadata['vermagic']}\n"
                "native_load_blocked=1\nautoload=0\n")
    template = repo / "ksu" / "Sail_Hans"
    files = [(p.name, p.read_bytes(), 0o755 if p.suffix == ".sh" else 0o644)
             for p in sorted(template.iterdir()) if p.is_file() and not p.is_symlink()]
    files.extend([("prototype.manifest", manifest.encode(), 0o644),
                  ("lib/modules/sailboat_coloros_hans.ko", data, 0o644)])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "x", compression=zipfile.ZIP_DEFLATED) as bundle:
        for name, content, mode in files:
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            bundle.writestr(info, content)
    report = dict(metadata, ko_sha256=digest, zip_sha256=hashlib.sha256(args.output.read_bytes()).hexdigest(),
                  target_kernel=args.target_kernel, target_abi_verified=False,
                  native_load_blocked=True, device_tested=False)
    report_path = args.output.with_suffix(".json")
    with report_path.open("x") as output:
        output.write(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(dict(report, zip=str(args.output)), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
