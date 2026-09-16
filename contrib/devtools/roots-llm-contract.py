#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Generate, validate, and rehearse the versioned Roots LLM contract."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Any


CONTRACT_NAME = "llm-execution-contract-v1.json"
OID_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^sha256:[0-9a-f]{64}$")
CORE_RELEASES = [
    {
        "id": "core-29.3",
        "tag": "v29.3",
        "tag_object": "f2ca1bc7f94f1f662e2d7ee0858f2d90018bb863",
        "commit": "99003bed87333f1be51bf3070235591b3a72f007",
        "tree": "d7910bd5e9335128932f1f848a767d773895c4a4",
        "signature": "must-verify-online-before-release-use",
    },
    {
        "id": "core-29.4",
        "tag": "v29.4",
        "tag_object": "4e70eab99b60f7718b78e2158de9fb82726f3cec",
        "commit": "3fc0865963a38b871e9f7d94e6151c4953563516",
        "tree": "38ad59b187f59647eb90ad1347bc481485ef4d01",
        "signature": "must-verify-online-before-release-use",
    },
]
EXPECTED_RESULTS = {
    "core_to_knots_29_3_tree": "56f97d3a9199c1fb191e1b8a21f2caae3901b7f6",
    "roots_29_3_tree": "a5708dcbf1d2611360fab68fc6a8e504db1ba95d",
    "canonical_29_4_commit": "cbc88cff9b35b95a549c0313e424e13093fcd6a1",
    "canonical_29_4_tree": "39a5e30207a09962e78ae81c24cc65b1e478ef90",
    "incremental_29_4_commit": "3e29908f7a0131a71309e80a78fe865ec8a50f76",
    "incremental_29_4_tree": "c676e8944470cc74fcc213e7368aed359ad8ae55",
    "incremental_conflict_count": 12,
    "incremental_decision": "rejected",
}
MANUAL_APPROVAL = {
    "question": "Choose exactly one reviewed disposition for this adaptation: accept, rewrite, reject, defer, or upstream.",
    "allowed_answers": ["accept", "rewrite", "reject", "defer", "upstream"],
    "required_evidence": ["reviewer", "rationale", "tests", "candidate_tree"],
    "default": "safe-stop",
    "forbidden": ["guess", "auto-approve", "treat silence as consent"],
}


class ContractError(Exception):
    """A stable, fail-closed contract diagnostic."""


def canonical(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def digest(value: Any) -> str:
    return "sha256:" + hashlib.sha256(canonical(value)).hexdigest()


def file_digest(path: Path) -> str:
    return "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()


def load_json(path: Path) -> Any:
    def object_without_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        value = {}
        for key, item in pairs:
            if key in value:
                raise ValueError("duplicate JSON key")
            value[key] = item
        return value

    def reject_constant(value: str) -> None:
        raise ValueError(f"non-finite JSON number: {value}")

    try:
        return json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=object_without_duplicates,
            parse_constant=reject_constant,
        )
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        raise ContractError(f"E_JSON: cannot read {path.name}") from error


def project_root(contract_path: Path) -> Path:
    if contract_path.name != CONTRACT_NAME or contract_path.parent.name != "roots":
        raise ContractError("E_PATH: contract must retain its versioned repository path")
    root = contract_path.resolve().parents[2]
    if not (root / "contrib/devtools/roots-replay.py").is_file():
        raise ContractError("E_PATH: repository root cannot be resolved from contract")
    return root


def source_record(root: Path, relative: str) -> dict[str, str]:
    path = root / relative
    return {"path": relative, "sha256": file_digest(path)}


def material_boundaries(root: Path, relative: str) -> list[dict[str, Any]]:
    value = load_json(root / relative)
    boundaries = []
    for material_id, material in value["materials"].items():
        boundary = {"id": material_id, "mechanism": material["mechanism"]}
        for field in ("expected_before_tree", "expected_after_tree", "expected_patch_sha256", "expected_sha256"):
            if field in material:
                boundary[field] = material[field]
        boundaries.append(boundary)
    return boundaries


