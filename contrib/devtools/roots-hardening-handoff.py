#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Validate the hardening handoff and gate cohesive post-29.4 changes."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any, NoReturn


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PLAN = ROOT / ".gestalt/cpp-hardening-and-backports.org"
DEFAULT_CONTRACT = ROOT / "contrib/roots/hardening-handoff-v1.json"
ACCOUNTING_TOOL = ROOT / "contrib/devtools/roots-continuous-accounting.py"
MANIFEST = ROOT / "contrib/roots/adaptation-manifest-29.3.json"
REGISTRY = ROOT / "contrib/roots/post-methodology-adaptations.json"


class ContractError(Exception):
    """A deterministic, user-actionable handoff failure."""

    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def fail(code: str, message: str) -> NoReturn:
    raise ContractError(code, message)


def mapping(value: Any, code: str, name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        fail(code, f"{name} must be an object")
    return value


def sequence(value: Any, code: str, name: str) -> list[Any]:
    if not isinstance(value, list):
        fail(code, f"{name} must be an array")
    return value


def string(value: Any, code: str, name: str) -> str:
    if not isinstance(value, str) or not value:
        fail(code, f"{name} must be a non-empty string")
    return value


def integer(value: Any, code: str, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        fail(code, f"{name} must be an integer")
    return value


def read_json(path: Path, code: str) -> dict[str, Any]:
    try:
        raw = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        fail(code, f"cannot read {path}: {error}")
    try:
        return mapping(json.loads(raw), code, str(path))
    except json.JSONDecodeError as error:
        fail(code, f"invalid JSON in {path}: line {error.lineno} column {error.colno}")


def canonical(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def sha256(path: Path) -> str:
    return "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()


def run_git(repository: Path, *arguments: str, code: str = "E_GIT") -> str:
    result = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode:
        detail = result.stderr.strip().splitlines()
        fail(code, detail[-1] if detail else "git command failed")
    return result.stdout.strip()


def load_accounting():
    spec = importlib.util.spec_from_file_location("roots_handoff_accounting", ACCOUNTING_TOOL)
    if spec is None or spec.loader is None:
        fail("E_ACCOUNTING_TOOL", "continuous-accounting tool cannot be loaded")
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except Exception as error:  # pragma: no cover - installation failure boundary
        fail("E_ACCOUNTING_TOOL", f"continuous-accounting tool failed to load: {error}")
    return module


def parse_plan(path: Path) -> tuple[dict[str, dict[str, Any]], str]:
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        fail("E_PLAN_READ", f"cannot read hardening plan: {error}")
    headings: dict[str, dict[str, Any]] = {}
    current: dict[str, Any] | None = None
    heading_re = re.compile(r"^(\*{1,2}) (TODO|WIP|DONE) \[#[ABC]\] (.+)$")
    for line_number, line in enumerate(text.splitlines(), 1):
        match = heading_re.match(line)
        if match:
            current = {
                "level": len(match.group(1)),
                "state": match.group(2),
                "title": match.group(3),
                "properties": {},
                "line": line_number,
            }
            continue
        if current is None:
            continue
        prop = re.match(r"^:([A-Z_]+):\s*(.*?)\s*$", line)
        if not prop:
            continue
        key, value = prop.groups()
        current["properties"][key] = value
        if key == "ID":
            if value in headings:
                fail("E_PLAN_DUPLICATE_ID", f"duplicate milestone ID: {value}")
            headings[value] = current
    return headings, text


def validate_contract(contract: dict[str, Any]) -> None:
    if integer(contract.get("schema_version"), "E_CONTRACT_SCHEMA", "schema_version") != 1:
        fail("E_CONTRACT_SCHEMA", "schema_version must be 1")
    if contract.get("kind") != "roots-hardening-handoff":
        fail("E_CONTRACT_KIND", "kind must be roots-hardening-handoff")
    baseline = mapping(contract.get("baseline"), "E_BASELINE", "baseline")
    exact = {
        "core_tag": "v29.4",
        "core_commit": "sha1:3fc0865963a38b871e9f7d94e6151c4953563516",
        "core_tree": "sha1:38ad59b187f59647eb90ad1347bc481485ef4d01",
        "candidate_commit": "sha1:cbc88cff9b35b95a549c0313e424e13093fcd6a1",
        "candidate_tree": "sha1:39a5e30207a09962e78ae81c24cc65b1e478ef90",
    }
    for key, expected in exact.items():
        if baseline.get(key) != expected:
            fail("E_BASELINE_LOCK", f"baseline.{key} differs from the accepted lock")
    authorities = mapping(contract.get("authorities"), "E_AUTHORITIES", "authorities")
    required_domains = {
        "lineage",
        "adaptation_inventory",
        "backport_semantics_and_invariants",
        "modularization_design",
        "release_provenance",
        "continuous_accounting",
    }
    if set(authorities) != required_domains:
        fail("E_AUTHORITIES", "authority domains are incomplete or unexpected")
    for domain, records_value in authorities.items():
        records = sequence(records_value, "E_AUTHORITIES", f"authorities.{domain}")
        if not records:
            fail("E_AUTHORITIES", f"authorities.{domain} is empty")
        for index, record_value in enumerate(records):
            record = string(record_value, "E_AUTHORITIES", f"authorities.{domain}[{index}]")
            if not (ROOT / record).exists():
                fail("E_AUTHORITY_PATH", f"authority path does not exist: {record}")
    pins = mapping(contract.get("pins"), "E_PINS", "pins")
    for name in ("risk_paths", "risk_sources", "analyzers", "test_matrices", "performance_baselines"):
        if name not in pins:
            fail("E_PINS", f"pins.{name} is required")
    analyzers = mapping(pins["analyzers"], "E_PINS", "pins.analyzers")
    for name in ("risk_paths", "risk_sources"):
        values = sequence(pins[name], "E_PINS", f"pins.{name}")
        if not values:
            fail("E_PINS", f"pins.{name} is empty")
        for index, value in enumerate(values):
            item = string(value, "E_PINS", f"pins.{name}[{index}]")
            if name == "risk_sources" and not (ROOT / item).exists():
                fail("E_PINS", f"risk source does not exist: {item}")
    for name in ("fast_candidates", "deep_candidates", "selection_rule"):
        if name not in analyzers:
            fail("E_PINS", f"pins.analyzers.{name} is required")
    for name in ("fast_candidates", "deep_candidates"):
        values = sequence(analyzers[name], "E_PINS", f"pins.analyzers.{name}")
        if not values:
            fail("E_PINS", f"pins.analyzers.{name} is empty")
        for index, value in enumerate(values):
            string(value, "E_PINS", f"pins.analyzers.{name}[{index}]")
    string(analyzers["selection_rule"], "E_PINS", "pins.analyzers.selection_rule")
    matrices = mapping(pins["test_matrices"], "E_PINS", "pins.test_matrices")
    if set(matrices) != {"build", "behavior", "portability"}:
        fail("E_PINS", "test matrices must cover build, behavior, and portability")
    for name, values in matrices.items():
        entries = sequence(values, "E_PINS", f"pins.test_matrices.{name}")
        if not entries:
            fail("E_PINS", f"pins.test_matrices.{name} is empty")
        for index, value in enumerate(entries):
            string(value, "E_PINS", f"pins.test_matrices.{name}[{index}]")
    performance = mapping(pins["performance_baselines"], "E_PINS", "pins.performance_baselines")
    source = string(performance.get("source"), "E_PINS", "pins.performance_baselines.source")
    if not (ROOT / source).exists():
        fail("E_PINS", f"performance baseline does not exist: {source}")
    collectors = sequence(
        performance.get("required_collectors"),
        "E_PINS",
        "pins.performance_baselines.required_collectors",
    )
    if not collectors:
        fail("E_PINS", "performance collectors are empty")
    for index, value in enumerate(collectors):
        string(value, "E_PINS", f"performance collector[{index}]")
    string(performance.get("comparison_rule"), "E_PINS", "pins.performance_baselines.comparison_rule")


def validate_plan(plan_path: Path, contract_path: Path) -> dict[str, Any]:
    contract = read_json(contract_path, "E_CONTRACT_JSON")
    validate_contract(contract)
    headings, text = parse_plan(plan_path)
    implementations = sequence(
        contract.get("approved_implementation_milestones"),
        "E_IMPLEMENTATIONS",
        "approved_implementation_milestones",
    )
    implementation_ids: set[str] = set()
    seams = read_json(ROOT / "contrib/roots/portability-seams-29.4.json", "E_SEAMS_JSON")
    proposed = {
        adaptation
        for seam_value in sequence(seams.get("seams"), "E_SEAMS", "seams")
        for adaptation in sequence(
            mapping(seam_value, "E_SEAMS", "seam").get("adaptation_ids"),
            "E_SEAMS",
            "seam.adaptation_ids",
        )
        if mapping(seam_value, "E_SEAMS", "seam").get("decision") == "propose"
    }
    covered: set[str] = set()
    for index, implementation_value in enumerate(implementations):
        implementation = mapping(implementation_value, "E_IMPLEMENTATIONS", f"implementation[{index}]")
        milestone_id = string(implementation.get("id"), "E_IMPLEMENTATIONS", "implementation.id")
        if milestone_id in implementation_ids:
            fail("E_IMPLEMENTATION_DUPLICATE", f"duplicate implementation milestone: {milestone_id}")
        implementation_ids.add(milestone_id)
        if milestone_id not in headings:
            fail("E_IMPLEMENTATION_MISSING", f"implementation milestone is missing from plan: {milestone_id}")
        covered.update(
            string(value, "E_IMPLEMENTATIONS", "implementation.adaptation_ids[]")
            for value in sequence(implementation.get("adaptation_ids"), "E_IMPLEMENTATIONS", "implementation.adaptation_ids")
        )
    if covered != proposed:
        fail("E_IMPLEMENTATION_COVERAGE", "approved seam proposals are not covered exactly once")

    validation = mapping(contract.get("validation"), "E_VALIDATION", "validation")
    allowed = set(sequence(validation.get("allowed_dispositions"), "E_MAPPING", "allowed_dispositions"))
    mapped: dict[str, str] = {}
    groups = sequence(contract.get("disposition_groups"), "E_MAPPING", "disposition_groups")
    for group_index, group_value in enumerate(groups):
        group = mapping(group_value, "E_MAPPING", f"disposition_groups[{group_index}]")
        status = string(group.get("status"), "E_MAPPING", "disposition.status")
        if status not in allowed:
            fail("E_MAPPING_STATUS", f"invalid disposition status: {status}")
        string(group.get("rationale"), "E_MAPPING", "disposition.rationale")
        replacements = sequence(group.get("replacement_refs"), "E_MAPPING", "disposition.replacement_refs")
        if not replacements:
            fail("E_MAPPING_REPLACEMENT", "every disposition group needs replacement references")
        for reference in replacements:
            string(reference, "E_MAPPING_REPLACEMENT", "replacement reference")
        for milestone_value in sequence(group.get("milestone_ids"), "E_MAPPING", "disposition.milestone_ids"):
            milestone = string(milestone_value, "E_MAPPING", "milestone ID")
            if milestone in mapped:
                fail("E_MAPPING_DUPLICATE", f"milestone mapped more than once: {milestone}")
            mapped[milestone] = status
    original_ids = set(headings) - implementation_ids
    if set(mapped) != original_ids:
        missing = sorted(original_ids - set(mapped))
        extra = sorted(set(mapped) - original_ids)
        fail("E_MAPPING_INCOMPLETE", f"mapping mismatch missing={missing} extra={extra}")
    if len(mapped) != integer(validation.get("expected_original_milestones"), "E_MAPPING", "expected_original_milestones"):
        fail("E_MAPPING_COUNT", "original milestone count differs from the reviewed inventory")
    l1_count = sum(headings[milestone]["level"] == 1 for milestone in mapped)
    if l1_count != integer(validation.get("expected_original_l1"), "E_MAPPING", "expected_original_l1"):
        fail("E_MAPPING_COUNT", "original L1 count differs from the reviewed inventory")
    if len(mapped) - l1_count != integer(validation.get("expected_original_l2"), "E_MAPPING", "expected_original_l2"):
        fail("E_MAPPING_COUNT", "original L2 count differs from the reviewed inventory")
    required_dispositions = mapping(
        validation.get("required_dispositions"),
        "E_MAPPING_STATUS",
        "required_dispositions",
    )
    for milestone, expected_status in required_dispositions.items():
        if mapped.get(milestone) != expected_status:
            fail("E_MAPPING_STATUS", f"{milestone} must be {expected_status}")

    required_properties = mapping(validation.get("required_l1_properties"), "E_L1_ACCEPTANCE", "required_l1_properties")
    for milestone_value in sequence(contract.get("behavior_l1_ids"), "E_L1_ACCEPTANCE", "behavior_l1_ids"):
        milestone = string(milestone_value, "E_L1_ACCEPTANCE", "behavior L1 ID")
        heading = headings.get(milestone)
        if heading is None or heading["level"] != 1:
            fail("E_L1_ACCEPTANCE", f"behavior L1 is missing: {milestone}")
        for key, expected_value in required_properties.items():
            if heading["properties"].get(key) != expected_value:
                fail("E_L1_ACCEPTANCE", f"{milestone} lacks {key}={expected_value}")
    for phrase_value in sequence(validation.get("forbidden_stale_phrases"), "E_STALE", "forbidden_stale_phrases"):
        phrase = string(phrase_value, "E_STALE", "stale phrase")
        if phrase in text:
            fail("E_STALE_29_3", f"stale assumption remains in plan: {phrase}")
    for phrase_value in sequence(validation.get("forbidden_duplicate_claims"), "E_DUPLICATE_AUTHORITY", "forbidden_duplicate_claims"):
        phrase = string(phrase_value, "E_DUPLICATE_AUTHORITY", "duplicate claim")
        if phrase in text:
            fail("E_DUPLICATE_AUTHORITY", f"duplicate source-of-truth claim remains: {phrase}")
    return {
        "decision": "accepted",
        "kind": "hardening-plan-validation",
        "original_milestones": len(mapped),
        "original_l1": l1_count,
        "original_l2": len(mapped) - l1_count,
        "implementation_milestones": sorted(implementation_ids),
        "dispositions": {status: list(mapped.values()).count(status) for status in sorted(allowed)},
        "baseline": contract["baseline"],
    }


def known_adaptations() -> set[str]:
    manifest = read_json(MANIFEST, "E_MANIFEST_JSON")
    registry = read_json(REGISTRY, "E_REGISTRY_JSON")
    ids: set[str] = set()
    for index, unit_value in enumerate(sequence(manifest.get("units"), "E_MANIFEST", "manifest.units")):
        unit = mapping(unit_value, "E_MANIFEST", f"manifest.units[{index}]")
        ids.add(string(unit.get("id"), "E_MANIFEST", "manifest unit ID"))
    for index, unit_value in enumerate(sequence(registry.get("units"), "E_REGISTRY", "registry.units")):
        unit = mapping(unit_value, "E_REGISTRY", f"registry.units[{index}]")
        ids.add(string(unit.get("id"), "E_REGISTRY", "registry unit ID"))
    return ids


def gate_change(repository: Path, base: str, head: str, evidence_path: Path, record_path: Path, contract_path: Path) -> dict[str, Any]:
    contract = read_json(contract_path, "E_CONTRACT_JSON")
    validate_contract(contract)
    evidence = read_json(evidence_path, "E_EVIDENCE_JSON")
    if integer(evidence.get("schema_version"), "E_EVIDENCE_SCHEMA", "evidence.schema_version") != 1:
        fail("E_EVIDENCE_SCHEMA", "evidence.schema_version must be 1")
    change_gate = mapping(contract.get("change_gate"), "E_CHANGE_GATE", "change_gate")
    change_class = string(evidence.get("change_class"), "E_CHANGE_CLASS", "change_class")
    allowed_classes = set(sequence(change_gate.get("allowed_change_classes"), "E_CHANGE_GATE", "allowed_change_classes"))
    if change_class not in allowed_classes:
        fail("E_CHANGE_CLASS", f"unsupported change class: {change_class}")
    action = string(evidence.get("adaptation_action"), "E_ADAPTATION_ACTION", "adaptation_action")
    if action not in {"update", "preserve"}:
        fail("E_ADAPTATION_ACTION", "adaptation_action must be update or preserve")
    adaptation_ids = [
        string(value, "E_ADAPTATION", "adaptation_ids[]")
        for value in sequence(evidence.get("adaptation_ids"), "E_ADAPTATION", "adaptation_ids")
    ]
    if not adaptation_ids or len(set(adaptation_ids)) != len(adaptation_ids):
        fail("E_ADAPTATION", "adaptation_ids must be non-empty and unique")
    unknown = sorted(set(adaptation_ids) - known_adaptations())
    if unknown:
        fail("E_ADAPTATION_UNKNOWN", f"unknown adaptation IDs: {unknown}")
    invariants = mapping(evidence.get("invariants"), "E_INVARIANTS", "invariants")
    required_invariants = mapping(change_gate.get("required_invariants"), "E_CHANGE_GATE", "required_invariants")
    if invariants != required_invariants:
        fail("E_INVARIANTS", "consensus/policy/RDTS invariant evidence is incomplete or failing")
    invariant_baseline = mapping(
        change_gate.get("accepted_invariant_evidence"),
        "E_INVARIANT_BASELINE",
        "accepted_invariant_evidence",
    )
    invariant_path = ROOT / string(
        invariant_baseline.get("path"),
        "E_INVARIANT_BASELINE",
        "accepted_invariant_evidence.path",
    )
    if sha256(invariant_path) != invariant_baseline.get("sha256"):
        fail("E_INVARIANT_BASELINE", "accepted invariant evidence digest differs from its lock")
    accepted = read_json(invariant_path, "E_INVARIANT_BASELINE")
    accepted_gates = sequence(accepted.get("gates"), "E_INVARIANT_BASELINE", "acceptance-evidence.gates")
    matching_gates = [
        mapping(value, "E_INVARIANT_BASELINE", "acceptance gate")
        for value in accepted_gates
        if isinstance(value, dict) and value.get("id") == invariant_baseline.get("gate_id")
    ]
    if len(matching_gates) != 1:
        fail("E_INVARIANT_BASELINE", "accepted invariant gate is missing or duplicated")
    if matching_gates[0].get("status") != "pass" or matching_gates[0].get("waiver") is not False:
        fail("E_INVARIANT_BASELINE", "accepted invariant gate is not an unwaived pass")
    tests = sequence(evidence.get("tests"), "E_TESTS", "tests")
    if not tests or any(not isinstance(value, str) or not value for value in tests):
        fail("E_TESTS", "tests must contain at least one non-empty command")

    repository = repository.resolve()
    if not (repository / ".git").exists():
        fail("E_REPOSITORY", "repository is not a Git worktree")
    base_commit = run_git(repository, "rev-parse", f"{base}^{{commit}}", code="E_BASE_REF")
    head_commit = run_git(repository, "rev-parse", f"{head}^{{commit}}", code="E_HEAD_REF")
    core_commit = contract["baseline"]["core_commit"].removeprefix("sha1:")
    core_tree = contract["baseline"]["core_tree"].removeprefix("sha1:")
    root_commit = contract["baseline"]["candidate_commit"].removeprefix("sha1:")
    root_tree = contract["baseline"]["candidate_tree"].removeprefix("sha1:")
    if run_git(repository, "rev-parse", "refs/tags/v29.4^{}", code="E_CORE_TAG") != core_commit:
        fail("E_CORE_TAG", "v29.4 does not peel to the locked Core commit")
    if run_git(repository, "rev-parse", f"{core_commit}^{{tree}}", code="E_CORE_TREE") != core_tree:
        fail("E_CORE_TREE", "locked Core commit has the wrong tree")
    if run_git(repository, "rev-parse", f"{root_commit}^{{tree}}", code="E_REPLAY_ROOT") != root_tree:
        fail("E_REPLAY_ROOT", "accepted 29.4 candidate has the wrong tree")
    run_git(repository, "merge-base", "--is-ancestor", root_commit, base_commit, code="E_REPLAY_ANCESTRY")
    run_git(repository, "merge-base", "--is-ancestor", base_commit, head_commit, code="E_CHANGE_ANCESTRY")
    if base_commit == head_commit:
        fail("E_EMPTY_CHANGE", "base and head must differ")
    head_line = run_git(repository, "rev-list", "--parents", "-n", "1", head_commit).split()
    if len(head_line) != 2 or head_line[1] != base_commit:
        fail("E_COHESIVE_COMMIT", "head must be one non-merge commit directly on base")
    merge_commits = run_git(repository, "rev-list", "--min-parents=2", f"{root_commit}..{head_commit}")
    if merge_commits:
        fail("E_REPLAY_MERGE", "hardening lineage contains a merge commit")
    changed_paths = run_git(repository, "diff", "--name-only", "-z", base_commit, head_commit).split("\0")
    changed_paths = sorted(path for path in changed_paths if path)
    if not changed_paths:
        fail("E_EMPTY_CHANGE", "change has no paths")
    cpp_paths = [path for path in changed_paths if path.endswith((".cpp", ".h"))]
    if change_class == "documentation-noop" and any(
        not (path.startswith("doc/") or path.endswith((".md", ".org", ".txt"))) for path in changed_paths
    ):
        fail("E_DOC_SCOPE", "documentation-noop contains a non-documentation path")
    if change_class == "documentation-noop" and action != "preserve":
        fail("E_DOC_ACTION", "documentation-noop must preserve adaptation behavior")
    if change_class == "cpp-fix" and not cpp_paths:
        fail("E_CPP_SCOPE", "cpp-fix contains no C++ source or header")
    if change_class == "cpp-fix" and action != "update":
        fail("E_CPP_ACTION", "cpp-fix must update an adaptation unit")
    sensitive = mapping(change_gate.get("sensitive_scope"), "E_CHANGE_GATE", "sensitive_scope")
    consensus_prefixes = tuple(
        string(value, "E_CHANGE_GATE", "sensitive_scope.consensus_prefixes[]")
        for value in sequence(sensitive.get("consensus_prefixes"), "E_CHANGE_GATE", "sensitive_scope.consensus_prefixes")
    )
    policy_prefixes = tuple(
        string(value, "E_CHANGE_GATE", "sensitive_scope.policy_prefixes[]")
        for value in sequence(sensitive.get("policy_prefixes"), "E_CHANGE_GATE", "sensitive_scope.policy_prefixes")
    )
    build_suffixes = tuple(
        string(value, "E_CHANGE_GATE", "sensitive_scope.build_suffixes[]")
        for value in sequence(sensitive.get("build_suffixes"), "E_CHANGE_GATE", "sensitive_scope.build_suffixes")
    )
    rdts_markers = [
        string(value, "E_CHANGE_GATE", "sensitive_scope.rdts_markers[]").lower()
        for value in sequence(sensitive.get("rdts_markers"), "E_CHANGE_GATE", "sensitive_scope.rdts_markers")
    ]
    diff_text = run_git(repository, "diff", "--no-ext-diff", "--unified=0", base_commit, head_commit).lower()
    derived_scope = {
        "consensus_paths": [path for path in changed_paths if path.startswith(consensus_prefixes)],
        "policy_paths": [path for path in changed_paths if path.startswith(policy_prefixes)],
        "build_registration_paths": [path for path in changed_paths if path.endswith(build_suffixes)],
        "rdts_bip110_diff": any(marker in diff_text for marker in rdts_markers),
    }
    if mapping(evidence.get("boundary_scope"), "E_BOUNDARY_SCOPE", "boundary_scope") != derived_scope:
        fail("E_BOUNDARY_SCOPE", "declared boundary scope differs from the Git diff")

    replay = mapping(evidence.get("replay"), "E_REPLAY_EVIDENCE", "replay")
    expected_replay = mapping(change_gate.get("required_replay"), "E_CHANGE_GATE", "required_replay")
    exact_replay = {
        "base_tag": expected_replay["base_tag"],
        "root_commit": expected_replay["root_commit"],
        "root_tree": expected_replay["root_tree"],
        "base_commit": "sha1:" + base_commit,
        "head_commit": "sha1:" + head_commit,
        "head_tree": "sha1:" + run_git(repository, "rev-parse", f"{head_commit}^{{tree}}"),
        "first_parent": True,
        "merge_commits": 0,
    }
    if replay != exact_replay:
        fail("E_REPLAY_EVIDENCE", "replay evidence does not match the exact Git lineage")
    expected_record_digest = string(evidence.get("accounting_record_sha256"), "E_ACCOUNTING_DIGEST", "accounting_record_sha256")
    try:
        actual_record_digest = sha256(record_path)
    except OSError as error:
        fail("E_ACCOUNTING_READ", f"cannot read accounting record: {error}")
    if expected_record_digest != actual_record_digest:
        fail("E_ACCOUNTING_DIGEST", "accounting record digest differs from evidence")
    record = read_json(record_path, "E_ACCOUNTING_JSON")
    accounting = load_accounting()
    try:
        observed = accounting.atoms(repository, base_commit, head_commit)
        accounting.validate(record, observed)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        fail("E_ACCOUNTING", str(error))
    changes = sequence(record.get("changes"), "E_ACCOUNTING", "accounting.changes")
    record_ids = {item.get("adaptation") for item in changes if isinstance(item, dict)}
    if record_ids != set(adaptation_ids):
        fail("E_ACCOUNTING_ADAPTATION", "accounting adaptations differ from evidence")
    tag_state = run_git(repository, "for-each-ref", "--format=%(refname) %(objectname)", "refs/tags")
    return {
        "decision": "accepted",
        "kind": "hardening-change-gate",
        "change_class": change_class,
        "adaptation_action": action,
        "adaptation_ids": sorted(adaptation_ids),
        "base_commit": "sha1:" + base_commit,
        "head_commit": "sha1:" + head_commit,
        "head_tree": exact_replay["head_tree"],
        "changed_paths": changed_paths,
        "accounted_atoms": len(observed),
        "invariants": invariants,
        "boundary_scope": derived_scope,
        "invariant_baseline_sha256": invariant_baseline["sha256"],
        "replay_root": {
            "tag": "v29.4",
            "commit": expected_replay["root_commit"],
            "tree": expected_replay["root_tree"],
        },
        "tag_state_sha256": "sha256:" + hashlib.sha256(tag_state.encode()).hexdigest(),
    }


def write_json(path: Path, value: Any) -> None:
    path.write_text(canonical(value) + "\n", encoding="utf-8")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    subparsers = result.add_subparsers(dest="command", required=True)
    validate = subparsers.add_parser("validate")
    validate.add_argument("--plan", type=Path, default=DEFAULT_PLAN)
    validate.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT)
    validate.add_argument("--output", type=Path)
    gate = subparsers.add_parser("gate")
    gate.add_argument("--repository", type=Path, required=True)
    gate.add_argument("--base", required=True)
    gate.add_argument("--head", required=True)
    gate.add_argument("--evidence", type=Path, required=True)
    gate.add_argument("--record", type=Path, required=True)
    gate.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT)
    gate.add_argument("--output", type=Path, required=True)
    return result


def main() -> int:
    args = parser().parse_args()
    output = getattr(args, "output", None)
    try:
        if args.command == "validate":
            result = validate_plan(args.plan, args.contract)
        else:
            result = gate_change(args.repository, args.base, args.head, args.evidence, args.record, args.contract)
        if output is not None:
            if not output.parent.is_dir():
                fail("E_OUTPUT_DIRECTORY", "output directory does not exist")
            write_json(output, result)
        print(canonical(result))
        return 0
    except ContractError as error:
        if output is not None and output.parent.is_dir():
            contract = None
            try:
                contract = read_json(getattr(args, "contract", DEFAULT_CONTRACT), "E_CONTRACT_JSON")
            except ContractError:
                pass
            safe_name = "hardening-safe-stop.json"
            if isinstance(contract, dict):
                gate_value = contract.get("change_gate")
                if isinstance(gate_value, dict) and isinstance(gate_value.get("safe_stop_output"), str):
                    safe_name = gate_value["safe_stop_output"]
            write_json(output.parent / safe_name, {"code": error.code, "decision": "safe-stop", "kind": "hardening-handoff"})
        print(f"{error.code}: {error}", file=sys.stderr)
        return 1
    except Exception:
        if output is not None and output.parent.is_dir():
            write_json(output.parent / "hardening-safe-stop.json", {"code": "E_INTERNAL", "decision": "safe-stop", "kind": "hardening-handoff"})
        print("E_INTERNAL: unexpected validation failure", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
