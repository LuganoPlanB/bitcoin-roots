#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Create deterministic release provenance from verified replay inputs."""

import argparse
import hashlib
import importlib.util
import json
import os
import re
import subprocess
from pathlib import Path


SHA256 = re.compile(r"sha256:[0-9a-f]{64}")
SHA1 = re.compile(r"sha1:[0-9a-f]{40}")
OUTPUT_NAME = "roots-release-evidence.json"
CANONICAL_INPUTS = (
    "contrib/roots/lineage-ledger.json",
    "contrib/roots/adaptation-manifest-29.3.json",
    "contrib/roots/replay-29.4-proposal/acceptance-evidence.json",
)
REPLAY_CANDIDATE_KEYS = {
    "base_commit", "base_tree", "tree", "tree_digest", "scoped_tree_digest",
}
REPLAY_APPROVAL_KEYS = {
    "source", "sha256", "reviewer", "proposal_digest", "candidate_tree",
    "approved_boundaries", "status",
}
REPLAY_REPRODUCIBILITY_KEYS = {
    "fresh_replay_runs", "fresh_replay_trees", "canonical_lineage_runs",
    "canonical_lineage_commits", "canonical_lineage_trees", "status",
}
REPLAY_PLATFORM_KEYS = {
    "source", "source_sha256", "github_run_conclusion", "classification",
    "supported_platforms", "invariant_count", "compatibility", "packaging",
    "generated_outputs", "status",
}
REPLAY_TOP_LEVEL_KEYS = {
    "approval", "candidate", "digest", "fresh_incremental_comparison", "gates",
    "identity_and_release_notes", "kind", "l4_accounting", "l6_evidence",
    "limitations", "lineage", "local_validation", "platform_and_behavior",
    "publication", "reproducibility", "schema_version", "scope", "status",
}


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def sha256_file(path):
    return "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path, description):
    if not path.is_file() or path.is_symlink():
        raise ValueError(f"{description} must be a regular file")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"{description} is not valid JSON") from error


def source_input(root, name):
    current = root
    for component in Path(name).parts:
        current /= component
        require(not current.is_symlink(), f"canonical source input is symlinked: {name}")
    path = root / name
    require(path.is_file() and not path.is_symlink(), f"canonical source input is invalid: {name}")
    return path


def source_revision(root, revision):
    result = subprocess.run(
        ["git", "-C", root, "rev-parse", "--verify", f"{revision}^{{commit}}"],
        capture_output=True,
        text=True,
    )
    require(result.returncode == 0, "source revision is invalid")
    return result.stdout.strip()


def validate_revision_inputs(root, revision):
    commit = source_revision(root, revision)
    for name in CANONICAL_INPUTS:
        path = source_input(root, name)
        result = subprocess.run(
            ["git", "-C", root, "show", f"{commit}:{name}"],
            capture_output=True,
        )
        require(
            result.returncode == 0,
            f"source revision does not contain canonical input: {name}",
        )
        require(
            path.read_bytes() == result.stdout,
            f"canonical input differs from source revision: {name}",
        )
    tree = subprocess.run(
        ["git", "-C", root, "rev-parse", f"{commit}^{{tree}}"],
        capture_output=True,
        text=True,
        check=False,
    )
    require(tree.returncode == 0, "source revision tree is invalid")
    return "sha1:" + tree.stdout.strip()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def trusted_validator(name):
    path = Path(__file__).resolve().parents[2] / "contrib/devtools" / name
    specification = importlib.util.spec_from_file_location(name.replace("-", "_"), path)
    require(specification and specification.loader, f"trusted validator is unavailable: {name}")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def is_nonempty_string(value):
    return isinstance(value, str) and bool(value)


def matches(pattern, value):
    return isinstance(value, str) and pattern.fullmatch(value) is not None


def is_relative_path(value):
    return (
        is_nonempty_string(value)
        and not value.startswith("/")
        and ".." not in Path(value).parts
    )