def build_contract(root: Path) -> dict[str, Any]:
    manifest_path = root / "contrib/roots/adaptation-manifest-29.3.json"
    topology_path = root / "contrib/roots/replay-29.4-proposal/canonical-topology.json"
    manifest = load_json(manifest_path)
    topology = load_json(topology_path)
    units = manifest["units"]
    commit_map = topology["commit_map"]
    if len(units) != 16 or len(commit_map) != 16:
        raise ContractError("E_ORDER: accepted adaptation inventory must contain sixteen units")

    adaptations = []
    packets = []
    for order, (unit, mapping) in enumerate(zip(units, commit_map, strict=True), start=1):
        unit_id = unit["id"]
        if mapping["adaptation_ids"] != [unit_id]:
            raise ContractError("E_ORDER: manifest and canonical commit map disagree")
        paths = unit["touched"]["paths"]
        path_digest = digest(paths)
        material_root = "contrib/roots/replay-core-to-knots-29.3" if unit_id.startswith("roots-knots-") else "contrib/roots/replay-29.3-release"
        context = [
            "contrib/roots/adaptation-manifest-29.3.json",
            f"{material_root}/adaptation-manifest-29.3.json",
            f"{material_root}/replay-materials.json",
            "contrib/roots/replay-29.4-proposal/canonical-topology.json",
            *unit["required_tests"],
        ]
        if unit_id == "roots-roots-release-generated-output":
            context.append("contrib/roots/core-29.4-migration-fixture.json")
        adaptations.append({
            "order": order,
            "id": unit_id,
            "dependencies": unit["dependencies"],
            "commit_group": mapping["commit"].removeprefix("sha1:"),
            "scope_pointer": f"/units/{order - 1}/touched/paths",
            "path_count": len(paths),
            "path_digest": path_digest,
        })
        packets.append({
            "id": unit_id,
            "purpose": unit["purpose"],
            "owner_area": unit["owner_area"],
            "risk_tier": unit["risk_tier"],
            "consensus_policy_effect": unit["consensus_policy_effect"],
            "provenance": unit["provenance"],
            "scope": {"manifest_pointer": f"/units/{order - 1}/touched/paths", "path_count": len(paths), "path_digest": path_digest},
            "context_paths": context,
            "required_tests": unit["required_tests"],
            "generated": unit["generated"],
            "manual_approval": MANUAL_APPROVAL,
        })

    immutable_paths = [
        "contrib/devtools/roots-llm-contract.py",
        "contrib/roots/lineage-ledger.json",
        "contrib/roots/adaptation-manifest-29.3.json",
        "contrib/roots/methodology-v1.json",
        "contrib/roots/core-29.4-migration-fixture.json",
        "contrib/roots/replay-core-to-knots-29.3/replay-materials.json",
        "contrib/roots/replay-29.3-release/replay-materials.json",
        "contrib/roots/replay-29.4-proposal/replay-materials.json",
        "contrib/roots/replay-29.4-proposal/canonical-topology.json",
        "contrib/roots/replay-29.4-proposal/acceptance-evidence.json",
        "contrib/roots/llm-execution-contract-v1.md",
    ]
    contract: dict[str, Any] = {
        "schema_version": 1,
        "contract_id": "bitcoin-roots-clean-clone-llm-v1",
        "contract_version": "1.0.0",
        "purpose": "Reconstruct reviewed Bitcoin Roots candidates from immutable Bitcoin Core inputs without conversational context or release authority.",
        "inputs": {
            "object_format": "sha1",
            "core_releases": CORE_RELEASES,
            "knots_29_3_stage": {
                "lineage_commit": "99ee26e9df0e63a5d1e0ab6bd46b1862ce67648b",
                "tree": EXPECTED_RESULTS["core_to_knots_29_3_tree"],
                "role": "provenance-stage-only-not-future-base",
            },
            "versioned_records": [source_record(root, path) for path in immutable_paths],
        },
        "allowed_remotes": [
            {"name": "core", "url": "https://github.com/bitcoin/bitcoin.git", "authority": "fetch-only"},
            {"name": "knots", "url": "https://github.com/bitcoinknots/bitcoin.git", "authority": "fetch-provenance-only"},
            {"name": "roots", "url": "https://github.com/LuganoPlanB/bitcoin-roots.git", "authority": "fetch-only"},
            {"name": "local-locked-mirror", "url": "${ROOTS_SOURCE}", "authority": "offline-rehearsal-only"},
        ],
        "topology": {
            "base": "verified-core-tag",
            "integration_branch_pattern": "roots/integration-v<CORE_RELEASE>",
            "first_parent_required": True,
            "forbidden_bases": ["bitcoin-knots-branch", "previous-roots-trunk"],
            "canonical_29_4": {
                "base_commit": CORE_RELEASES[1]["commit"],
                "target_commit": EXPECTED_RESULTS["canonical_29_4_commit"],
                "target_tree": EXPECTED_RESULTS["canonical_29_4_tree"],
                "commit_count": 16,
            },
        },
        "adaptations": adaptations,
        "dependency_graph": {"nodes": [unit["id"] for unit in adaptations], "edges": []},
        "replay_boundaries": {
            "core_to_knots_29_3": material_boundaries(root, "contrib/roots/replay-core-to-knots-29.3/replay-materials.json"),
            "roots_29_3": material_boundaries(root, "contrib/roots/replay-29.3-release/replay-materials.json"),
            "core_29_4": material_boundaries(root, "contrib/roots/replay-29.4-proposal/replay-materials.json"),
        },
        "context_packets": packets,
        "generators": [
            {
                "id": "generated-manpages",
                "inputs": ["candidate binaries", "candidate help output", "candidate version identity"],
                "commands": [
                    ["cmake", "--build", "<candidate-build-dir>", "--target", "generate-bitcoin-conf"],
                    ["python3", "contrib/devtools/gen-manpages.py", "<candidate-build-dir>/bin"],
                ],
                "outputs": ["doc/man/bitcoin-cli.1", "doc/man/bitcoin-qt.1", "doc/man/bitcoin-tx.1", "doc/man/bitcoin-util.1", "doc/man/bitcoin-wallet.1", "doc/man/bitcoind.1"],
                "freshness_gate": "generated-output freshness must pass before approval",
            }
        ],
        "workflows": {
            "human_contract": "contrib/roots/llm-execution-contract-v1.md",
            "maintainer_runbook": "contrib/roots/maintainer-runbook.md",
            "ordered_steps": [
                "verify-clean-clone", "fetch-explicit-tags", "verify-tag-signatures", "verify-objects",
                "reconstruct-core-to-knots-29.3", "reconstruct-roots-29.3", "replay-core-29.4",
                "compare-incremental-oracle", "run-required-tests", "request-bounded-approvals",
                "account-every-hunk", "prepare-release-evidence", "safe-stop",
            ],
            "commands": {
                "validate_contract": ["python3", "contrib/devtools/roots-llm-contract.py", "validate", "contrib/roots/llm-execution-contract-v1.json"],
                "verify_core_29_3_tag": ["git", "-C", "$SOURCE", "verify-tag", "v29.3"],
                "verify_core_29_4_tag": ["git", "-C", "$SOURCE", "verify-tag", "v29.4"],
                "rehearse": ["python3", "contrib/devtools/roots-llm-contract.py", "rehearse", "contrib/roots/llm-execution-contract-v1.json", "--repository", "$SOURCE", "--output-directory", "$OUTPUT", "--offline-locked-mirror"],
            },
        },
        "expected_results": EXPECTED_RESULTS,
        "schemas": {
            "evidence_bundle": {"required_fields": ["schema_version", "status", "trees", "artifact_digests", "commit_to_adaptation_map", "contract_digest"]},
            "stable_report": {"required_fields": ["schema_version", "status", "mode", "roots_29_3", "core_29_4", "evidence_bundle_digest", "tag_state_changed"]},
            "safe_stop": {"required_fields": ["schema_version", "status", "code", "action", "resumable", "contract_digest"]},
        },
        "required_tests": [
            "ci/test/test_roots_llm_contract.py",
            "ci/test/test_roots_maintainer_runbook.py",
            "ci/test/test_roots_replay.py",
            "ci/test/test_roots_trusted_replay_ci.py",
            "ci/test/test_roots_pr_gate.py",
            "ci/test/test_roots_continuous_accounting.py",
            "ci/test/test_roots_release_evidence.py",
            "ci/test/test_prepare_release.py",
        ],
        "manual_approvals": {
            "adaptation_disposition": MANUAL_APPROVAL,
            "release_authorization": {
                "question": "Does an authorized human approve signing, tagging, and publication after all evidence gates pass?",
                "allowed_answers": ["approve", "reject"],
                "default": "reject",
                "outside_contract": True,
            },
        },
        "forbidden_actions": [
            "push", "publish", "create-or-move-release-tag", "sign-release-artifacts", "use-credentials",
            "read-private-paths", "use-knots-as-future-base", "use-previous-roots-trunk-as-future-base",
            "continue-after-lock-mismatch", "invent-manual-decision",
        ],
        "recovery": {
            "interrupt": "preserve state, candidate, reports, and tag snapshot; do not delete or improvise",
            "resume": "inspect then resume only with identical base, tree, manifest, materials, state directory, and candidate tree",
            "abandon": "write replay-abandoned.json and retain evidence",
            "safe_stop_output": "llm-safe-stop.json",
        },
        "classifications": {
            "verified_facts": [
                "The locked 29.3 and 29.4 commits, trees, replay artifacts, and reviewed 29.4 topology are versioned repository evidence.",
                "The accepted 29.4 candidate is Core-tag-rooted and the incremental oracle is rejected.",
            ],
            "policy_decisions": [
                "Future Roots releases start from a verified Bitcoin Core tag, never Knots or a previous Roots trunk.",
                "Policy changes remain local policy and must not become consensus rules.",
                "This contract never grants push, tag, signing, publication, credential, or secret access authority.",
            ],
            "optional_recommendations": [
                "Use a network-free locked local mirror for repeatable rehearsals.",
                "Retain disposable replay outputs until review completes.",
            ],
            "unresolved_questions": [
                "The historical ledger records Core v29.3 and v29.4 tag signatures as unverified; online release use must verify and record signer fingerprints.",
                "No later-Core tag is allowlisted; its tag object, commit, tree, fixture, materials, and tests require review before replay.",
            ],
        },
        "limits": {"adaptation_count": 16, "max_context_paths_per_packet": 6, "max_path_count_per_packet": 300, "network_during_rehearsal": False},
    }
    contract["digest"] = digest(contract)
    return contract


