#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Validate a pinned, reviewed unsigned asset packet and optionally create its draft."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import tarfile
import zipfile

import archive

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "ci/release"
PACKAGE_NAMES = {
    f"bitcoin-roots-{platform}.{'tar.gz' if platform.startswith('linux') else 'zip'}": platform
    for platform in ("linux-x86_64", "linux-aarch64", "darwin-x86_64", "darwin-arm64", "windows-x86_64")
}


def run(command, **kwargs):
    return subprocess.run(command, check=True, capture_output=True, text=True, **kwargs).stdout.strip()


def digest(path, algorithm):
    result = hashlib.new(algorithm)
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            result.update(chunk)
    return result.hexdigest()


def snapshot_packet(args, destination):
    patch_name = f"bitcoin-roots-{args.tag[1:]}.patch"
    expected = {*PACKAGE_NAMES, patch_name, "SHA512SUMS"}
    if not args.assets.is_dir() or args.assets.is_symlink():
        raise ValueError("Assets must be a regular directory")
    if {path.name for path in args.assets.iterdir()} != expected:
        raise ValueError("Expected exactly five platform packages, the release patch and SHA512SUMS")
    for name in sorted(expected):
        source = args.assets / name
        if source.is_symlink() or not source.is_file() or source.stat().st_size == 0:
            raise ValueError(f"Asset must be a nonempty regular file: {name}")
        shutil.copyfile(source, destination / name)
    if digest(destination / "SHA512SUMS", "sha512") != args.manifest_sha512:
        raise ValueError("SHA512SUMS differs from the reviewed digest")
    entries = {}
    for line in (destination / "SHA512SUMS").read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        match = re.fullmatch(r"([0-9a-f]{128})  (.+)", line)
        if not match or match[2] not in expected - {"SHA512SUMS"} or match[2] in entries:
            raise ValueError("Invalid, duplicate or unexpected manifest entry")
        entries[match[2]] = match[1]
    if set(entries) != expected - {"SHA512SUMS"}:
        raise ValueError("Manifest must contain exactly the six release assets")
    for name, checksum in entries.items():
        if digest(destination / name, "sha512") != checksum:
            raise ValueError(f"Release checksum mismatch: {name}")
    expected_root = archive.archive_root_name(args.tag, None)
    for name in PACKAGE_NAMES:
        archive.validate_archive_root(destination / name, expected_root, ROOT / "COPYING")
    if args.notes.is_symlink() or not args.notes.is_file() or not args.notes.stat().st_size:
        raise ValueError("Reviewed release notes must be a nonempty regular file")
    # Use a private snapshot for upload so changes to the input packet cannot
    # substitute different bytes between validation and gh release create.
    notes_path = destination / "reviewed-notes.md"
    shutil.copyfile(args.notes, notes_path)
    if digest(notes_path, "sha256") != args.notes_sha256:
        raise ValueError("Release notes differ from the reviewed digest")
    return sorted(expected), patch_name, notes_path


def validate_source(args, patch, temporary):
    if Path(run(["git", "rev-parse", "--show-toplevel"])).resolve() != ROOT:
        raise ValueError("Run the draft helper from its own tagged source checkout")
    run(["git", "ls-files", "--error-unmatch", "ci/release/create-draft.py"])
    if run(["git", "rev-parse", "HEAD^{commit}"]) != args.commit:
        raise ValueError("HEAD does not match the reviewed release commit")
    if run(["git", "status", "--porcelain"]):
        raise ValueError("Release source has staged, unstaged or untracked changes")
    tag_object = run(["git", "rev-parse", f"refs/tags/{args.tag}^{{tag}}"])
    if run(["git", "rev-parse", f"refs/tags/{args.tag}^{{commit}}"]) != args.commit:
        raise ValueError("Annotated release tag does not match the reviewed commit")
    core_version = args.tag[1:].split("-roots.", 1)[0]
    environment = {**os.environ, "RELEASE_CANONICAL_REF": f"refs/tags/{args.tag}",
                   "RELEASE_CORE_REF": f"refs/tags/v{core_version}"}
    # The actual remote canonical tip is independently checked below; this
    # local override avoids treating a stale remote-tracking ref as evidence.
    run([str(SCRIPTS / "validate-release-source.sh"), args.tag], env=environment)
    regenerated = temporary / "regenerated.patch"
    run([str(SCRIPTS / "create-patch-series.sh"), args.tag, str(regenerated)], env=environment)
    if digest(regenerated, "sha512") != digest(patch, "sha512"):
        raise ValueError("Delivered patch differs from the replay-verified tagged series")
    return tag_object, core_version