def is_nonnegative_integer(value):
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def validate_inputs(ledger, manifest, replay):
    try:
        trusted_validator("roots-lineage.py").validate(ledger)
        trusted_validator("roots-adaptation-manifest.py").validate_manifest(manifest)
    except (AttributeError, KeyError, TypeError, ValueError) as error:
        raise ValueError(f"trusted provenance validation failed: {error}") from error
    require(
        isinstance(ledger, dict)
        and set(ledger) == {"claims", "exceptions", "fork_starts", "object_format", "releases", "schema_version"},
        "ledger schema is invalid",
    )
    require(ledger["schema_version"] == 1 and ledger["object_format"] == "sha1", "ledger version is invalid")
    require(
        isinstance(manifest, dict) and set(manifest) == {"atlas_report_digest", "schema_version", "units"},
        "manifest schema is invalid",
    )
    require(
        manifest["schema_version"] == 1
        and matches(SHA256, manifest["atlas_report_digest"])
        and isinstance(manifest["units"], list)
        and manifest["units"],
        "manifest contents are invalid",
    )
    require(
        isinstance(replay, dict)
        and set(replay) == REPLAY_TOP_LEVEL_KEYS
        and replay.get("schema_version") == 1
        and replay.get("kind") == "roots-29.4-technical-acceptance"
        and replay.get("scope") == "technical alignment acceptance only"
        and replay.get("status") == "accepted",
        "replay result is not accepted",
    )
    candidate = replay.get("candidate")
    require(
        isinstance(candidate, dict)
        and set(candidate) == REPLAY_CANDIDATE_KEYS
        and all(matches(SHA1, candidate.get(key)) for key in ("base_commit", "base_tree", "tree"))
        and all(matches(SHA256, candidate.get(key)) for key in ("tree_digest", "scoped_tree_digest")),
        "replay candidate is invalid",
    )
    approval = replay.get("approval")
    require(
        isinstance(approval, dict)
        and set(approval) == REPLAY_APPROVAL_KEYS
        and approval.get("status") == "approved"
        and approval.get("candidate_tree") == candidate["tree"]
        and matches(SHA256, approval.get("proposal_digest"))
        and matches(SHA256, approval.get("sha256"))
        and is_relative_path(approval.get("source"))
        and is_nonempty_string(approval.get("reviewer"))
        and is_nonnegative_integer(approval.get("approved_boundaries"))
        and approval["approved_boundaries"] > 0,
        "replay approval is invalid",
    )
    gates = replay.get("gates")
    require(
        isinstance(gates, list)
        and gates
        and all(
            isinstance(gate, dict)
            and set(gate) == {"id", "critical", "status", "waiver"}
            and is_nonempty_string(gate["id"])
            and isinstance(gate["critical"], bool)
            and isinstance(gate["waiver"], bool)
            and gate["status"] == "pass"
            for gate in gates
        )
        and len({gate["id"] for gate in gates}) == len(gates),
        "replay gates are invalid",
    )
    require(
        all(not gate["critical"] or not gate["waiver"] for gate in gates),
        "replay critical gate is waived",
    )
    reproducibility = replay["reproducibility"]
    require(
        isinstance(reproducibility, dict)
        and set(reproducibility) == REPLAY_REPRODUCIBILITY_KEYS
        and reproducibility.get("status") == "pass"
        and all(
            is_nonnegative_integer(reproducibility[key]) and reproducibility[key] >= 2
            for key in ("fresh_replay_runs", "canonical_lineage_runs")
        )
        and isinstance(reproducibility.get("fresh_replay_trees"), list)
        and isinstance(reproducibility.get("canonical_lineage_trees"), list)
        and isinstance(reproducibility.get("canonical_lineage_commits"), list),
        "replay reproducibility is invalid",
    )
    require(
        reproducibility["fresh_replay_runs"] == len(reproducibility["fresh_replay_trees"])
        and reproducibility["canonical_lineage_runs"]
        == len(reproducibility["canonical_lineage_trees"])
        == len(reproducibility["canonical_lineage_commits"])
        and all(
            isinstance(tree, str) and tree == candidate["tree"]
            for tree in reproducibility["fresh_replay_trees"]
            + reproducibility["canonical_lineage_trees"]
        )
        and all(
            matches(SHA1, commit)
            for commit in reproducibility["canonical_lineage_commits"]
        ),
        "replay reproducibility does not match candidate",
    )
    platform = replay.get("platform_and_behavior")
    require(
        isinstance(platform, dict)
        and set(platform) == REPLAY_PLATFORM_KEYS
        and platform.get("status") == "pass"
        and platform.get("compatibility") == "pass"
        and is_relative_path(platform.get("source"))
        and matches(SHA256, platform.get("source_sha256"))
        and all(is_nonempty_string(platform.get(key)) for key in ("github_run_conclusion", "classification", "packaging"))
        and all(is_nonnegative_integer(platform.get(key)) for key in ("generated_outputs", "invariant_count"))
        and isinstance(platform.get("supported_platforms"), list)
        and platform["supported_platforms"]
        and all(is_nonempty_string(item) for item in platform["supported_platforms"])
        and platform["supported_platforms"] == sorted(set(platform["supported_platforms"])),
        "replay platform evidence is invalid",
    )
    require(matches(SHA256, replay.get("digest")), "replay result digest is invalid")
    replay_digest = replay["digest"]
    replay_unsigned = dict(replay)
    replay_unsigned.pop("digest")
    require(replay_digest == "sha256:" + hashlib.sha256(canonical_json(replay_unsigned)).hexdigest(), "replay result digest mismatch")


