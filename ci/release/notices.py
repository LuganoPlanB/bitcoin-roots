#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
TRACKED_NOTICES = {
    "secp256k1": "src/secp256k1/COPYING",
    "minisketch": "src/minisketch/LICENSE",
    "leveldb": "src/leveldb/LICENSE",
    "crc32c": "src/crc32c/LICENSE",
    "ctaes": "src/crypto/ctaes/COPYING",
    "libmultiprocess": "src/ipc/libmultiprocess/COPYING",
    "RobotoMono-Apache2": "contrib/roots/notices/RobotoMono-APACHE-2.0.txt",
    "RobotoMono-attribution": "contrib/roots/notices/RobotoMono-NOTICE.txt",
}
COMMON_EXTERNAL = {"qt", "boost", "libevent", "zeromq", "qrencode", "sqlite", "miniupnpc"}
MACOS_EXTERNAL = {"pcre2", "libsodium", "brotli", "graphite2", "md4c", "glib", "libpng", "openssl", "freetype", "harfbuzz", "icu", "gettext", "dbus", "zstd", "libb2", "double-conversion"}
WINDOWS_EXTERNAL = {"zlib", "libpng", "pcre2", "bzip2", "brotli", "freetype", "double-conversion", "libiconv"}
PLATFORMS = {"linux-x86_64", "linux-aarch64", "darwin-x86_64", "darwin-arm64", "windows-x86_64"}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def required_components(platform):
    if platform not in PLATFORMS:
        raise ValueError(f"Unknown notice platform: {platform}")
    return COMMON_EXTERNAL | (MACOS_EXTERNAL if platform.startswith("darwin") else {"capnp"} if platform.startswith("linux") else WINDOWS_EXTERNAL)


def safe_path(path):
    if not isinstance(path, str) or not path or "\x00" in path or PurePosixPath(path).is_absolute() or "\\" in path or any(x in {"", ".", ".."} for x in path.split("/")):
        raise ValueError(f"Unsafe notice path: {path}")
    return path


def validate_index(index, read, expected_platform):
    if index.get("format") != 1:
        raise ValueError("Unsupported notice index format")
    required = required_components(expected_platform)
    if index.get("platform") != expected_platform:
        raise ValueError(f"Notice platform mismatch: expected {expected_platform}, found {index.get('platform')}")
    components = index.get("components", {})
    if not required <= components.keys():
        raise ValueError(f"Missing dependency notice inputs: {sorted(required - components.keys())}")
    for component, metadata in components.items():
        if not metadata.get("version") or not metadata.get("source") or not re.fullmatch(r"[0-9a-f]{64}", metadata.get("source_sha256", "")):
            raise ValueError(f"Missing exact notice provenance: {component}")
        if not metadata.get("files") and not (component == "sqlite" and index["platform"].startswith("darwin") and metadata.get("role") == "system-library"):
            raise ValueError(f"Missing notice text: {component}")
        provenance_type = metadata.get("provenance_type")
        if provenance_type not in {"build-receipt", "pinned-source-archive", "versioned-primary-notice", "source-version-stability"}:
            raise ValueError(f"Unsupported notice provenance: {component}")
        evidence = metadata.get("evidence")
        if evidence not in index.get("files", {}):
            raise ValueError(f"Missing retained notice provenance: {component}")
        if metadata["version"] == "unknown" and (provenance_type != "source-version-stability" or len(metadata.get("notice_stable_versions", [])) < 2):
            raise ValueError(f"Unknown version requires explicit notice stability evidence: {component}")
        for path in metadata.get("files", []):
            if path not in index.get("files", {}):
                raise ValueError(f"Missing indexed notice: {path}")
    for path, sha256 in index.get("files", {}).items():
        safe_path(path)
        data = read(path)
        if not data or digest(data) != sha256:
            raise ValueError(f"Notice bytes differ from index: {path}")
    for component, source in TRACKED_NOTICES.items():
        path = f"tracked/{component}.txt"
        if path not in index.get("files", {}) or read(path) != (ROOT / source).read_bytes():
            raise ValueError(f"Missing or altered tracked notice: {component}")


