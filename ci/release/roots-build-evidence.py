#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Create and verify source-bound release build attestations."""

import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path


OUTPUT_NAME = "roots-release-build-evidence.json"
ATTESTATION_SUFFIX = ".build-attestation.json"
SHA1 = re.compile(r"sha1:[0-9a-f]{40}")
SHA256 = re.compile(r"sha256:[0-9a-f]{64}")
PACKAGE = re.compile(r"bitcoin-roots-(linux|darwin|windows)-[A-Za-z0-9_.-]+\.(?:tar\.gz|zip)")
PLATFORMS = {"linux": "linux", "darwin": "macos", "windows": "windows"}


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(value):
    return "sha256:" + hashlib.sha256(canonical(value)).hexdigest()


def git_source(repository, revision):
    require(repository.is_absolute() and repository.is_dir() and not repository.is_symlink(), "source repository is invalid")
    commit_result = subprocess.run(["git", "-C", repository, "rev-parse", "--verify", f"{revision}^{{commit}}"], capture_output=True, text=True)
    require(commit_result.returncode == 0, "source revision is invalid")
    commit = commit_result.stdout.strip()
    tree_result = subprocess.run(["git", "-C", repository, "rev-parse", f"{commit}^{{tree}}"], capture_output=True, text=True)
    require(tree_result.returncode == 0, "source tree is invalid")
    return "sha1:" + commit, "sha1:" + tree_result.stdout.strip()


def package_identity(path):
    match = PACKAGE.fullmatch(path.name)
    require(path.is_file() and not path.is_symlink() and match is not None, "build artifact is invalid")
    return PLATFORMS[match.group(1)]


def write_new(path, value):
    require(not path.exists() and not path.is_symlink(), "refusing to replace build evidence")
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as output:
        output.write(canonical(value) + b"\n")


def attest(artifact, repository, revision, output):
    platform = package_identity(artifact)
    require(output.name == artifact.name + ATTESTATION_SUFFIX, "attestation output name is invalid")
    source_commit, source_tree = git_source(repository, revision)
    value = {
        "artifact": {"name": artifact.name, "sha256": "sha256:" + hashlib.sha256(artifact.read_bytes()).hexdigest()},
        "kind": "bitcoin-roots-build-attestation",
        "platform": platform,
        "schema_version": 1,
        "source_commit": source_commit,
        "source_tree": source_tree,
    }
    value["digest"] = digest(value)
    write_new(output, value)


def read_attestation(path):
    require(path.is_file() and not path.is_symlink() and path.name.endswith(ATTESTATION_SUFFIX), "build attestation is invalid")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("build attestation is invalid") from error
    expected = {"artifact", "digest", "kind", "platform", "schema_version", "source_commit", "source_tree"}
    require(isinstance(value, dict) and set(value) == expected, "build attestation schema is invalid")
    reported = value["digest"]
    unsigned = dict(value)
    unsigned.pop("digest")
    require(
        value["schema_version"] == 1
        and value["kind"] == "bitcoin-roots-build-attestation"
        and value["platform"] in set(PLATFORMS.values())
        and SHA1.fullmatch(value["source_commit"] or "")
        and SHA1.fullmatch(value["source_tree"] or "")
        and isinstance(value["artifact"], dict)
        and set(value["artifact"]) == {"name", "sha256"}
        and PACKAGE.fullmatch(value["artifact"]["name"] or "")
        and SHA256.fullmatch(value["artifact"]["sha256"] or "")
        and SHA256.fullmatch(reported or "")
        and reported == digest(unsigned),
        "build attestation contents are invalid",
    )
    return value


def aggregate(directory, repository, revision, expected_count):
    require(directory.is_dir() and not directory.is_symlink(), "build evidence input is invalid")
    source_commit, source_tree = git_source(repository, revision)
    packages = {}
    attestations = {}
    for path in sorted(directory.rglob("*")):
        require(not path.is_symlink(), "build evidence input symlink is forbidden")
        if not path.is_file() or path.name == "SHA256SUMS" or path.name.endswith(".sha256"):
            continue
        if path.name.endswith(ATTESTATION_SUFFIX):
            value = read_attestation(path)
            name = value["artifact"]["name"]
            require(name not in attestations, "duplicate build attestation")
            attestations[name] = value
            continue
        platform = package_identity(path)
        require(path.name not in packages, "duplicate build artifact")
        packages[path.name] = (path, platform)
    require(len(packages) == expected_count and set(packages) == set(attestations), "build artifacts and attestations differ")
    artifacts = []
    platforms = set()
    for name in sorted(packages):
        path, platform = packages[name]
        attestation = attestations[name]
        package_hash = "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()
        require(
            attestation["source_commit"] == source_commit
            and attestation["source_tree"] == source_tree
            and attestation["platform"] == platform
            and attestation["artifact"] == {"name": name, "sha256": package_hash},
            "build attestation does not match source or package",
        )
        platforms.add(platform)
        artifacts.append({"name": name, "platform": platform, "sha256": package_hash, "attestation_digest": attestation["digest"]})
    require(platforms == {"linux", "macos", "windows"}, "build platform matrix is incomplete")
    value = {
        "artifacts": artifacts,
        "matrix": sorted(platforms),
        "release_source_commit": source_commit,
        "release_source_tree": source_tree,
        "schema_version": 2,
        "status": "pass",
    }
    value["digest"] = digest(value)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    attestation = subparsers.add_parser("attest")
    attestation.add_argument("--artifact", type=Path, required=True)
    attestation.add_argument("--source-repository", type=Path, required=True)
    attestation.add_argument("--source-revision", required=True)
    attestation.add_argument("--output", type=Path, required=True)
    for command in ("aggregate", "verify"):
        aggregate_parser = subparsers.add_parser(command)
        aggregate_parser.add_argument("--artifacts", type=Path, required=True)
        aggregate_parser.add_argument("--source-repository", type=Path, required=True)
        aggregate_parser.add_argument("--source-revision", required=True)
        aggregate_parser.add_argument("--expected-count", type=int, required=True)
        aggregate_parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "attest":
            attest(args.artifact, args.source_repository, args.source_revision, args.output)
            return
        require(args.output.name == OUTPUT_NAME, "unsafe build evidence output path")
        expected = aggregate(args.artifacts, args.source_repository, args.source_revision, args.expected_count)
        if args.command == "verify":
            require(args.output.is_file() and not args.output.is_symlink(), "build evidence is unavailable")
            require(json.loads(args.output.read_text(encoding="utf-8")) == expected, "build evidence does not match attestations")
        else:
            write_new(args.output, expected)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(f"roots-build-evidence: {error}") from error


if __name__ == "__main__":
    main()