def build(
    ledger_path,
    manifest_path,
    replay_path,
    candidate_tree=None,
    source_repository=None,
    source_revision_name=None,
):
    if source_repository is not None:
        require(source_revision_name is not None, "source revision is required")
        revision_tree = validate_revision_inputs(source_repository, source_revision_name)
        if candidate_tree is None:
            candidate_tree = revision_tree
        require(
            candidate_tree == revision_tree,
            "release source tree does not match source revision",
        )
        ledger_path, manifest_path, replay_path = (source_input(source_repository, name) for name in CANONICAL_INPUTS)
    ledger = read_json(ledger_path, "ledger")
    manifest = read_json(manifest_path, "manifest")
    replay = read_json(replay_path, "replay result")
    validate_inputs(ledger, manifest, replay)
    candidate = replay["candidate"]
    if candidate_tree is not None:
        require(matches(SHA1, candidate_tree), "release source tree is invalid")
    outcome_counts = {}
    for gate in replay["gates"]:
        outcome_counts[gate["status"]] = outcome_counts.get(gate["status"], 0) + 1
    value = {
        "artifact_links": [
            {"path": "contrib/roots/lineage-ledger.json", "sha256": sha256_file(ledger_path)},
            {"path": "contrib/roots/adaptation-manifest-29.3.json", "sha256": sha256_file(manifest_path)},
            {"path": "contrib/roots/replay-29.4-proposal/acceptance-evidence.json", "sha256": sha256_file(replay_path)},
        ],
        "base_tree": candidate["base_tree"],
        "build_matrix": replay.get("platform_and_behavior", {}).get("supported_platforms", []),
        "replay_candidate_tree": candidate["tree"],
        "release_source_tree": candidate_tree or candidate["tree"],
        "exceptions": ledger["exceptions"],
        "invariants": [gate["id"] for gate in replay["gates"] if gate.get("critical")],
        "ledger_digest": sha256_file(ledger_path),
        "manifest_digest": sha256_file(manifest_path),
        "manual_approvals": [replay.get("approval", {}).get("proposal_digest")],
        "outcome_counts": outcome_counts,
        "replay_result_digest": sha256_file(replay_path),
        "replay_tool_version": "1",
        "reproducibility": replay["reproducibility"],
        "schema_version": 1,
    }
    require(all(isinstance(item, str) and item for item in value["manual_approvals"]), "replay approval is incomplete")
    require(isinstance(value["build_matrix"], list) and value["build_matrix"], "build matrix is incomplete")
    value["digest"] = "sha256:" + hashlib.sha256(canonical_json(value)).hexdigest()
    return value


def write_output(path, value):
    require(
        path.name == OUTPUT_NAME and not path.is_absolute() and path.parent == Path("."),
        "unsafe evidence output path",
    )
    require(not path.exists() and not path.is_symlink(), "refusing to replace existing evidence output")
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as output:
        output.write(canonical_json(value) + b"\n")


