# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Verify/extract local GUI dependency sources into an out-of-source directory."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tarfile


def prepare(destination):
    source = Path(__file__).resolve().parent
    destination = destination.resolve()
    repository = source.parents[2]
    if destination == repository or repository in destination.parents:
        raise ValueError("dependency staging must be outside the source repository")
    lock = json.loads((source / "ui-lock.json").read_text())
    for name, expected in [
        (lock["archive"], lock["sha256"]),
        (lock["manifest"], lock["manifestSha256"]),
    ]:
        if hashlib.sha256((source / name).read_bytes()).hexdigest() != expected:
            raise ValueError("input hash mismatch: " + name)
    records = json.loads((source / lock["manifest"]).read_text())
    expected = {r["path"]: r for r in records}
    if len(expected) != len(records):
        raise ValueError("duplicate manifest entry")
    if destination.exists() and any(destination.iterdir()):
        actual = {
            p.relative_to(destination).as_posix()
            for p in destination.rglob("*")
            if p.is_file()
        }
        if actual != expected.keys():
            raise ValueError("existing staging file set differs")
    else:
        destination.mkdir(parents=True, exist_ok=True)
        with tarfile.open(source / lock["archive"]) as archive:
            members = archive.getmembers()
            if {m.name for m in members} != expected.keys() or len(members) != len(
                expected
            ):
                raise ValueError("archive file set differs")
            for member in members:
                path = PurePosixPath(member.name)
                if not member.isfile() or path.is_absolute() or ".." in path.parts:
                    raise ValueError("unsafe archive member")
                target = destination.joinpath(*path.parts)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.extractfile(member).read())
                target.chmod(0o644)
    for name, record in expected.items():
        p = destination / name
        if (
            p.is_symlink()
            or not p.is_file()
            or hashlib.sha256(p.read_bytes()).hexdigest() != record["sha256"]
        ):
            raise ValueError("staged source differs: " + name)
    print(
        json.dumps(
            dict(
                files=len(expected),
                archiveSha256=lock["sha256"],
                destination=str(destination),
            )
        )
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, required=True)
    prepare(parser.parse_args().destination)