def fail(code: str, message: str) -> None:
    raise ContractError(f"{code}: {message}")


def shape_error_code(path: str) -> str:
    if path.startswith("$.inputs.core_releases"):
        return "E_TAG_LOCK"
    if path.startswith("$.inputs"):
        return "E_INPUT"
    if path.startswith("$.allowed_remotes"):
        return "E_REMOTE"
    if path.startswith("$.adaptations"):
        if ".dependencies" in path:
            return "E_DEPENDENCY"
        if any(field in path for field in (".scope_pointer", ".path_count", ".path_digest")):
            return "E_PATH"
        return "E_ORDER"
    if path.startswith("$.dependency_graph"):
        return "E_DEPENDENCY"
    if path.startswith("$.replay_boundaries"):
        return "E_EXPECTED"
    if path.startswith("$.context_packets"):
        if any(field in path for field in (".context_paths", ".scope")):
            return "E_PATH"
        if ".generated" in path:
            return "E_GENERATOR"
        if ".manual_approval" in path:
            return "E_MANUAL"
        return "E_CONTEXT"
    if path.startswith("$.generators"):
        return "E_GENERATOR"
    if path.startswith("$.expected_results"):
        return "E_EXPECTED"
    if path.startswith("$.manual_approvals"):
        return "E_MANUAL"
    if path.startswith("$.forbidden_actions"):
        return "E_AUTHORITY"
    return "E_SCHEMA"


