#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import argparse
from collections import deque
import os
from pathlib import Path
import re
import sys
import tarfile
import zipfile


VERSION_TAG_RE = re.compile(r"^v[0-9]+\.[0-9]+(?:\.[0-9]+|rc[0-9]+)?-roots\.[1-9][0-9]*$")
GIT_SHA_RE = re.compile(r"^[0-9A-Fa-f]{12,64}$")


def archive_root_name(release_tag, git_sha):
    if release_tag:
        if not VERSION_TAG_RE.fullmatch(release_tag):
            raise ValueError(f"Invalid Roots release tag: {release_tag}")
        version = release_tag.removeprefix("v")
    else:
        if not GIT_SHA_RE.fullmatch(git_sha):
            raise ValueError("GITHUB_SHA must contain at least 12 hexadecimal characters")
        version = f"git-{git_sha[:12].lower()}"
    return f"bitcoin-roots-{version}"


def archive_members(archive):
    if archive.name.endswith(".tar.gz"):
        with tarfile.open(archive, mode="r:gz") as package:
            return package.getmembers()
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as package:
            return package.infolist()
    raise ValueError(f"Unsupported release archive: {archive.name}")


def validate_archive_root(archive, expected_root, copying=None):
    members = archive_members(archive)
    if not members:
        raise ValueError(f"Release archive is empty: {archive.name}")

    has_payload = False
    paths = set()
    symlinks = {}
    license_member = None
    for member in members:
        name = member.name if isinstance(member, tarfile.TarInfo) else member.filename
        normalized = name.rstrip("/")
        if not normalized:
            continue
        parts = normalized.split("/")
        if "\\" in normalized or any(part in {"", ".", ".."} for part in parts):
            raise ValueError(f"Unsafe path in {archive.name}: {name}")
        if normalized in paths:
            raise ValueError(f"Duplicate archive member in {archive.name}: {name}")
        paths.add(normalized)
        if isinstance(member, tarfile.TarInfo):
            if not (member.isfile() or member.isdir()):
                raise ValueError(f"Unsupported archive member in {archive.name}: {name}")
            is_regular_file = member.isfile()
        else:
            member_type = (member.external_attr >> 16) & 0o170000
            if member_type not in {0, 0o100000, 0o40000, 0o120000}:
                raise ValueError(f"Unsupported archive member in {archive.name}: {name}")
            if member_type == 0o120000:
                symlinks[normalized] = member
            is_regular_file = not member.is_dir() and member_type in {0, 0o100000}
        if parts[0] != expected_root:
            raise ValueError(
                f"Expected root directory {expected_root} in {archive.name}, found: {name}"
            )
        has_payload |= len(parts) > 1 and is_regular_file
        if normalized == f"{expected_root}/COPYING" and is_regular_file:
            license_member = member

    if symlinks:
        with zipfile.ZipFile(archive) as package:
            for name, member in symlinks.items():
                if not 0 < member.file_size <= 4096:
                    raise ValueError(f"Invalid symlink target in {archive.name}: {name}")
                try:
                    target = package.read(member).decode("utf-8")
                except UnicodeDecodeError as error:
                    raise ValueError(f"Invalid symlink target in {archive.name}: {name}") from error
                if target.startswith("/") or "\\" in target or "\0" in target or re.match(r"^[A-Za-z]:", target):
                    raise ValueError(f"Unsafe symlink target in {archive.name}: {name}")
                symlinks[name] = target

        def resolve_target(path):
            pending = deque(path.split("/"))
            parts = []
            expanding = set()
            while pending:
                part = pending.popleft()
                # End markers limit cycle detection to active target expansion.
                # A later path component may legitimately traverse the same link.
                if isinstance(part, tuple):
                    expanding.remove(part[0])
                    continue
                if part in {"", "."}:
                    continue
                if part == "..":
                    if len(parts) <= 1:
                        raise ValueError(f"Symlink escapes archive root in {archive.name}: {path}")
                    parts.pop()
                    continue
                parts.append(part)
                if parts[0] != expected_root:
                    raise ValueError(f"Symlink escapes archive root in {archive.name}: {path}")
                prefix = "/".join(parts)
                if prefix in symlinks:
                    if prefix in expanding:
                        raise ValueError(f"Symlink cycle in {archive.name}: {prefix}")
                    expanding.add(prefix)
                    parts.pop()
                    pending.extendleft(reversed([*symlinks[prefix].split("/"), (prefix,)]))
            resolved = "/".join(parts)
            if not parts or (resolved not in paths and not any(name.startswith(resolved + "/") for name in paths)):
                raise ValueError(f"Missing symlink target in {archive.name}: {path}")
            return resolved

        for name in symlinks:
            resolve_target(name)
        # Member paths may themselves pass through a link. Validate those too,
        # so extraction cannot redirect an otherwise valid-looking payload.
        for name in paths:
            resolve_target(name)

    if not has_payload:
        raise ValueError(f"Release archive has no files below {expected_root}: {archive.name}")
    if copying is not None:
        if license_member is None:
            raise ValueError(f"Release archive is missing regular COPYING: {archive.name}")
        # Git checkouts may use CRLF on Windows. Accept only the exact
        # canonical text in uniform LF or CRLF form, without changing payloads.
        expected_notice = copying.read_bytes().replace(b"\r\n", b"\n")
        accepted_notices = (expected_notice, expected_notice.replace(b"\n", b"\r\n"))
        notice_size = license_member.size if isinstance(license_member, tarfile.TarInfo) else license_member.file_size
        if notice_size not in {len(notice) for notice in accepted_notices}:
            raise ValueError(f"Release archive COPYING differs from canonical notice: {archive.name}")
        if isinstance(license_member, tarfile.TarInfo):
            with tarfile.open(archive, mode="r:gz") as package:
                notice = package.extractfile(license_member).read()
        else:
            with zipfile.ZipFile(archive) as package:
                notice = package.read(license_member)
        if notice not in accepted_notices:
            raise ValueError(f"Release archive COPYING differs from canonical notice: {archive.name}")


def stage_copying(directory):
    if not directory.is_dir():
        raise ValueError(f"License staging directory does not exist: {directory}")
    destination = directory / "COPYING"
    if destination.is_symlink():
        raise ValueError(f"License staging refuses a COPYING symlink: {directory}")
    # Stage the same LF bytes regardless of the runner checkout convention.
    destination.write_bytes((Path(__file__).resolve().parents[2] / "COPYING").read_bytes().replace(b"\r\n", b"\n"))


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    root_parser = subparsers.add_parser("root-name")
    root_parser.add_argument("--tag", default=os.environ.get("RELEASE_TAG", ""))
    root_parser.add_argument("--sha", default=os.environ.get("GITHUB_SHA", ""))

    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("archive", type=Path)
    validate_parser.add_argument("expected_root")
    validate_parser.add_argument("--copying", type=Path)

    copying_parser = subparsers.add_parser("stage-copying")
    copying_parser.add_argument("directory", type=Path)

    args = parser.parse_args()
    try:
        if args.command == "root-name":
            print(archive_root_name(args.tag, args.sha))
        elif args.command == "stage-copying":
            stage_copying(args.directory)
        else:
            validate_archive_root(args.archive, args.expected_root, args.copying)
    except (OSError, ValueError, tarfile.TarError, zipfile.BadZipFile) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
