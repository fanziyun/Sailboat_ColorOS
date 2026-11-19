#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import shutil
import subprocess
import tarfile
import urllib.request

PACKAGES = (
    ("kdir", "android13-5.15", 836831452, "ba6ad95bb81d84eaa97a729fc1b9a935b680c14db56b73420051543561e07f0e"),
    ("clang", "clang-r450784e", 454419780, "d235aee8bd426c875fcb4b2f7b237d56df235d8e2ba2e18fc1fe4703f2bd4081"),
    ("src", "android13-5.15", 152756849, "65c15304890ee8cd6de39ba5ce9c609590e1c7c452bf7a08e2d0641b34768a13"),
)
ENDPOINT = "https://cnb.cool/Ylarod/ddk-lfs.git/info/lfs/objects/batch"


def download(package, destination, cache_root=None):
    kind, name, size, digest = package
    if cache_root:
        cached = cache_root / f"download-{kind}" / f"{kind}.{name}.tar.zst"
        if cached.is_file() and cached.stat().st_size == size:
            with cached.open("rb") as source:
                actual = hashlib.file_digest(source, "sha256").hexdigest()
            if actual != digest:
                raise RuntimeError(f"cached archive checksum mismatch: {cached}")
            print(f"reusing verified {kind}: {cached}", flush=True)
            return cached
    payload = json.dumps({"operation": "download", "transfers": ["basic"],
                          "objects": [{"oid": digest, "size": size}]}).encode()
    request = urllib.request.Request(ENDPOINT, data=payload, headers={
        "Content-Type": "application/vnd.git-lfs+json", "Accept": "application/vnd.git-lfs+json"})
    with urllib.request.urlopen(request, timeout=60) as response:
        obj = json.load(response)["objects"][0]
    if obj.get("oid") != digest or obj.get("size") != size or "error" in obj:
        raise RuntimeError(f"unexpected LFS metadata for {kind}")
    action = obj["actions"]["download"]
    url = action["href"]
    if not url.startswith("https://lfs.cnb.cool/"):
        raise RuntimeError("unexpected LFS download host")
    archive = destination / f"{kind}.{name}.tar.zst"
    headers = dict(action.get("header", {}))
    headers.setdefault("Accept", "application/vnd.git-lfs")
    hasher = hashlib.sha256()
    total = 0
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=120) as response:
        with archive.open("xb") as output:
            while chunk := response.read(1024 * 1024):
                total += len(chunk)
                if total > size:
                    raise RuntimeError("download exceeds pinned size")
                output.write(chunk)
                hasher.update(chunk)
    if total != size or hasher.hexdigest() != digest:
        raise RuntimeError(f"size/SHA256 mismatch for {archive}")
    print(f"verified {kind}: {total} bytes {digest}", flush=True)
    return archive


def contained(path, root):
    return path == root or root in path.parents


def extract(archive, destination, sdk_root):
    process = subprocess.Popen(["zstd", "-dc", str(archive)], stdout=subprocess.PIPE)
    links = []
    seen = set()
    total = 0
    try:
        with tarfile.open(fileobj=process.stdout, mode="r|") as stream:
            for member in stream:
                name = PurePosixPath(member.name)
                if name.is_absolute() or ".." in name.parts:
                    raise RuntimeError(f"unsafe archive path: {member.name}")
                normalized = str(name)
                if normalized in ("", "."):
                    continue
                target = destination.joinpath(*name.parts)
                if not contained(target, destination):
                    raise RuntimeError("archive path escapes destination")
                if normalized in seen and not member.isdir():
                    raise RuntimeError(f"duplicate archive path: {member.name}")
                seen.add(normalized)
                if member.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                elif member.isfile():
                    total += member.size
                    if total > 20 * 1024**3:
                        raise RuntimeError("archive expands beyond limit")
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with stream.extractfile(member) as source, target.open("xb") as output:
                        shutil.copyfileobj(source, output)
                    target.chmod(member.mode & 0o777)
                elif member.issym() or member.islnk():
                    if member.islnk():
                        resolved = destination / posixpath.normpath(member.linkname)
                    elif member.linkname.startswith("/opt/ddk/"):
                        resolved = sdk_root / member.linkname.removeprefix("/opt/ddk/")
                    elif member.linkname.startswith("/"):
                        raise RuntimeError(f"external absolute link: {member.linkname}")
                    else:
                        resolved = Path(os.path.normpath(target.parent / member.linkname))
                    resolved = Path(os.path.normpath(resolved))
                    if not contained(resolved, sdk_root):
                        raise RuntimeError(f"link outside isolated SDK: {member.name}")
                    links.append((target, resolved, member.islnk()))
                else:
                    raise RuntimeError(f"special archive member: {member.name}")
        process.stdout.close()
        if process.wait() != 0:
            raise RuntimeError("zstd extraction failed")
        for target, resolved, hard in links:
            target.parent.mkdir(parents=True, exist_ok=True)
            if hard:
                if not resolved.is_file() or resolved.is_symlink():
                    raise RuntimeError(f"unsafe/unresolved hardlink: {target}")
                os.link(resolved, target)
            else:
                target.symlink_to(os.path.relpath(resolved, target.parent))
    except BaseException:
        process.kill()
        process.wait()
        raise
    finally:
        process.stdout.close()
    print(f"extracted {archive.name}: {total} bytes", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--cache-root", type=Path)
    args = parser.parse_args()
    root = args.output.resolve()
    if shutil.disk_usage(root.parent).free < 12 * 1024**3:
        raise RuntimeError("need at least 12 GiB free")
    root.mkdir(exist_ok=False)
    sdk = root / "sdk" / "opt" / "ddk"
    sdk.mkdir(parents=True)
    for package in PACKAGES:
        kind, name, size, digest = package
        downloads = root / f"download-{kind}"
        downloads.mkdir()
        destination = sdk / kind
        destination.mkdir()
        archive = download(package, downloads, args.cache_root)
        extract(archive, destination, sdk)
    generated_makefile = sdk / "kdir/android13-5.15/Makefile"
    text = generated_makefile.read_text()
    old_include = "include /opt/ddk/src/android13-5.15/Makefile"
    if old_include not in text:
        raise RuntimeError("unexpected generated DDK Makefile")
    generated_makefile.write_text(text.replace(old_include, f"include {sdk}/src/android13-5.15/Makefile", 1))
    (root / "manifest.json").write_text(json.dumps({
        "source": "cnb.cool/Ylarod/ddk-prebuilts",
        "source_commit": "8b6e66bd195c779fb8665315249f146206dae496",
        "packages": [{"kind": p[0], "name": p[1], "bytes": p[2], "sha256": p[3]} for p in PACKAGES],
    }, indent=2) + "\n")
    print(f"SDK_READY={sdk}", flush=True)


if __name__ == "__main__":
    main()
