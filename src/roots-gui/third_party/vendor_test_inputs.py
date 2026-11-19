# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Repack verified original test sources; acquisition/license review is separate."""

import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import tarfile


def package(source, output):
    manifest_path = Path(__file__).with_name("test_inputs") / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    expected = {x["path"]: x["sha256"] for x in manifest["files"]}
    actual = {
        p.relative_to(source).as_posix()
        for p in source.rglob("*")
        if p.is_file() and p != source / "manifest.json"
    }
    if actual != expected.keys():
        raise ValueError("original source file set differs from pinned manifest")
    with (
        output.open("wb") as raw,
        gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed,
    ):
        with tarfile.open(
            fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT
        ) as archive:
            for name in sorted(expected):
                p = source / name
                data = p.read_bytes()
                if p.is_symlink() or hashlib.sha256(data).hexdigest() != expected[name]:
                    raise ValueError("original source identity differs: " + name)
                member = tarfile.TarInfo(name)
                member.size = len(data)
                member.mode = 0o644
                member.mtime = member.uid = member.gid = 0
                member.uname = member.gname = ""
                archive.addfile(member, io.BytesIO(data))
    print(
        json.dumps(
            dict(
                files=len(expected),
                sha256=hashlib.sha256(output.read_bytes()).hexdigest(),
            )
        )
    )


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    package(a.source.resolve(), a.output.resolve())
