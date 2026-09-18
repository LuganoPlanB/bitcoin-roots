#!/usr/bin/env python3
"""Verify the immutable Roots 29.4 production accounting boundary."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


SHA1 = re.compile(r"^sha1:[0-9a-f]{40}$")
SHA256 = re.compile(r"^sha256:[0-9a-f]{64}$")
PLATFORMS = ["linux", "macos", "windows"]
LEGACY_COMMIT = "sha1:42098b53c57fb6818736c7b6732fa84ffe6ad391"
LEGACY_TREE = "sha1:a5708dcbf1d2611360fab68fc6a8e504db1ba95d"


class AccountingError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise AccountingError(message)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def sha256_bytes(value):
    return "sha256:" + hashlib.sha256(value).hexdigest()


def load_json(path, name):
    require(path.is_file() and not path.is_symlink(), f"{name} is unavailable")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise AccountingError(f"{name} is invalid") from error


def bound_file(repository, value, name):
    require(isinstance(value, str) and value, f"{name} path is invalid")
    relative = Path(value)
    require(not relative.is_absolute() and ".." not in relative.parts, f"{name} path is unsafe")
    target = repository
    for component in relative.parts:
        target /= component
        require(not target.is_symlink(), f"{name} path is symlinked")
    require(target.is_file(), f"{name} is unavailable")
    return target


def git(repository, *args):
    result = subprocess.run(["git", "-C", str(repository), *args], capture_output=True)
    require(result.returncode == 0, "production Git input is invalid")
    return result.stdout


def tree(repository, commit):
    return "sha1:" + git(repository, "rev-parse", f"{commit}^{{tree}}").decode().strip()


def atoms(repository, base, head):
    names = git(repository, "diff", "--name-status", "-z", "--no-renames", base, head).split(b"\0")
    values = []
    for status, encoded_path in zip(names[0::2], names[1::2]):
        if not status or not encoded_path:
            continue
        path = encoded_path.decode("utf-8", "strict")
        patch = git(repository, "diff", "--no-ext-diff", "--binary", "--full-index", "--unified=0", base, head, "--", path)
        kind = {b"A": "add", b"D": "delete"}.get(status[:1], "modify")
        if b"GIT binary patch" in patch:
            kind = "binary"
        if b"old mode " in patch or b"new mode " in patch:
            mode = b"\n".join(line for line in patch.splitlines() if line.startswith((b"old mode ", b"new mode ")))
            values.append((path, "mode", sha256_bytes(mode)))
        blocks = [b"@@ " + block for block in patch.split(b"\n@@ ")[1:]]
        if not blocks:
            if kind == "modify" and b"old mode " in patch:
                continue
            blocks = [patch]
        values.extend((path, kind, sha256_bytes(block)) for block in blocks)
    return sorted(values)


def require_oid(value, name):
    require(isinstance(value, str) and SHA1.fullmatch(value), f"{name} is invalid")


def validate(record_path, repository, build_path, public_release=False):
    record = load_json(record_path, "production accounting")
    require(set(record) == {"schema_version", "releases"} and record["schema_version"] == 1, "production accounting schema is invalid")
    require(isinstance(record["releases"], list) and len(record["releases"]) == 2, "production releases are invalid")
    legacy, current = record["releases"]
    require(set(legacy) == {"id", "source_commit", "source_tree", "state"}, "historical release schema is invalid")
    require(legacy["id"] == "roots-29.3-roots.1" and legacy["state"] == "preserved", "historical release was rewritten")
    require(legacy["source_commit"] == LEGACY_COMMIT, "historical commit differs")
    require(legacy["source_tree"] == LEGACY_TREE, "historical tree differs")
    expected = {"id", "canonical", "frozen", "replay_tree", "topology", "sources", "production_commits", "authorization", "build_attestation"}
    require(isinstance(current, dict) and set(current) == expected and current["id"] == "roots-29.4-roots.1", "29.4 accounting schema is invalid")
    canonical_line = current["canonical"]
    frozen = current["frozen"]
    require(isinstance(canonical_line, dict) and set(canonical_line) == {"commit", "tree"}, "canonical baseline is invalid")
    require(isinstance(frozen, dict) and set(frozen) == {"commit", "tree", "parent"}, "frozen baseline is invalid")
    for name, value in (("canonical commit", canonical_line["commit"]), ("canonical tree", canonical_line["tree"]), ("frozen commit", frozen["commit"]), ("frozen tree", frozen["tree"]), ("frozen parent", frozen["parent"]), ("replay tree", current["replay_tree"])):
        require_oid(value, name)
    canonical_commit = canonical_line["commit"].removeprefix("sha1:")
    frozen_commit = frozen["commit"].removeprefix("sha1:")
    require(frozen["parent"] == canonical_line["commit"], "frozen source does not directly follow canonical target")
    require(tree(repository, canonical_commit) == canonical_line["tree"], "canonical source tree differs")
    require(tree(repository, frozen_commit) == frozen["tree"], "frozen source tree differs")
    require(git(repository, "rev-parse", f"{frozen_commit}^").decode().strip() == canonical_commit, "frozen source parent differs")
    require(current["replay_tree"] == canonical_line["tree"], "replay tree crosses the canonical anchor")
    topology = current["topology"]
    require(isinstance(topology, dict) and set(topology) == {"path", "sha256"} and isinstance(topology["path"], str) and SHA256.fullmatch(topology["sha256"] or ""), "topology binding is invalid")
    topology_path = bound_file(repository, topology["path"], "canonical topology")
    topology_value = load_json(topology_path, "canonical topology")
    require(sha256_bytes(topology_path.read_bytes()) == topology["sha256"], "canonical topology digest differs")
    require(topology_value.get("target_commit") == canonical_line["commit"] and topology_value.get("target_tree") == canonical_line["tree"], "canonical topology target differs")
    sources = current["sources"]
    require(isinstance(sources, dict) and set(sources) == {"manifest", "registry", "frozen_record", "promotion"}, "production source bindings are invalid")
    for name, binding in sources.items():
        require(isinstance(binding, dict) and set(binding) == {"path", "sha256"} and isinstance(binding["path"], str) and SHA256.fullmatch(binding["sha256"] or ""), f"{name} binding is invalid")
        path = bound_file(repository, binding["path"], name)
        require(sha256_bytes(path.read_bytes()) == binding["sha256"], f"{name} binding differs")
    promotion = load_json(bound_file(repository, sources["promotion"]["path"], "promotion"), "promotion authorization")
    require(isinstance(current["authorization"], bool) and promotion.get("authorization") is current["authorization"], "authorization differs from promotion record")
    commits = current["production_commits"]
    require(isinstance(commits, list) and len(commits) == 1, "production commits are incomplete")
    summary = commits[0]
    require(isinstance(summary, dict) and set(summary) == {"commit", "parent", "tree", "atom_count", "atom_digest"}, "production commit summary is invalid")
    observed = atoms(repository, canonical_commit, frozen_commit)
    require(summary["commit"] == frozen["commit"] and summary["parent"] == frozen["parent"] and summary["tree"] == frozen["tree"] and summary["atom_count"] == len(observed) and summary["atom_digest"] == sha256_bytes(canonical(observed)), "production atoms are not exactly accounted")
    policy = current["build_attestation"]
    require(isinstance(policy, dict) and set(policy) == {"required_platforms", "source_commit", "source_tree"} and policy["required_platforms"] == PLATFORMS and policy["source_commit"] == frozen["commit"] and policy["source_tree"] == frozen["tree"], "build attestation policy is invalid")
    build = load_json(build_path, "build evidence")
    require(build.get("status") == "pass" and build.get("release_source_commit") == frozen["commit"] and build.get("release_source_tree") == frozen["tree"] and build.get("matrix") == PLATFORMS, "build attestations do not bind production source")
    require(not public_release or current["authorization"], "public release lacks approval")
    return sha256_bytes(canonical(record))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--build-evidence", type=Path, required=True)
    parser.add_argument("--public-release", action="store_true")
    args = parser.parse_args()
    try:
        print(validate(args.record, args.repository, args.build_evidence, args.public_release))
    except (OSError, UnicodeDecodeError, AccountingError) as error:
        print(f"roots-production-accounting: {error}", file=sys.stderr)
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
