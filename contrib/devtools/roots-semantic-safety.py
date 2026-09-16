#!/usr/bin/env python3
"""Fail-closed evidence contracts for Roots portability replays.

This validator never applies a patch, promotes an adaptation, creates a
candidate, or makes a release decision.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from datetime import datetime
from pathlib import Path
from typing import Any


class SafetyError(ValueError):
    pass


SHA256 = re.compile(r"^sha256:[0-9a-f]{64}$")
SHA1 = re.compile(r"^sha1:[0-9a-f]{40}$")
ISO = re.compile(r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$")
CRITICAL_ZONES = {
    "consensus-validation": ("src/consensus/", "src/validation", "src/kernel/"),
    "chain-parameters": ("src/kernel/chainparams", "src/chainparams", "CChainParams"),
    "serialization-script": ("src/serialize", "src/script/", "src/primitives/"),
    "p2p-wire-state": ("src/net", "src/protocol", "src/txrequest"),
    "wallet-database": ("src/wallet/", "src/walletdb"),
    "secrets": (".github/workflows/release", "ci/release", "contrib/devtools/sign"),
    "policy-consensus-boundary": ("src/policy/", "src/txmempool", "src/node/miner"),
    "release-signing": ("contrib/guix/", "contrib/devtools/sign", "doc/release-process"),
    "manifest-self-modification": ("contrib/roots/", "contrib/devtools/roots-"),
}
LOW_RISK_PREFIXES = ("doc/", "test/", "src/qt/locale/", "contrib/")
PROMOTIONS = {"module", "patch", "conditional-replacement"}
PLATFORMS = {"linux", "macos", "windows"}
REQUIRED_INVARIANTS = {
    "no-rdts-bip110-enforcement", "core-valid-block-acceptance",
    "policy-rejection-block-acceptance", "conservative-configurable-defaults",
    "knots-compatibility", "startup-help-config", "roots-feature-regression",
}
INVARIANT_SCOPES = {
    "no-rdts-bip110-enforcement": "roots-specific",
    "core-valid-block-acceptance": "core-knots-shared",
    "policy-rejection-block-acceptance": "roots-specific",
    "conservative-configurable-defaults": "roots-specific",
    "knots-compatibility": "core-knots-shared",
    "startup-help-config": "core-knots-shared",
    "roots-feature-regression": "roots-specific",
}
DIFFERENTIAL_INVARIANTS = {"core-valid-block-acceptance", "knots-compatibility"}
POLICY_BLOCK_INVARIANTS = {"policy-rejection-block-acceptance"}
GOVERNANCE_POLICY = {
    "consensus-regression": ("P0", "stop", "consensus", "consensus-review", "stop-work"),
    "policy-validity-leak": ("P0", "stop", "policy-consensus", "policy-consensus-review", "stop-work"),
    "serialization": ("P0", "stop", "serialization", "serialization-review", "stop-work"),
    "wallet-durable-corruption": ("P0", "stop", "wallet", "wallet-review", "stop-work"),
    "secrets": ("P0", "stop", "security", "security-response", "security"),
    "provenance": ("P0", "stop", "provenance", "provenance-review", "stop-work"),
    "incomplete-delta": ("P0", "stop", "release", "release-review", "stop-work"),
    "remote-crash": ("P1", "hold", "subsystem", "security-review", "security-sensitive"),
    "benign-static-analysis": ("P3", "continue", "affected", "ordinary-review", "ordinary-review"),
    "time-pressured-release": ("P1", "hold", "release", "release-review", "ordinary-review"),
}
LIFECYCLE_POLICY = {
    "exact-cherry-pick": "absorbed", "rebased-equivalent": "absorbed",
    "partial": "rewritten", "reverted": "rewritten", "renamed": "rewritten",
    "obsolete-api": "obsolete", "rejected-consensus-change": "rejected",
    "same-subject-different-behavior": "rewritten", "negative-decision": "rejected",
}


def canonical(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode() + b"\n"


def fail(message: str) -> None:
    raise SafetyError(message)


def object_value(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        fail(f"{name} must be an object")
    return value


def exact(value: Any, keys: set[str], name: str) -> dict[str, Any]:
    value = object_value(value, name)
    if set(value) != keys:
        fail(f"{name} keys must be exactly {sorted(keys)}")
    return value


def text(value: Any, name: str) -> str:
    if not isinstance(value, str) or not value:
        fail(f"{name} must be a non-empty string")
    return value


def digest(value: Any, name: str) -> str:
    if not isinstance(value, str) or not SHA256.fullmatch(value):
        fail(f"{name} must be a sha256 digest")
    return value


def blob(value: Any, name: str) -> str:
    if not isinstance(value, str) or not (SHA1.fullmatch(value) or SHA256.fullmatch(value)):
        fail(f"{name} must be an immutable sha1 or sha256 hash")
    return value


def timestamp(value: Any, name: str) -> str:
    if not isinstance(value, str) or not ISO.fullmatch(value):
        fail(f"{name} must be UTC ISO-8601")
    try:
        datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
    except ValueError:
        fail(f"{name} is not a timestamp")
    return value


def strings(value: Any, name: str, allow_empty: bool = False) -> list[str]:
    if not isinstance(value, list) or (not allow_empty and not value):
        fail(f"{name} must be a non-empty list")
    if not all(isinstance(item, str) and item for item in value) or len(set(value)) != len(value):
        fail(f"{name} must contain unique non-empty strings")
    return value


def records(value: Any, name: str, key: str = "id") -> list[dict[str, Any]]:
    if not isinstance(value, list) or not value:
        fail(f"{name} must be a non-empty list")
    identifiers: set[str] = set()
    result = []
    for record in value:
        record = object_value(record, name)
        identifier = text(record.get(key), f"{name} {key}")
        if identifier in identifiers:
            fail(f"{name} has duplicate {key}")
        identifiers.add(identifier)
        result.append(record)
    return result


CHANGE_KEYS = {"id", "paths", "symbols", "targets", "behavior", "transitive", "content_checks", "rename_from", "move_from", "shared_header"}


def validate_change_shape(change: Any) -> dict[str, Any]:
    change = exact(change, CHANGE_KEYS, "change")
    text(change["id"], "change id")
    strings(change["paths"], "change paths")
    strings(change["symbols"], "change symbols", True)
    strings(change["targets"], "change targets", True)
    strings(change["behavior"], "change behavior")
    checks = exact(change["content_checks"], {"docs", "tests", "branding", "tooling"}, "content checks")
    if any(result is not True for result in checks.values()):
        fail("all applicable content checks must pass")
    for key in ("rename_from", "move_from", "shared_header"):
        if change[key] is not None:
            text(change[key], key)
    if not isinstance(change["transitive"], list):
        fail("transitive must be a list")
    return change


def zones_for(change: dict[str, Any], depth: int = 0, seen: set[str] | None = None) -> set[str]:
    if depth >= 8:
        fail("transitive graph exceeds maximum depth")
    seen = set() if seen is None else seen
    change = validate_change_shape(change)
    identifier = change["id"]
    if identifier in seen:
        fail("transitive graph has a cycle")
    seen.add(identifier)
    fields = strings(change["paths"], "change paths") + strings(change["symbols"], "change symbols", True) + strings(change["targets"], "change targets", True) + strings(change["behavior"], "change behavior") + [change[key] for key in ("rename_from", "move_from", "shared_header") if change[key] is not None]
    zones = {zone for zone, needles in CRITICAL_ZONES.items() if any(needle in field for field in fields for needle in needles)}
    for child in change["transitive"]:
        zones.update(zones_for(child, depth + 1, seen))
    seen.remove(identifier)
    return zones


def classify(value: dict[str, Any]) -> dict[str, Any]:
    exact(value, {"schema_version", "changes"}, "classification input")
    if value["schema_version"] != 1 or not isinstance(value["changes"], list) or not value["changes"]:
        fail("classification input is incomplete")
    identifiers: set[str] = set()
    classifications = []
    for change in value["changes"]:
        change = validate_change_shape(change)
        identifier = change["id"]
        if identifier in identifiers:
            fail("changes have duplicate id")
        identifiers.add(identifier)
        checks = change["content_checks"]
        zones = sorted(zones_for(change))
        if not zones and not all(path.startswith(LOW_RISK_PREFIXES) for path in strings(change["paths"], "change paths")):
            fail("unknown or unclassified input requires manual classification")
        classifications.append({"id": identifier, "zones": zones, "decision": "manual-required" if zones else "noncritical-review", "content_checks": checks})
    return {"schema_version": 1, "classifications": classifications}


def bound_evidence(value: Any, name: str, candidate: str, scoped: str, manifest: str, identity: str, finalized: str) -> None:
    keys = {"id", "candidate_digest", "scoped_tree_digest", "manifest_digest", "result_digest", "produced_at", identity}
    for record in records(value, name):
        exact(record, keys, name)
        if (record["candidate_digest"], record["scoped_tree_digest"], record["manifest_digest"]) != (candidate, scoped, manifest):
            fail(f"{name} binding drift")
        digest(record["result_digest"], f"{name} result digest")
        if timestamp(record["produced_at"], f"{name} produced at") < finalized:
            fail(f"{name} is stale")
        text(record[identity], f"{name} {identity}")


def validate_ledger(value: dict[str, Any]) -> None:
    exact(value, {"schema_version", "prior_manifest", "current_manifest", "candidate_digest", "scoped_tree_digest", "candidate_finalized_at", "entries"}, "manual ledger")
    if value["schema_version"] != 1:
        fail("manual ledger schema version is invalid")
    candidate = digest(value["candidate_digest"], "candidate digest")
    scoped = digest(value["scoped_tree_digest"], "scoped tree digest")
    finalized = timestamp(value["candidate_finalized_at"], "ledger candidate finalized at")
    for name in ("prior_manifest", "current_manifest"):
        exact(value[name], {"revision", "digest"}, name)
        if not isinstance(value[name]["revision"], int) or value[name]["revision"] < 1:
            fail(f"{name} revision must be positive")
        digest(value[name]["digest"], f"{name} digest")
    prior, current = value["prior_manifest"], value["current_manifest"]
    if current["revision"] <= prior["revision"] or current["digest"] == prior["digest"]:
        fail("ledger requires a new current manifest revision and digest")
    entry_keys = {"id", "candidate_digest", "scoped_tree_digest", "prior_manifest_revision", "prior_manifest_digest", "current_manifest_revision", "current_manifest_digest", "base_blob", "input_blobs", "conflict_hunks", "resolution_records", "rationale", "alternatives", "consensus_policy_analysis", "reviewer_evidence", "test_evidence", "artifact_hashes", "promotion_mechanism", "zones", "expert_markers", "embargoed", "public_record"}
    for entry in records(value["entries"], "ledger entries"):
        exact(entry, entry_keys, "ledger entry")
        if (entry["candidate_digest"], entry["scoped_tree_digest"]) != (candidate, scoped):
            fail("ledger candidate or scoped-tree drift")
        if (entry["prior_manifest_revision"], entry["prior_manifest_digest"], entry["current_manifest_revision"], entry["current_manifest_digest"]) != (prior["revision"], prior["digest"], current["revision"], current["digest"]):
            fail("ledger manifest binding drift")
        blob(entry["base_blob"], "ledger base blob")
        for value_blob in strings(entry["input_blobs"], "ledger input blobs"):
            blob(value_blob, "ledger input blob")
        conflict_ids: set[str] = set()
        for conflict in records(entry["conflict_hunks"], "conflict hunks"):
            exact(conflict, {"id", "base_digest", "input_digest", "range_digest"}, "conflict hunk")
            for field in ("base_digest", "input_digest"):
                blob(conflict[field], f"conflict hunk {field}")
            digest(conflict["range_digest"], "conflict hunk range digest")
            conflict_ids.add(conflict["id"])
            if conflict["base_digest"] != entry["base_blob"] or conflict["input_digest"] not in entry["input_blobs"]:
                fail("conflict hunk does not bind declared base or input blob")
        resolution_ids: set[str] = set()
        for resolution in records(entry["resolution_records"], "resolution records"):
            exact(resolution, {"id", "coverage_digest", "result_digest", "range_digest"}, "resolution record")
            for field in ("coverage_digest", "result_digest", "range_digest"):
                digest(resolution[field], f"resolution record {field}")
            resolution_ids.add(resolution["id"])
            matching = next((conflict for conflict in entry["conflict_hunks"] if conflict["id"] == resolution["id"]), None)
            if matching is not None and (resolution["range_digest"] != matching["range_digest"] or resolution["coverage_digest"] != "sha256:" + hashlib.sha256(canonical(matching)).hexdigest()):
                fail("resolution coverage does not match conflict hunk")
            if resolution["result_digest"] not in entry["artifact_hashes"]:
                fail("resolution result digest is not an entry artifact")
        if conflict_ids != resolution_ids:
            fail("conflict and resolution IDs must match exactly")
        for field in ("rationale", "consensus_policy_analysis"):
            text(entry[field], f"ledger {field}")
        strings(entry["alternatives"], "ledger alternatives")
        bound_evidence(entry["reviewer_evidence"], "ledger reviewer evidence", candidate, scoped, current["digest"], "role", finalized)
        bound_evidence(entry["test_evidence"], "ledger test evidence", candidate, scoped, current["digest"], "test_identity", finalized)
        for artifact in strings(entry["artifact_hashes"], "ledger artifact hashes"):
            digest(artifact, "ledger artifact hash")
        if entry["promotion_mechanism"] not in PROMOTIONS:
            fail("ledger promotion mechanism is invalid")
        zones = strings(entry["zones"], "ledger zones")
        if any(zone not in CRITICAL_ZONES for zone in zones) or not strings(entry["expert_markers"], "ledger expert markers"):
            fail("ledger zone or expert markers are invalid")
        if not isinstance(entry["embargoed"], bool) or not isinstance(entry["public_record"], dict):
            fail("ledger embargo record has invalid type")
        if entry["embargoed"]:
            exact(entry["public_record"], {"id", "zone", "redacted"}, "embargoed public record")
            if entry["public_record"] != {"id": entry["id"], "zone": entry["public_record"]["zone"], "redacted": True} or entry["public_record"]["zone"] not in zones:
                fail("public embargo record must contain only redacted metadata")
        elif entry["public_record"]:
            fail("non-embargoed ledger record must not carry public redaction")


def validate_evidence(value: dict[str, Any]) -> None:
    keys = {"schema_version", "candidate_digest", "scoped_tree_digest", "manifest_revision", "manifest_digest", "immutable_inputs", "expected_unit_ids", "finalized_at", "produced_at", "unit_outcomes", "approvals", "invariants", "build_test_platform_matrix", "generated_outputs", "compatibility_migration", "reproducibility", "performance_resources", "limitations", "security_review", "release_approval", "digest"}
    exact(value, keys, "candidate evidence")
    if value["schema_version"] != 1 or not isinstance(value["manifest_revision"], int) or value["manifest_revision"] < 1:
        fail("candidate evidence version or manifest revision is invalid")
    candidate, scoped, manifest = digest(value["candidate_digest"], "candidate digest"), digest(value["scoped_tree_digest"], "scoped tree digest"), digest(value["manifest_digest"], "manifest digest")
    finalized, produced = timestamp(value["finalized_at"], "finalized at"), timestamp(value["produced_at"], "produced at")
    if produced < finalized:
        fail("evidence was produced before candidate finalization")
    kinds: set[str] = set()
    for input_value in records(value["immutable_inputs"], "immutable inputs"):
        exact(input_value, {"id", "kind", "digest"}, "immutable input")
        kind = text(input_value["kind"], "immutable input kind")
        digest(input_value["digest"], "immutable input digest")
        if kind in kinds:
            fail("immutable inputs have duplicate kind")
        kinds.add(kind)
        if kind == "manifest" and input_value["digest"] != manifest:
            fail("immutable manifest input digest drift")
    if kinds != {"manifest", "materials", "plan"}:
        fail("immutable inputs must contain exactly manifest, materials, and plan")
    expected = set(strings(value["expected_unit_ids"], "expected unit ids"))
    outcome_keys = {"id", "status", "candidate_digest", "scoped_tree_digest", "manifest_digest", "fixture_digest", "artifact_digest", "finished_at", "command", "test_identity"}
    units = records(value["unit_outcomes"], "unit outcomes")
    if {unit["id"] for unit in units} != expected:
        fail("unit outcomes must exactly cover expected unit ids")
    for unit in units:
        exact(unit, outcome_keys, "unit outcome")
        if (unit["candidate_digest"], unit["scoped_tree_digest"], unit["manifest_digest"]) != (candidate, scoped, manifest) or unit["status"] != "pass" or timestamp(unit["finished_at"], "unit outcome finished at") < finalized:
            fail("unit outcome binding, status, or freshness is invalid")
        for field in ("fixture_digest", "artifact_digest"):
            digest(unit[field], f"unit outcome {field}")
        text(unit["command"], "unit outcome command")
        text(unit["test_identity"], "unit outcome test identity")
    approvals: dict[str, str] = {}
    for approval in records(value["approvals"], "approvals"):
        exact(approval, {"id", "role", "candidate_digest", "scoped_tree_digest", "manifest_digest", "evidence_digest", "produced_at"}, "approval")
        if (approval["candidate_digest"], approval["scoped_tree_digest"], approval["manifest_digest"]) != (candidate, scoped, manifest) or timestamp(approval["produced_at"], "approval produced at") < finalized:
            fail("approval binding or freshness is invalid")
        text(approval["role"], "approval role")
        approvals[approval["id"]] = digest(approval["evidence_digest"], "approval evidence digest")
    invariant_keys = outcome_keys | {"expectation_scope", "responsible_adaptation_id", "core_knots_differential_digest", "policy_block_acceptance_digest"}
    invariants = records(value["invariants"], "invariants")
    if {item["id"] for item in invariants} != REQUIRED_INVARIANTS:
        fail("evidence requires exactly seven invariant results")
    for item in invariants:
        exact(item, invariant_keys, "invariant")
        identifier = item["id"]
        if (item["candidate_digest"], item["scoped_tree_digest"], item["manifest_digest"]) != (candidate, scoped, manifest) or item["status"] != "pass" or item["expectation_scope"] != INVARIANT_SCOPES[identifier] or timestamp(item["finished_at"], "invariant finished at") < finalized:
            fail("invariant binding, scope, status, or freshness is invalid")
        for field in ("fixture_digest", "artifact_digest"):
            digest(item[field], f"invariant {field}")
        for field in ("command", "test_identity", "responsible_adaptation_id"):
            text(item[field], f"invariant {field}")
        for field, required in (("core_knots_differential_digest", identifier in DIFFERENTIAL_INVARIANTS), ("policy_block_acceptance_digest", identifier in POLICY_BLOCK_INVARIANTS)):
            if required:
                digest(item[field], f"invariant {field}")
            elif item[field] is not None:
                fail(f"invariant {field} is forbidden for this invariant")
    platform_keys = {"id", "platform", "status", "candidate_digest", "scoped_tree_digest", "manifest_digest", "artifact_digest", "limitation"}
    platforms: set[str] = set()
    for item in records(value["build_test_platform_matrix"], "platform matrix"):
        exact(item, platform_keys, "platform result")
        platform = text(item["platform"], "platform")
        if platform not in PLATFORMS or platform in platforms:
            fail("platform matrix has duplicate or unknown platform")
        platforms.add(platform)
        if (item["candidate_digest"], item["scoped_tree_digest"], item["manifest_digest"]) != (candidate, scoped, manifest):
            fail("platform binding drift")
        digest(item["artifact_digest"], "platform artifact digest")
        if item["status"] == "pass" and item["limitation"] is None:
            continue
        if item["status"] != "approved-limitation":
            fail("platform must pass or have approved limitation")
        exact(item["limitation"], {"reason", "approval_id", "approval_digest"}, "platform limitation")
        approval_id = text(item["limitation"]["approval_id"], "limitation approval id")
        if approvals.get(approval_id) != digest(item["limitation"]["approval_digest"], "limitation approval digest"):
            fail("platform limitation approval reference is not current")
        text(item["limitation"]["reason"], "limitation reason")
    if platforms != PLATFORMS:
        fail("platform matrix is incomplete")
    for name in ("generated_outputs", "compatibility_migration", "reproducibility", "limitations", "security_review"):
        for item in records(value[name], name):
            exact(item, {"id", "candidate_digest", "scoped_tree_digest", "manifest_digest", "artifact_digest", "summary", "produced_at"}, name)
            if (item["candidate_digest"], item["scoped_tree_digest"], item["manifest_digest"]) != (candidate, scoped, manifest) or timestamp(item["produced_at"], f"{name} produced at") < finalized:
                fail(f"{name} binding or freshness is invalid")
            digest(item["artifact_digest"], f"{name} artifact digest")
            text(item["summary"], f"{name} summary")
    for item in records(value["performance_resources"], "performance resources"):
        exact(item, {"id", "candidate_digest", "scoped_tree_digest", "manifest_digest", "artifact_digest", "baseline", "result", "explanation", "produced_at"}, "performance resource")
        if (item["candidate_digest"], item["scoped_tree_digest"], item["manifest_digest"]) != (candidate, scoped, manifest) or timestamp(item["produced_at"], "performance resource produced at") < finalized:
            fail("performance resource binding or freshness is invalid")
        digest(item["artifact_digest"], "performance resource artifact digest")
        for field in ("baseline", "result", "explanation"):
            text(item[field], f"performance resource {field}")
    if value["release_approval"] is not False:
        fail("release approval is a separate human decision")
    reported = digest(value["digest"], "evidence digest")
    payload = dict(value)
    payload.pop("digest")
    if reported != "sha256:" + hashlib.sha256(canonical(payload)).hexdigest():
        fail("candidate evidence enclosing digest mismatch")


def validate_lifecycle(value: dict[str, Any]) -> None:
    exact(value, {"schema_version", "candidate_digest", "manifest_digest", "proposals"}, "lifecycle proposals")
    if value["schema_version"] != 1:
        fail("lifecycle schema version is invalid")
    candidate, manifest = digest(value["candidate_digest"], "lifecycle candidate digest"), digest(value["manifest_digest"], "lifecycle manifest digest")
    keys = {"id", "adaptation_id", "scenario", "proposed_lifecycle", "patch_ids", "symbols", "tests", "release_notes", "behavior", "evidence_digest", "candidate_digest", "manifest_digest", "decision"}
    seen: set[str] = set()
    for proposal in records(value["proposals"], "lifecycle proposals"):
        exact(proposal, keys, "lifecycle proposal")
        scenario = text(proposal["scenario"], "lifecycle scenario")
        if scenario not in LIFECYCLE_POLICY or scenario in seen or proposal["proposed_lifecycle"] != LIFECYCLE_POLICY[scenario] or proposal["decision"] != "manual-required":
            fail("lifecycle scenario or proposal decision is invalid")
        seen.add(scenario)
        for field in ("adaptation_id", "behavior"):
            text(proposal[field], f"lifecycle {field}")
        for field in ("patch_ids", "symbols", "tests", "release_notes"):
            strings(proposal[field], f"lifecycle {field}")
        digest(proposal["evidence_digest"], "lifecycle evidence digest")
        if (proposal["candidate_digest"], proposal["manifest_digest"]) != (candidate, manifest):
            fail("lifecycle candidate or manifest binding drift")
    if seen != set(LIFECYCLE_POLICY):
        fail("lifecycle fixtures are incomplete")


def validate_governance(value: dict[str, Any]) -> None:
    exact(value, {"schema_version", "cases"}, "governance table")
    if value["schema_version"] != 1:
        fail("governance schema version is invalid")
    keys = {"scenario", "severity", "owner_role", "authority", "evidence", "disclosure_route", "outcome"}
    seen: set[str] = set()
    for case in records(value["cases"], "governance cases", "scenario"):
        exact(case, keys, "governance case")
        scenario = text(case["scenario"], "governance scenario")
        if scenario not in GOVERNANCE_POLICY or scenario in seen:
            fail("governance scenario is duplicate or unknown")
        seen.add(scenario)
        if (case["severity"], case["outcome"], case["owner_role"], case["authority"], case["disclosure_route"]) != GOVERNANCE_POLICY[scenario]:
            fail("governance decision table mapping is invalid")
        for field in ("owner_role", "authority", "evidence", "disclosure_route"):
            text(case[field], f"governance {field}")
    if seen != set(GOVERNANCE_POLICY):
        fail("governance table lacks required tabletops")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("classify", "validate-ledger", "validate-evidence", "validate-lifecycle", "validate-governance"))
    parser.add_argument("input", type=Path)
    arguments = parser.parse_args()
    try:
        value = json.loads(arguments.input.read_text(encoding="utf-8"))
        object_value(value, "document")
        validator = {"classify": classify, "validate-ledger": validate_ledger, "validate-evidence": validate_evidence, "validate-lifecycle": validate_lifecycle, "validate-governance": validate_governance}[arguments.command]
        result = validator(value)
        print(json.dumps(result, sort_keys=True, separators=(",", ":")) if arguments.command == "classify" else f"roots-semantic-safety: valid {arguments.command[9:]}")
    except (OSError, json.JSONDecodeError, SafetyError) as error:
        print(f"roots-semantic-safety: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