def validate_remote(args, tag_object, core_version):
    canonical = f"refs/heads/roots/{core_version}"
    tag = f"refs/tags/{args.tag}"
    output = run(["git", "ls-remote", f"https://github.com/{args.repository}.git",
                  canonical, tag, f"{tag}^{{}}"])
    refs = {}
    for line in output.splitlines():
        oid, ref = line.split("\t")
        if ref in refs or not re.fullmatch(r"[0-9a-f]{40}", oid):
            raise ValueError("Invalid remote ref response")
        refs[ref] = oid
    if refs != {canonical: args.commit, tag: tag_object, f"{tag}^{{}}": args.commit}:
        raise ValueError("Remote canonical tip or annotated tag identity differs from the reviewed source")
    pages = json.loads(run(["gh", "api", "--paginate", "--slurp",
                           f"repos/{args.repository}/releases?per_page=100"]))
    if not isinstance(pages, list) or any(not isinstance(page, list) for page in pages):
        raise ValueError("Invalid release inventory response")
    for page in pages:
        for release in page:
            if not isinstance(release, dict) or "tag_name" not in release:
                raise ValueError("Invalid release inventory entry")
            if release["tag_name"] == args.tag:
                raise ValueError("Release already exists; refusing to replace any draft or public assets")


def prepare_draft(args):
    if not archive.VERSION_TAG_RE.fullmatch(args.tag):
        raise ValueError("Invalid Roots release tag")
    for value, pattern, name in (
        (args.commit, r"[0-9a-f]{40}", "release commit"),
        (args.manifest_sha512, r"[0-9a-f]{128}", "reviewed manifest SHA512"),
        (args.notes_sha256, r"[0-9a-f]{64}", "reviewed notes SHA256"),
        (args.repository, r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", "GitHub repository"),
    ):
        if not re.fullmatch(pattern, value):
            raise ValueError(f"Invalid {name}")
    with tempfile.TemporaryDirectory(prefix="roots-reviewed-draft-") as directory:
        temporary = Path(directory)
        assets = temporary / "assets"
        assets.mkdir()
        names, patch_name, notes_path = snapshot_packet(args, assets)
        tag_object, core_version = validate_source(args, assets / patch_name, temporary)
        # Refresh irreversible-action identities after the potentially slow
        # package inspection and patch replay, immediately before draft creation.
        validate_remote(args, tag_object, core_version)
        if args.create_draft:
            command = ["gh", "release", "create", args.tag, "--repo", args.repository,
                       "--draft", "--verify-tag", "--title", f"Bitcoin Roots {args.tag}",
                       "--notes-file", str(notes_path)]
            if re.search(r"rc[0-9]+-roots\.", args.tag):
                command.append("--prerelease")
            command.extend(str(assets / name) for name in names)
            print(run(command))
        else:
            print("Validated immutable source, six assets, canonical COPYING, patch replay and reviewed notes; no draft created")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--assets", required=True, type=Path)
    parser.add_argument("--notes", required=True, type=Path)
    parser.add_argument("--manifest-sha512", required=True)
    parser.add_argument("--notes-sha256", required=True)
    parser.add_argument("--create-draft", action="store_true",
                        help="Create a new draft after verification; requires separate release authority")
    args = parser.parse_args()
    try:
        prepare_draft(args)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError, tarfile.TarError, zipfile.BadZipFile) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