def validate_evidence(
    path,
    ledger_path=None,
    manifest_path=None,
    replay_path=None,
    candidate_tree=None,
    source_repository=None,
    source_revision_name=None,
):
    value = read_json(path, "release evidence")
    require(isinstance(value, dict), "release evidence schema is invalid")
    expected = {
        "artifact_links", "base_tree", "build_matrix", "digest", "exceptions", "invariants",
        "ledger_digest", "manifest_digest", "manual_approvals", "outcome_counts",
        "replay_candidate_tree", "release_source_tree", "replay_result_digest", "replay_tool_version", "reproducibility", "schema_version",
    }
    require(set(value) == expected and value["schema_version"] == 1 and value["replay_tool_version"] == "1", "release evidence schema is invalid")
    require(
        matches(SHA1, value["base_tree"])
        and matches(SHA1, value["replay_candidate_tree"])
        and matches(SHA1, value["release_source_tree"]),
        "release evidence trees are invalid",
    )
    require(isinstance(value["build_matrix"], list) and value["build_matrix"] and all(isinstance(item, str) and item for item in value["build_matrix"]), "release evidence build matrix is invalid")
    require(
        isinstance(value["artifact_links"], list)
        and len(value["artifact_links"]) == 3
        and all(
            isinstance(item, dict)
            and set(item) == {"path", "sha256"}
            and is_relative_path(item["path"])
            and matches(SHA256, item["sha256"])
            for item in value["artifact_links"]
        ),
        "release evidence artifact links are invalid",
    )
    require([item["path"] for item in value["artifact_links"]] == list(CANONICAL_INPUTS), "release evidence artifact link order is invalid")
    require(isinstance(value["manual_approvals"], list) and value["manual_approvals"] and all(isinstance(item, str) and item for item in value["manual_approvals"]), "release evidence approvals are invalid")
    require(isinstance(value["exceptions"], list) and all(isinstance(item, dict) for item in value["exceptions"]), "release evidence exceptions are invalid")
    require(isinstance(value["outcome_counts"], dict) and value["outcome_counts"] and all(isinstance(item, int) and not isinstance(item, bool) and item >= 0 for item in value["outcome_counts"].values()), "release evidence outcomes are invalid")
    require(isinstance(value["reproducibility"], dict) and isinstance(value["invariants"], list) and value["invariants"] and all(isinstance(item, str) and item for item in value["invariants"]), "release evidence nested structure is invalid")
    require(
        all(
            matches(SHA256, value[item])
            for item in ("ledger_digest", "manifest_digest", "replay_result_digest", "digest")
        ),
        "release evidence digests are invalid",
    )
    digest = value["digest"]
    unsigned = dict(value)
    unsigned.pop("digest")
    require(digest == "sha256:" + hashlib.sha256(canonical_json(unsigned)).hexdigest(), "release evidence digest mismatch")
    if ledger_path and manifest_path and replay_path:
        require(
            value
            == build(
                ledger_path,
                manifest_path,
                replay_path,
                candidate_tree,
                source_repository,
                source_revision_name,
            ),
            "release evidence does not match authoritative inputs",
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ledger", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--replay-result", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--candidate-tree")
    parser.add_argument("--source-repository", type=Path)
    parser.add_argument("--source-revision")
    parser.add_argument("--verify", action="store_true")
    arguments = parser.parse_args()
    try:
        require(
            arguments.source_repository is not None
            and arguments.source_repository.is_dir()
            and not arguments.source_repository.is_symlink(),
            "source repository is invalid",
        )
        require(arguments.source_revision is not None, "source revision is required")
        if arguments.verify:
            require(arguments.ledger and arguments.manifest and arguments.replay_result, "authoritative evidence inputs are required")
            validate_evidence(
                arguments.output,
                arguments.ledger,
                arguments.manifest,
                arguments.replay_result,
                arguments.candidate_tree,
                arguments.source_repository,
                arguments.source_revision,
            )
        else:
            require(arguments.ledger and arguments.manifest and arguments.replay_result, "evidence inputs are required")
            write_output(
                arguments.output,
                build(
                    arguments.ledger,
                    arguments.manifest,
                    arguments.replay_result,
                    arguments.candidate_tree,
                    arguments.source_repository,
                    arguments.source_revision,
                ),
            )
    except (OSError, ValueError) as error:
        raise SystemExit(f"roots-release-evidence: {error}") from error


if __name__ == "__main__":
    main()