def stage(directory, inputs, expected_platform):
    index = json.loads(inputs.read_text())
    source_directory = inputs.parent
    def read(path):
        safe_path(path)
        source = source_directory / path
        if source.is_symlink() or not source.is_file() or not source.resolve().is_relative_to(source_directory.resolve()):
            raise ValueError(f"Notice input is not a regular file: {path}")
        return source.read_bytes()
    validate_index(index, read, expected_platform)
    destination = directory / "notices"
    if not directory.is_dir() or destination.exists() or destination.is_symlink():
        raise ValueError("Notice staging requires an existing root without notices")
    destination.mkdir()
    for path in sorted(index["files"]):
        target = destination / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(read(path))
    (destination / "index.json").write_text(json.dumps(index, indent=2, sort_keys=True) + "\n")


def collect(platform, descriptor, destination):
    """Copy reviewed notices and their retained primary-source evidence.

    Dependency versions and linkage evidence belong to the build inventory,
    never to a guess based on a dylib's ABI number. Evidence can be an original
    receipt or independently verified source selection with log/binary evidence.
    Unknown patch versions remain unknown when notice bytes are proven stable.
    """
    dependencies = json.loads(descriptor.read_text())
    index = {"format": 1, "platform": platform, "components": {}, "files": {}}
    payload = {}
    for component, source in TRACKED_NOTICES.items():
        path = f"tracked/{component}.txt"
        payload[path] = (ROOT / source).read_bytes()
    for component, metadata in dependencies.items():
        safe_path(component)
        source = Path(metadata.get("evidence", metadata.get("receipt", "")))
        if not source.is_absolute():
            source = descriptor.parent / source
        if source.is_symlink() or not source.is_file():
            raise ValueError(f"Missing retained primary-source evidence: {component}")
        entry = {"version": metadata["version"], "source": metadata["source"],
                 "source_sha256": metadata.get("source_sha256", digest(source.read_bytes())),
                 "provenance_type": metadata.get("provenance_type", "build-receipt"),
                 "role": metadata.get("role", "build-dependency"), "files": []}
        if "notice_stable_versions" in metadata:
            entry["notice_stable_versions"] = metadata["notice_stable_versions"]
        evidence = f"provenance/{component}/{source.name}"
        safe_path(evidence)
        payload[evidence] = source.read_bytes()
        entry["evidence"] = evidence
        for number, filename in enumerate(metadata["files"]):
            notice = Path(filename)
            if not notice.is_absolute():
                notice = descriptor.parent / notice
            if notice.is_symlink() or not notice.is_file():
                raise ValueError(f"Missing exact dependency notice: {component}: {filename}")
            path = f"external/{component}/{number}-{notice.name}"
            safe_path(path)
            payload[path] = notice.read_bytes()
            entry["files"].append(path)
        index["components"][component] = entry
    index["files"] = {path: digest(data) for path, data in payload.items()}
    validate_index(index, payload.__getitem__, platform)
    if destination.exists():
        raise ValueError("Notice collection destination already exists")
    destination.mkdir(parents=True)
    for path, data in payload.items():
        target = destination / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    (destination / "index.json").write_text(json.dumps(index, indent=2, sort_keys=True) + "\n")


def validate_archive(archive, expected_root, expected_platform):
    prefix = f"{expected_root}/notices/"
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as package:
            def read(path):
                member = package.getinfo(prefix + safe_path(path))
                if member.is_dir() or (member.external_attr >> 16) & 0o170000 not in {0, 0o100000}:
                    raise ValueError(f"Notice is not a regular member: {path}")
                return package.read(member)
            validate_index(json.loads(read("index.json")), read, expected_platform)
    else:
        with tarfile.open(archive) as package:
            def read(path):
                member = package.getmember(prefix + safe_path(path))
                if not member.isfile():
                    raise ValueError(f"Notice is not a regular member: {path}")
                return package.extractfile(member).read()
            validate_index(json.loads(read("index.json")), read, expected_platform)


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    staging = commands.add_parser("stage")
    staging.add_argument("directory", type=Path)
    staging.add_argument("inputs", type=Path)
    staging.add_argument("platform", choices=sorted(PLATFORMS))
    validation = commands.add_parser("validate")
    validation.add_argument("archive", type=Path)
    validation.add_argument("expected_root")
    validation.add_argument("platform", choices=sorted(PLATFORMS))
    collection = commands.add_parser("collect")
    collection.add_argument("platform", choices=sorted(PLATFORMS))
    collection.add_argument("descriptor", type=Path)
    collection.add_argument("destination", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "stage":
            stage(args.directory, args.inputs, args.platform)
        elif args.command == "validate":
            validate_archive(args.archive, args.expected_root, args.platform)
        else:
            collect(args.platform, args.descriptor, args.destination)
    except (OSError, ValueError, KeyError, tarfile.TarError, zipfile.BadZipFile) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
