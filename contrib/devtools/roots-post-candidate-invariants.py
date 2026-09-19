#!/usr/bin/env python3
"""Verify post-candidate replay containment with immutable Git identities."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

CORE = "3fc0865963a38b871e9f7d94e6151c4953563516"
CANDIDATE = "cbc88cff9b35b95a549c0313e424e13093fcd6a1"
ZONES = {
    "policy_consensus": ("src/policy/", "src/txmempool", "src/node/miner"),
    "chain_parameters": ("src/chainparams", "src/kernel/chainparams"),
    "consensus_validation": ("src/consensus/", "src/validation", "src/kernel/"),
    "serialization_script": ("src/serialize", "src/script/", "src/primitives/"),
    "wallet": ("src/wallet/", "src/walletdb"),
    "networking": ("src/net", "src/protocol", "src/txrequest"),
    "crypto": ("src/crypto/", "src/secp256k1/"),
    "build": ("CMakeLists.txt", "cmake/", "src/CMakeLists.txt"),
}
SHA256 = re.compile(r"^sha256:[0-9a-f]{64}$")

class InvariantError(ValueError):
    pass

def canonical(value):
    return json.dumps(value, indent=2, sort_keys=True).encode("utf-8") + b"\n"

def digest(value):
    return "sha256:" + hashlib.sha256(value).hexdigest()

def git(repository, *arguments):
    result = subprocess.run(["git", "-C", str(repository), *arguments], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode:
        raise InvariantError("required Git object is unavailable")
    return result.stdout

def classify_path(path, owned):
    if path not in owned:
        return "unowned_path"
    for zone, prefixes in ZONES.items():
        if path.startswith(prefixes):
            return zone
    if path.startswith(("src/", "depends/")):
        return "unowned_production_source"
    return None

def build(repository, manifest):
    value = json.loads(manifest.read_text(encoding="utf-8"))
    handler = value.get("handler", {})
    overlay = handler.get("overlay", {})
    result = str(handler.get("result_tree", ""))[5:]
    if (
        value.get("candidate", {}).get("commit") != "sha1:" + CANDIDATE
        or handler.get("kind") != "locked-git-diff-path-set-with-production-overlay"
        or not re.fullmatch(r"[0-9a-f]{40}", result)
        or not SHA256.fullmatch(str(handler.get("patch_sha256", "")))
        or not isinstance(overlay, dict)
        or overlay.get("result_tree") != "sha1:" + result
        or not SHA256.fullmatch(str(overlay.get("patch_sha256", "")))
    ):
        raise InvariantError("manifest identities or digest are not locked")
    if (
        git(repository, "rev-parse", "--verify", CORE + "^{commit}").decode().strip() != CORE
        or git(repository, "rev-parse", "--verify", CANDIDATE + "^{commit}").decode().strip() != CANDIDATE
        or git(repository, "rev-parse", CANDIDATE + "^{tree}").decode().strip() != "39a5e30207a09962e78ae81c24cc65b1e478ef90"
    ):
        raise InvariantError("locked Core or candidate identity differs")
    paths = git(repository, "diff-tree", "--no-commit-id", "--name-only", "-r", CANDIDATE, result).decode().splitlines()
    owned = handler["paths"]
    if paths != sorted(paths) or any(path not in set(owned) for path in paths):
        raise InvariantError("result paths are unowned or unordered")
    reasons = {path: classify_path(path, set(owned)) for path in paths}
    zones = {zone: [path for path, reason in reasons.items() if reason == zone] for zone in ZONES}
    if any(reasons.values()):
        raise InvariantError("post-candidate replay changes a runtime or production zone")
    scoped = git(repository, "diff", "--binary", "--full-index", CANDIDATE, result, "--", *paths)
    intermediate = str(overlay.get("base_tree", ""))[5:]
    source_scoped = git(repository, "diff", "--binary", "--full-index", CANDIDATE, intermediate, "--", *paths)
    if handler["patch_sha256"] != digest(source_scoped):
        raise InvariantError("manifest source patch digest does not bind scoped diff")
    return {
        "candidate_commit": "sha1:" + CANDIDATE,
        "core_commit": "sha1:" + CORE,
        "decision": "accepted",
        "invariants": {
            "core_valid_block_acceptance": "inherited-no-runtime-delta",
            "no_rdts_bip110_enforcement": "inherited-no-runtime-delta",
            "policy_rejection_block_acceptance": "inherited-no-runtime-delta",
        },
        "path_count": len(paths),
        "result_tree": "sha1:" + result,
        "runtime_zone_paths": zones,
        "scoped_diff_sha256": digest(scoped),
    }

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("build", "verify"))
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        expected = build(arguments.repository, arguments.manifest)
        if arguments.command == "build":
            if arguments.output.exists():
                raise InvariantError("refusing overwrite")
            arguments.output.write_bytes(canonical(expected))
        elif arguments.output.read_bytes() != canonical(expected):
            raise InvariantError("invariant evidence does not match locked trees")
        print(json.dumps({"decision": "accepted", "paths": expected["path_count"], "result_tree": expected["result_tree"]}, sort_keys=True))
        return 0
    except (InvariantError, OSError, json.JSONDecodeError) as error:
        print("roots-post-candidate-invariants: " + str(error), file=sys.stderr)
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
