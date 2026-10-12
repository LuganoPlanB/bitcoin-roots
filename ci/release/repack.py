#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Add reviewed notices to an existing package without changing its payload."""

import argparse
import copy
import hashlib
import io
import json
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile
import zipfile

from archive import validate_archive_root
from notices import ROOT, stage, validate_archive


def digest_stream(stream):
    digest = hashlib.sha256()
    while data := stream.read(1024 * 1024):
        digest.update(data)
    return digest.hexdigest()


def inventory(path):
    entries = {}
    if path.suffix == ".zip":
        with zipfile.ZipFile(path) as archive:
            for member in archive.infolist():
                with archive.open(member) as stream:
                    sha256 = digest_stream(stream)
                entries[member.filename] = (member.external_attr, member.create_system,
                                            member.date_time, sha256)
    else:
        with tarfile.open(path, "r:gz") as archive:
            for member in archive:
                stream = archive.extractfile(member) if member.isfile() else None
                sha256 = digest_stream(stream) if stream is not None else None
                if stream is not None:
                    stream.close()
                entries[member.name] = (member.type, member.mode, member.linkname,
                                        member.uid, member.gid, member.uname,
                                        member.gname, member.mtime, member.pax_headers, sha256)
    return entries


def repack(source, destination, expected_root, inputs, platform):
    if source.resolve() == destination.resolve() or destination.exists() or destination.is_symlink():
        raise ValueError("Repacking requires a new destination; existing assets are immutable")
    if source.suffix != destination.suffix:
        raise ValueError("Repacking must preserve archive format")
    validate_archive_root(source, expected_root)
    before = inventory(source)
    has_copying = any(name.rstrip("/") == f"{expected_root}/COPYING" for name in before)
    if has_copying:
        validate_archive_root(source, expected_root, ROOT / "COPYING")
    if any(name.rstrip("/") == f"{expected_root}/notices" or
           name.startswith(f"{expected_root}/notices/") for name in before):
        raise ValueError("Source already contains notices; refuse to replace them")
    with tempfile.TemporaryDirectory() as temporary_dir:
        root = Path(temporary_dir) / expected_root
        root.mkdir()
        (root / "COPYING").write_bytes((ROOT / "COPYING").read_bytes())
        stage(root, inputs, platform)
        additions = sorted(path for path in root.rglob("*") if path.is_file() and
                           f"{expected_root}/{path.relative_to(root).as_posix()}" not in before)
        # Exclusive creation also rejects a symlink or another asset created
        # between the initial check and opening the destination.
        destination_file = destination.open("xb")
        try:
            if source.suffix == ".zip":
                with zipfile.ZipFile(source) as original, zipfile.ZipFile(destination_file, "w") as output:
                    output.comment = original.comment
                    for member in original.infolist():
                        # Preserve Unix mode/link data and all bytes inside signed apps.
                        copied_member = copy.copy(member)
                        with original.open(member) as stream, output.open(copied_member, "w") as target:
                            shutil.copyfileobj(stream, target, length=1024 * 1024)
                        # zipfile supplies Unix 0600 when this field is zero.
                        # Windows archives can deliberately carry no Unix mode.
                        copied_member.external_attr = member.external_attr
                    for path in additions:
                        member = zipfile.ZipInfo(f"{expected_root}/{path.relative_to(root).as_posix()}")
                        member.create_system = 3
                        member.external_attr = 0o100644 << 16
                        member.compress_type = zipfile.ZIP_DEFLATED
                        output.writestr(member, path.read_bytes())
            else:
                with tarfile.open(source, "r:gz") as original, tarfile.open(fileobj=destination_file, mode="w:gz") as output:
                    for member in original:
                        stream = original.extractfile(member) if member.isfile() else None
                        output.addfile(copy.copy(member), stream)
                        if stream is not None:
                            stream.close()
                    for path in additions:
                        data = path.read_bytes()
                        member = tarfile.TarInfo(f"{expected_root}/{path.relative_to(root).as_posix()}")
                        member.mode = 0o644
                        member.size = len(data)
                        output.addfile(member, io.BytesIO(data))
            destination_file.close()
            validate_archive_root(destination, expected_root, ROOT / "COPYING")
            validate_archive(destination, expected_root, platform)
            after = inventory(destination)
            if any(after.get(name) != metadata for name, metadata in before.items()):
                raise ValueError("Repacking changed existing archive content or metadata")
            allowed = {f"{expected_root}/{path.relative_to(root).as_posix()}" for path in additions}
            if set(after) - set(before) != allowed:
                raise ValueError("Repacking introduced unexpected members")
        except Exception:
            destination_file.close()
            destination.unlink(missing_ok=True)
            raise
    return {"source": str(source), "destination": str(destination),
            "unchanged_members": len(before), "added_notice_members": len(allowed),
            "payload_bytes_modes_links_unchanged": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("expected_root")
    parser.add_argument("notice_inputs", type=Path)
    parser.add_argument("platform")
    args = parser.parse_args()
    try:
        print(json.dumps(repack(args.source, args.destination, args.expected_root, args.notice_inputs, args.platform), sort_keys=True))
    except (OSError, ValueError, KeyError, tarfile.TarError, zipfile.BadZipFile) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
