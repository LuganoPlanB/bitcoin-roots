# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Verify unchanged pinned test sources and extract only outside the repository."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tarfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(destination, *, source=None):
    bundled = Path(__file__).resolve().parent
    source = bundled if source is None else Path(source)
    destination = Path(destination).resolve()
    repository = bundled.parents[2]
    if destination == repository or repository in destination.parents:
        raise ValueError("test inputs must be outside the source repository")
    lock = json.loads((source / "test-inputs-lock.json").read_text())
    for name, expected in [
        (lock["archive"], lock["sha256"]),
        (lock["manifest"], lock["manifestSha256"]),
    ]:
        if digest(source / name) != expected:
            raise ValueError("test input hash mismatch: " + name)
    manifest = json.loads((source / lock["manifest"]).read_text())
    expected = {x["path"]: x["sha256"] for x in manifest["files"]}
    if len(expected) != len(manifest["files"]) or not expected:
        raise ValueError("test manifest must be nonempty and unique")
    if (
        manifest["engineRevision"] != lock["engineRevision"]
        or manifest["upstreamArchiveSha256"] != lock["upstreamArchiveSha256"]
    ):
        raise ValueError("original upstream test source pin differs")
    with tarfile.open(source / lock["archive"]) as archive:
        members = archive.getmembers()
        if (
            len(members) != len(expected)
            or {m.name for m in members} != expected.keys()
        ):
            raise ValueError("test archive file set differs")
        for member in members:
            path = PurePosixPath(member.name)
            if (
                not member.isfile()
                or path.is_absolute()
                or ".." in path.parts
                or member.name == "manifest.json"
            ):
                raise ValueError("unsafe test archive member")
            if (
                hashlib.sha256(archive.extractfile(member).read()).hexdigest()
                != expected[member.name]
            ):
                raise ValueError("test archive source differs: " + member.name)
        if destination.exists() and any(destination.iterdir()):
            actual = {
                p.relative_to(destination).as_posix()
                for p in destination.rglob("*")
                if p.is_file()
            }
            if actual != set(expected) | {"manifest.json"}:
                raise ValueError("existing test source file set differs")
        else:
            destination.mkdir(parents=True, exist_ok=True)
            for member in members:
                target = destination / member.name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.extractfile(member).read())
                target.chmod(0o644)
            (destination / "manifest.json").write_bytes(
                (source / lock["manifest"]).read_bytes()
            )
    for name, wanted in expected.items():
        p = destination / name
        if p.is_symlink() or not p.is_file() or digest(p) != wanted:
            raise ValueError("prepared test source differs: " + name)
    if (destination / "manifest.json").is_symlink() or digest(
        destination / "manifest.json"
    ) != lock["manifestSha256"]:
        raise ValueError("prepared test manifest differs")
    return dict(
        files=len(expected),
        engineRevision=lock["engineRevision"],
        archiveSha256=lock["sha256"],
        destination=str(destination),
    )


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--destination", type=Path, required=True)
    print(json.dumps(prepare(p.parse_args().destination)))