def validate_shape(observed: Any, expected: Any, path: str = "$") -> None:
    """Reject every malformed JSON container before semantic field access."""
    code = shape_error_code(path)
    if isinstance(expected, dict):
        if not isinstance(observed, dict):
            fail(code, f"{path} must be an object")
        if set(observed) != set(expected):
            fail(code, f"{path} fields differ from the contract schema")
        for key, item in expected.items():
            validate_shape(observed[key], item, f"{path}.{key}")
        return
    if isinstance(expected, list):
        if not isinstance(observed, list):
            fail(code, f"{path} must be an array")
        if len(observed) != len(expected):
            fail(code, f"{path} array length differs from the contract schema")
        for index, item in enumerate(expected):
            validate_shape(observed[index], item, f"{path}[{index}]")
        return
    if type(observed) is not type(expected):
        fail(code, f"{path} has the wrong scalar type")


def validate_contract(value: Any, root: Path) -> None:
    if not isinstance(value, dict):
        fail("E_SCHEMA", "contract must be an object")
    expected = build_contract(root)
    if value.get("schema_version") != 1 or value.get("contract_id") != expected["contract_id"] or value.get("contract_version") != "1.0.0":
        fail("E_SCHEMA", "unsupported contract identity or version")
    validate_shape(value, expected)
    inputs = value.get("inputs")
    if not isinstance(inputs, dict) or not isinstance(inputs.get("core_releases"), list) or len(inputs["core_releases"]) != 2:
        fail("E_SCHEMA", "immutable inputs are missing")
    for observed, locked in zip(inputs["core_releases"], CORE_RELEASES, strict=True):
        if observed.get("tag") != locked["tag"] or observed.get("tag_object") != locked["tag_object"]:
            fail("E_TAG_LOCK", f"Core tag lock differs for {locked['id']}")
        for field in ("commit", "tree"):
            if observed.get(field) != locked[field]:
                fail("E_HASH_LOCK", f"Core {field} lock differs for {locked['id']}")
        if observed.get("signature") != locked["signature"]:
            fail("E_TAG_LOCK", f"Core signature rule differs for {locked['id']}")
    adaptations = value.get("adaptations")
    if not isinstance(adaptations, list) or [item.get("id") for item in adaptations if isinstance(item, dict)] != [item["id"] for item in expected["adaptations"]]:
        fail("E_ORDER", "adaptation order differs from canonical topology")
    for observed, locked in zip(adaptations, expected["adaptations"], strict=True):
        if observed.get("order") != locked["order"] or observed.get("commit_group") != locked["commit_group"]:
            fail("E_ORDER", f"commit grouping differs for {locked['id']}")
        if observed.get("dependencies") != locked["dependencies"]:
            fail("E_DEPENDENCY", f"dependency set differs for {locked['id']}")
        for field in ("scope_pointer", "path_count", "path_digest"):
            if observed.get(field) != locked[field]:
                fail("E_PATH", f"scope {field} differs for {locked['id']}")
    packets = value.get("context_packets")
    if not isinstance(packets, list) or len(packets) != 16:
        fail("E_CONTEXT", "one bounded context packet is required per adaptation")
    for observed, locked in zip(packets, expected["context_packets"], strict=True):
        if observed.get("id") != locked["id"] or observed.get("context_paths") != locked["context_paths"] or observed.get("scope") != locked["scope"]:
            fail("E_PATH", f"context paths differ for {locked['id']}")
        if observed.get("generated") != locked["generated"]:
            fail("E_GENERATOR", f"generated inputs or outputs differ for {locked['id']}")
        if observed.get("manual_approval") != MANUAL_APPROVAL:
            fail("E_MANUAL", f"manual instruction is missing or ambiguous for {locked['id']}")
        if len(observed["context_paths"]) > expected["limits"]["max_context_paths_per_packet"]:
            fail("E_CONTEXT", f"context packet is unbounded for {locked['id']}")
    if value.get("dependency_graph") != expected["dependency_graph"]:
        fail("E_DEPENDENCY", "dependency graph differs from the manifest")
    if value.get("replay_boundaries") != expected["replay_boundaries"]:
        fail("E_EXPECTED", "pre/post tree or material digest boundary differs")
    if value.get("generators") != expected["generators"]:
        fail("E_GENERATOR", "generator inputs, commands, or outputs differ")
    if value.get("expected_results") != EXPECTED_RESULTS:
        fail("E_EXPECTED", "expected replay result differs")
    if value.get("manual_approvals") != expected["manual_approvals"]:
        fail("E_MANUAL", "manual approval gate is missing or ambiguous")
    if value.get("forbidden_actions") != expected["forbidden_actions"]:
        fail("E_AUTHORITY", "forbidden action boundary differs")
    if value.get("digest") != digest({key: item for key, item in value.items() if key != "digest"}):
        fail("E_DIGEST", "contract digest mismatch")
    if value != expected:
        fail("E_DRIFT", "contract differs from versioned source records")


