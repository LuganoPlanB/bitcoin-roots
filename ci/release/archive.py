#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import argparse
import os
from pathlib import Path
import re
import sys
import tarfile
import zipfile


VERSION_TAG_RE = re.compile(
    r"^v[0-9]+\.[0-9]+(?:\.[0-9]+)?(?:[-+][0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?$"
)
GIT_SHA_RE = re.compile(r"^[0-9A-Fa-f]{12,64}$")


def archive_root_name(release_tag, git_sha):
    if release_tag:
        if not VERSION_TAG_RE.fullmatch(release_tag):
            raise ValueError(f"Invalid release version tag: {release_tag}")
        version = release_tag.removeprefix("v")
    else:
        if not GIT_SHA_RE.fullmatch(git_sha):
            raise ValueError("GITHUB_SHA must contain at least 12 hexadecimal characters")
        version = f"git-{git_sha[:12].lower()}"
    return f"bitcoin-roots-{version}"


def archive_member_names(archive):
    if archive.name.endswith(".tar.gz"):
        with tarfile.open(archive, mode="r:gz") as package:
            return [member.name for member in package.getmembers()]
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as package:
            return package.namelist()
    raise ValueError(f"Unsupported release archive: {archive.name}")


def validate_archive_root(archive, expected_root):
    names = archive_member_names(archive)
    if not names:
        raise ValueError(f"Release archive is empty: {archive.name}")

    has_payload = False
    for name in names:
        normalized = name.rstrip("/")
        if not normalized:
            continue
        parts = normalized.split("/")
        if "\\" in normalized or any(part in {"", ".", ".."} for part in parts):
            raise ValueError(f"Unsafe path in {archive.name}: {name}")
        if parts[0] != expected_root:
            raise ValueError(
                f"Expected root directory {expected_root} in {archive.name}, found: {name}"
            )
        has_payload |= len(parts) > 1

    if not has_payload:
        raise ValueError(f"Release archive has no files below {expected_root}: {archive.name}")


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    root_parser = subparsers.add_parser("root-name")
    root_parser.add_argument("--tag", default=os.environ.get("RELEASE_TAG", ""))
    root_parser.add_argument("--sha", default=os.environ.get("GITHUB_SHA", ""))

    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("archive", type=Path)
    validate_parser.add_argument("expected_root")

    args = parser.parse_args()
    try:
        if args.command == "root-name":
            print(archive_root_name(args.tag, args.sha))
        else:
            validate_archive_root(args.archive, args.expected_root)
    except (OSError, ValueError, tarfile.TarError, zipfile.BadZipFile) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
