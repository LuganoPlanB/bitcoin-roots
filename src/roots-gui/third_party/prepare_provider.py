# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license; see COPYING.
"""Verify and unpack local provider inputs into a new out-of-source directory."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import posixpath
import shutil
import subprocess
import tarfile


def unpack(archive, destination):
    with tarfile.open(archive) as source:
        for item in source.getmembers():
            path = PurePosixPath(item.name)
            if path.is_absolute() or ".." in path.parts or not path.parts:
                raise RuntimeError("unsafe source archive path")
            if not (item.isfile() or item.isdir() or item.issym()):
                raise RuntimeError("unsupported source archive member")
            if item.issym():
                target = PurePosixPath(posixpath.normpath(str(path.parent / item.linkname)))
                if PurePosixPath(item.linkname).is_absolute() or ".." in target.parts or target.parts[0] != path.parts[0]:
                    raise RuntimeError("source archive link escapes input root")
        if hasattr(tarfile, "data_filter"):
            source.extractall(destination, filter="data")
        else: # Python 3.10: paths/types/links were checked before any writes.
            source.extractall(destination)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    destination = args.destination.resolve()
    repository = root.parents[2]
    if destination == repository or repository in destination.parents:
        raise RuntimeError("provider extraction must be out of source")
    if destination.exists() and any(destination.iterdir()):
        raise RuntimeError("provider extraction requires a fresh empty directory")
    lock = json.loads((root / "provider-lock.json").read_text())
    for dependency in [lock["libwally"], lock["bips_vectors"]]:
        archive = root / dependency["archive"]
        if hashlib.sha256(archive.read_bytes()).hexdigest() != dependency["sha256"]:
            raise RuntimeError("provider archive hash mismatch")
    for patch in lock["libwally"]["patches"]:
        if hashlib.sha256((root / patch["path"]).read_bytes()).hexdigest() != patch["sha256"]:
            raise RuntimeError("provider patch hash mismatch")
    for license_text in lock.get("license_texts", []):
        if hashlib.sha256((root / license_text["path"]).read_bytes()).hexdigest() != license_text["sha256"]:
            raise RuntimeError("provider license text hash mismatch")
    destination.mkdir(parents=True, exist_ok=True)
    for dependency in [lock["libwally"], lock["bips_vectors"]]:
        unpack(root / dependency["archive"], destination)
    provider = destination / "libwally-core-1.6.1"
    for patch in lock["libwally"]["patches"]:
        subprocess.run(["git", "apply", "--check", str(root / patch["path"])], cwd=provider, check=True)
        subprocess.run(["git", "apply", str(root / patch["path"])], cwd=provider, check=True)
    # Upstream deliberately prefixes its CMake names with '_'. Copy aliases
    # only into this disposable staging tree; preserve every upstream input.
    for relative in [".", "src", "src/ctest"]:
        directory = provider / relative
        shutil.copy2(directory / "_CMakeLists.txt", directory / "CMakeLists.txt")
    shutil.copytree(provider / "_cmake", provider / "cmake")
    print(json.dumps({"provider": str(provider), "bips": str(destination / "bips-provider-vectors"),
                      "revision": lock["libwally"]["revision"], "local_inputs_verified": True}))


if __name__ == "__main__":
    main()