def run(command: list[Any], *, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([str(item) for item in command], cwd=cwd, check=False, text=True, capture_output=True)
    if result.returncode:
        raise ContractError(f"E_COMMAND: {' '.join(str(item) for item in command)}: {result.stderr.strip()}")
    return result


def git(repository: Path, *arguments: str) -> str:
    return run(["git", "-C", repository, *arguments]).stdout.strip()


def replay_layer(root: Path, repository: Path, output: Path, name: str, revision: str, tree: str, materials_relative: str, final_state: str) -> tuple[str, dict[str, str]]:
    replay = root / "contrib/devtools/roots-replay.py"
    materials_root = root / materials_relative
    state = output / f"{name}-state"
    review = output / f"{name}-review"
    state.mkdir()
    review.mkdir()
    run([
        sys.executable, replay, "replay", "--repository", repository, "--revision", revision,
        "--expected-tree", tree, "--manifest", materials_root / "adaptation-manifest-29.3.json",
        "--materials", materials_root / "replay-materials.json", "--materials-root", materials_root,
        "--state-directory", state, "--apply",
    ])
    state_path = state / final_state
    run([sys.executable, replay, "report", "--state", state_path, "--output-directory", review])
    exported = review / "replay-generated-series.patch"
    run([sys.executable, replay, "export-patches", "--repository", state / "owned-candidate", "--state", state_path, "--output", exported])
    state_value = load_json(state_path)
    artifacts = {
        "state": file_digest(state_path),
        "report_json": file_digest(review / "replay-review.json"),
        "report_text": file_digest(review / "replay-review.txt"),
        "generated_export": file_digest(exported),
        "outcome_set": digest(state_value["completed_units"]),
    }
    return state_value["candidate_tree"], artifacts


def write_new_json(path: Path, value: Any) -> None:
    if path.exists() or path.is_symlink():
        fail("E_OUTPUT", f"refusing to replace {path.name}")
    path.write_bytes(canonical(value) + b"\n")


def rehearse(contract_path: Path, repository: Path, output: Path, offline: bool) -> None:
    root = project_root(contract_path)
    if not repository.is_absolute() or not output.is_absolute() or output.exists():
        fail("E_PATH", "repository and new output directory must be absolute")
    output.mkdir()
    value = load_json(contract_path)
    validate_contract(value, root)
    if git(repository, "status", "--porcelain"):
        fail("E_DIRTY", "rehearsal repository must be clean")
    if not offline:
        for release in CORE_RELEASES:
            if git(repository, "rev-parse", f"refs/tags/{release['tag']}") != release["tag_object"]:
                fail("E_TAG_LOCK", f"annotated tag object is unavailable for {release['tag']}")
            run(["git", "-C", repository, "verify-tag", release["tag"]])
    tags_before = git(repository, "for-each-ref", "--format=%(refname) %(objectname)", "refs/tags")
    original_head = git(repository, "rev-parse", "HEAD")
    replay = root / "contrib/devtools/roots-replay.py"
    for release in CORE_RELEASES:
        run([sys.executable, replay, "verify", "--repository", repository, "--revision", release["commit"], "--expected-tree", release["tree"]])
    method = load_json(root / "contrib/roots/methodology-v1.json")["reconstructions"]
    git(repository, "checkout", "--quiet", "--detach", CORE_RELEASES[0]["commit"])
    stage_tree, stage_artifacts = replay_layer(root, repository, output, "core-knots-29.3", CORE_RELEASES[0]["commit"], CORE_RELEASES[0]["tree"], "contrib/roots/replay-core-to-knots-29.3", "replay-state-0010.json")
    if stage_tree != EXPECTED_RESULTS["core_to_knots_29_3_tree"] or stage_artifacts != method["core-to-knots-29.3"]["artifact_digests"]:
        fail("E_EXPECTED", "Core-to-Knots 29.3 rehearsal differs from accepted evidence")
    knots_commit = value["inputs"]["knots_29_3_stage"]["lineage_commit"]
    git(repository, "checkout", "--quiet", "--detach", knots_commit)
    roots_tree, roots_artifacts = replay_layer(root, repository, output, "roots-29.3", knots_commit, stage_tree, "contrib/roots/replay-29.3-release", "replay-state-0016.json")
    if roots_tree != EXPECTED_RESULTS["roots_29_3_tree"] or roots_artifacts != method["roots-29.3"]["artifact_digests"]:
        fail("E_EXPECTED", "Roots 29.3 rehearsal differs from accepted evidence")
    git(repository, "checkout", "--quiet", "--detach", original_head)
    canonical_repository = output / "canonical-29.4"
    result = run([root / "contrib/roots/replay-29.4-proposal/canonical-lineage.bash", repository, canonical_repository])
    expected_lines = {
        f"base_commit={CORE_RELEASES[1]['commit']}",
        f"target_commit={EXPECTED_RESULTS['canonical_29_4_commit']}",
        f"target_tree={EXPECTED_RESULTS['canonical_29_4_tree']}",
    }
    if not expected_lines <= set(result.stdout.splitlines()):
        fail("E_EXPECTED", "canonical 29.4 constructor result differs")
    observed_commits = git(canonical_repository, "rev-list", "--first-parent", "--reverse", f"{CORE_RELEASES[1]['commit']}..{EXPECTED_RESULTS['canonical_29_4_commit']}").splitlines()
    expected_map = [{"commit": item["commit_group"], "adaptation_ids": [item["id"]]} for item in value["adaptations"]]
    if observed_commits != [item["commit"] for item in expected_map]:
        fail("E_ORDER", "canonical first-parent commit map differs")
    tags_after = git(repository, "for-each-ref", "--format=%(refname) %(objectname)", "refs/tags")
    if tags_after != tags_before or git(repository, "rev-parse", "HEAD") != original_head:
        fail("E_AUTHORITY", "rehearsal changed source tag or HEAD state")
    artifact_digests = {"core_to_knots_29_3": stage_artifacts, "roots_29_3": roots_artifacts}
    evidence = {
        "schema_version": 1,
        "status": "passed",
        "trees": {"core_to_knots_29_3": stage_tree, "roots_29_3": roots_tree, "core_29_4": EXPECTED_RESULTS["canonical_29_4_tree"]},
        "artifact_digests": artifact_digests,
        "commit_to_adaptation_map": expected_map,
        "contract_digest": value["digest"],
    }
    evidence_path = output / "llm-evidence-bundle.json"
    write_new_json(evidence_path, evidence)
    report = {
        "schema_version": 1,
        "status": "passed",
        "mode": "offline-locked-mirror" if offline else "verified-tags",
        "roots_29_3": {"stage_tree": stage_tree, "final_tree": roots_tree},
        "core_29_4": {"base": CORE_RELEASES[1]["commit"], "target_commit": EXPECTED_RESULTS["canonical_29_4_commit"], "target_tree": EXPECTED_RESULTS["canonical_29_4_tree"], "commit_count": len(expected_map)},
        "evidence_bundle_digest": file_digest(evidence_path),
        "tag_state_changed": False,
    }
    write_new_json(output / "llm-rehearsal-report.json", report)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    generate = subparsers.add_parser("generate")
    generate.add_argument("output", type=Path)
    generate.add_argument("--replace", action="store_true", help="replace only the canonical generated contract path")
    validate = subparsers.add_parser("validate")
    validate.add_argument("contract", type=Path)
    rehearsal = subparsers.add_parser("rehearse")
    rehearsal.add_argument("contract", type=Path)
    rehearsal.add_argument("--repository", type=Path, required=True)
    rehearsal.add_argument("--output-directory", type=Path, required=True)
    rehearsal.add_argument("--offline-locked-mirror", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.command == "generate":
            output = args.output.resolve()
            root = output.parents[2]
            if output.name != CONTRACT_NAME:
                fail("E_PATH", f"output must be named {CONTRACT_NAME}")
            generated = build_contract(root)
            if args.replace:
                output.write_bytes(canonical(generated) + b"\n")
            else:
                write_new_json(output, generated)
        elif args.command == "validate":
            contract = args.contract.resolve()
            validate_contract(load_json(contract), project_root(contract))
            print(f"roots-llm-contract: valid {load_json(contract)['digest']}")
        else:
            rehearse(args.contract.resolve(), args.repository.resolve(), args.output_directory.resolve(), args.offline_locked_mirror)
            print(f"roots-llm-contract: rehearsal passed {args.output_directory.resolve() / 'llm-rehearsal-report.json'}")
    except ContractError as error:
        if args.command == "rehearse":
            output = args.output_directory.resolve()
            if output.is_dir() and not (output / "llm-safe-stop.json").exists():
                code = str(error).split(":", 1)[0]
                contract_digest = "unavailable"
                try:
                    contract_digest = load_json(args.contract.resolve()).get("digest", "unavailable")
                except ContractError:
                    pass
                write_new_json(output / "llm-safe-stop.json", {"schema_version": 1, "status": "safe-stop", "code": code, "action": "preserve evidence and request review", "resumable": True, "contract_digest": contract_digest})
        print(f"roots-llm-contract: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
