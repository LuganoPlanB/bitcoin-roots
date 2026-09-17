#!/usr/bin/env python3
"""Fail-closed, read-only validation of a Roots production promotion contract."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Any


SHA1 = re.compile(r"^sha1:[0-9a-f]{40}$")
MAX_BYTES = 65_536
VENDOR_PREFIXES = ("upstream/", "vendor/", "src/secp256k1/", "src/leveldb/", "src/crc32c/", "src/minisketch/")


class ContractError(ValueError):
    pass


def load(path: Path) -> dict[str, Any]:
    try:
        if not path.is_file() or path.is_symlink() or path.stat().st_size > MAX_BYTES:
            raise ContractError("contract is unavailable or exceeds its size limit")
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=no_duplicates)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ContractError("contract is invalid") from error
    if not isinstance(value, dict):
        raise ContractError("contract must be an object")
    return value


def no_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise ContractError("contract has a duplicate key")
        value[key] = item
    return value


def oid(value: Any, field: str) -> str:
    if not isinstance(value, str) or not SHA1.fullmatch(value):
        raise ContractError(f"{field} must be a full sha1 object ID")
    return value.removeprefix("sha1:")


def contract_shape(value: dict[str, Any], require_unauthorized: bool = True) -> None:
    required = {"schema_version", "core", "candidate", "control_plane", "production", "authorization"}
    if set(value) != required or value["schema_version"] != 1 or not isinstance(value["authorization"], bool):
        raise ContractError("contract shape or authorization is invalid")
    if require_unauthorized and value["authorization"] is not False:
        raise ContractError("promotion is not authorized for topology validation")
    core, candidate, control, production = (value["core"], value["candidate"], value["control_plane"], value["production"])
    if not all(isinstance(item, dict) for item in (core, candidate, control, production)):
        raise ContractError("contract sections must be objects")
    if set(core) != {"tag", "tag_object", "commit", "tree"} or core["tag"] != "v29.4":
        raise ContractError("Core tag binding is invalid")
    if set(candidate) != {"tree", "canonical_commit", "input_commit", "forbidden_old_trunk_commit"} or candidate["input_commit"] != candidate["canonical_commit"]:
        raise ContractError("candidate binding is invalid")
    if set(control) != {"branch", "base_commit", "candidate_transfer"} or control["branch"] != "refs/heads/codex/roots-29-4-release" or control["candidate_transfer"] != "exact-bundle-or-commit":
        raise ContractError("control-plane binding is invalid")
    if set(production) != {"integration_ref", "production_ref", "release_tag", "head", "fast_forward_only"} or production["integration_ref"] != "refs/heads/integration/roots-29.4" or production["production_ref"] != "refs/heads/roots/29.4" or production["release_tag"] != "refs/tags/v29.4-roots.1" or production["fast_forward_only"] is not True:
        raise ContractError("production binding is invalid")
    for section, fields in ((core, ("tag_object", "commit", "tree")), (candidate, ("tree", "canonical_commit", "input_commit", "forbidden_old_trunk_commit")), (control, ("base_commit",)), (production, ("head",))):
        for field in fields:
            oid(section[field], field)


def git(repository: Path, *args: str) -> str:
    environment = git_environment()
    result = subprocess.run(["git", "-C", str(repository), "-c", "core.hooksPath=/dev/null", "-c", "rerere.enabled=false", "-c", "core.useReplaceRefs=false", *args], capture_output=True, text=True, encoding="utf-8", errors="strict", env=environment, check=False)
    if result.returncode:
        raise ContractError("Git object or ref resolution failed")
    return result.stdout.strip()


def git_environment() -> dict[str, str]:
    return {"PATH": os.environ.get("PATH", ""), "LC_ALL": "C", "LANG": "C", "TZ": "UTC", "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": os.devnull, "GIT_NO_REPLACE_OBJECTS": "1", "GIT_OPTIONAL_LOCKS": "0"}


def is_ancestor(repository: Path, ancestor: str, descendant: str) -> bool:
    result = subprocess.run(["git", "-C", str(repository), "-c", "core.hooksPath=/dev/null", "-c", "rerere.enabled=false", "-c", "core.useReplaceRefs=false", "merge-base", "--is-ancestor", ancestor, descendant], capture_output=True, env=git_environment(), check=False)
    return result.returncode == 0


def resolve(repository: Path, ref: str, expected: str, kind: str = "commit") -> None:
    value = git(repository, "rev-parse", "--verify", f"{ref}^{{{kind}}}")
    if value != expected:
        raise ContractError(f"{ref} differs from its immutable binding")


def validate(path: Path, repository: Path, require_unauthorized: bool = True) -> dict[str, Any]:
    value = load(path)
    contract_shape(value, require_unauthorized)
    if not repository.is_dir() or repository.is_symlink():
        raise ContractError("repository is unsafe")
    if any(name in os.environ for name in ("GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_REPLACE_REF_BASE", "GIT_DIR", "GIT_WORK_TREE")):
        raise ContractError("host Git object override is forbidden")
    if git(repository, "rev-parse", "--is-shallow-repository") != "false":
        raise ContractError("shallow history is forbidden")
    git_dir = Path(git(repository, "rev-parse", "--git-dir"))
    if not git_dir.is_absolute():
        git_dir = repository / git_dir
    if (git_dir / "objects/info/alternates").exists() or git(repository, "for-each-ref", "--format=%(refname)", "refs/replace"):
        raise ContractError("alternates and replacements are forbidden")
    core, candidate, production = value["core"], value["candidate"], value["production"]
    tag_object, core_commit, core_tree = (oid(core["tag_object"], "core.tag_object"), oid(core["commit"], "core.commit"), oid(core["tree"], "core.tree"))
    canonical, candidate_tree, old_trunk, head = (oid(candidate["canonical_commit"], "candidate.canonical_commit"), oid(candidate["tree"], "candidate.tree"), oid(candidate["forbidden_old_trunk_commit"], "candidate.forbidden_old_trunk_commit"), oid(production["head"], "production.head"))
    resolve(repository, "refs/tags/v29.4", tag_object, "tag")
    resolve(repository, "refs/tags/v29.4", core_commit)
    resolve(repository, core_commit, core_tree, "tree")
    resolve(repository, canonical, candidate_tree, "tree")
    if head != canonical:
        raise ContractError("production head must be the canonical commit")
    for ref in (production["integration_ref"], production["production_ref"]):
        resolve(repository, ref, canonical)
    if not is_ancestor(repository, core_commit, canonical):
        raise ContractError("canonical history does not descend from Core v29.4")
    if is_ancestor(repository, old_trunk, canonical):
        raise ContractError("old trunk ancestry is forbidden")
    commits = git(repository, "rev-list", "--parents", f"{core_commit}..{canonical}").splitlines()
    if not commits or any(len(line.split()) != 2 for line in commits):
        raise ContractError("merge commits or disconnected canonical history are forbidden")
    changes = git(repository, "diff-tree", "--no-commit-id", "--name-only", "-r", core_commit, canonical).splitlines()
    if any(name.startswith(VENDOR_PREFIXES) for name in changes):
        raise ContractError("submodule or vendor import is forbidden")
    if "160000" in git(repository, "diff-tree", "--raw", "-r", core_commit, canonical):
        raise ContractError("submodule import is forbidden")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("contract", type=Path)
    parser.add_argument("--repository", type=Path, required=True)
    args = parser.parse_args()
    try:
        validate(args.contract, args.repository)
    except ContractError as error:
        print(f"roots-promotion-contract: {error}", file=sys.stderr)
        return 1
    print("promotion contract: valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
