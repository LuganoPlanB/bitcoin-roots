#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Validate deterministic Bitcoin Roots logical-adaptation manifests.

The JSON Schema is deliberately a small interchange envelope. This validator
enforces cross-field invariants which JSON Schema cannot express: stable unit
identities, dependency graph safety, provenance and licence evidence, and
mechanism/lifecycle compatibility. It never fetches, checks out, or runs Git.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any


MAX_JSON_BYTES = 4_000_000
MAX_DEPTH = 32
ID_RE = re.compile(r"^roots-[a-z0-9]+(?:-[a-z0-9]+)*$")
DIGEST_RE = re.compile(r"^sha256:[0-9a-f]{64}$")
PATCH_ID_RE = re.compile(r"^patch-id:sha1:[0-9a-f]{40}$")
COMMIT_RE = re.compile(r"^sha1:[0-9a-f]{40}$")

OWNER_AREAS = frozenset({"build", "compatibility", "documentation", "gui", "identity", "network", "policy", "tests", "tooling", "wallet"})
LAYERS = frozenset({"knots-inherited", "roots-owned"})
RISK_TIERS = frozenset({"low", "medium", "high", "critical"})
EFFECTS = frozenset({"consensus-neutral", "policy-only", "consensus-sensitive", "unknown"})
MECHANISMS = frozenset({"commit", "patch", "module/data", "generator", "manual"})
LIFECYCLE = frozenset({"active", "absorbed", "obsolete", "rejected"})
PROJECTS = frozenset({"bitcoin-core", "bitcoin-knots", "bitcoin-roots"})
PHASES = ("foundation", "module", "behavior", "ui-branding", "tests", "generated", "release-metadata")


class ManifestError(ValueError):
    pass


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ManifestError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _depth(value: Any, current: int = 0) -> int:
    if not isinstance(value, (dict, list)):
        return current
    return max([current] + [_depth(item, current + 1) for item in (value.values() if isinstance(value, dict) else value)])


def read_manifest(path: Path) -> dict[str, Any]:
    try:
        if path.stat().st_size > MAX_JSON_BYTES:
            raise ManifestError("JSON input exceeds byte limit")
        value = json.loads(path.read_text(encoding="utf-8", errors="strict"), object_pairs_hook=_unique_object)
    except ManifestError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ManifestError("cannot read JSON input") from error
    if not isinstance(value, dict) or _depth(value) > MAX_DEPTH:
        raise ManifestError("manifest must be a bounded JSON object")
    return value


def canonical_json(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def digest(value: Any) -> str:
    return "sha256:" + hashlib.sha256(canonical_json(value)).hexdigest()


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")


def _expect_object(value: Any, field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ManifestError(f"{field} must be an object")
    return value


def _expect_strings(value: Any, field: str, *, nonempty: bool = False) -> list[str]:
    if not isinstance(value, list) or (nonempty and not value) or any(not isinstance(item, str) or not item for item in value):
        raise ManifestError(f"{field} must be a{' non-empty' if nonempty else ''} string array")
    if len(value) != len(set(value)) or value != sorted(value):
        raise ManifestError(f"{field} must be unique and sorted")
    return value


def _expect_keys(value: dict[str, Any], field: str, required: set[str], optional: set[str] = frozenset()) -> None:
    missing, extra = required - value.keys(), value.keys() - required - optional
    if missing or extra:
        raise ManifestError(f"{field} has invalid keys")


def _validate_provenance(value: Any, unit_id: str) -> None:
    entries = value if isinstance(value, list) else None
    if not entries:
        raise ManifestError(f"{unit_id}: provenance must be non-empty")
    for index, entry in enumerate(entries):
        entry = _expect_object(entry, f"{unit_id}: provenance[{index}]")
        _expect_keys(entry, f"{unit_id}: provenance[{index}]", {"project", "commit", "confidence"})
        if entry["project"] not in PROJECTS or not isinstance(entry["commit"], str) or not COMMIT_RE.fullmatch(entry["commit"]):
            raise ManifestError(f"{unit_id}: invalid provenance identity")
        if entry["confidence"] not in {"verified", "reviewed", "inferred"}:
            raise ManifestError(f"{unit_id}: invalid provenance confidence")


def _validate_unit(unit: Any) -> dict[str, Any]:
    unit = _expect_object(unit, "unit")
    required = {"id", "owner_area", "purpose", "provenance", "license", "original_patch_ids", "current_patch_ids", "layer", "dependencies", "conflicts", "provides", "conditions", "phase", "touched", "generated", "risk_tier", "consensus_policy_effect", "supported_release_range", "application", "required_tests", "upstream_disposition", "retirement_conditions", "lifecycle"}
    _expect_keys(unit, "unit", required)
    unit_id = unit.get("id")
    if not isinstance(unit_id, str) or not ID_RE.fullmatch(unit_id):
        raise ManifestError("unit id must be a stable roots-* identifier")
    if unit["owner_area"] not in OWNER_AREAS or not isinstance(unit["purpose"], str) or not unit["purpose"].strip():
        raise ManifestError(f"{unit_id}: owner area or purpose is invalid")
    _validate_provenance(unit["provenance"], unit_id)
    license_info = _expect_object(unit["license"], f"{unit_id}: license")
    _expect_keys(license_info, f"{unit_id}: license", {"compatible", "evidence"})
    if license_info["compatible"] is not True or not isinstance(license_info["evidence"], str) or not license_info["evidence"].strip():
        raise ManifestError(f"{unit_id}: license compatibility needs evidence")
    for field in ("original_patch_ids", "current_patch_ids"):
        for patch_id in _expect_strings(unit[field], f"{unit_id}: {field}"):
            if not PATCH_ID_RE.fullmatch(patch_id):
                raise ManifestError(f"{unit_id}: invalid {field}")
    if unit["layer"] not in LAYERS or unit["risk_tier"] not in RISK_TIERS or unit["consensus_policy_effect"] not in EFFECTS:
        raise ManifestError(f"{unit_id}: invalid layer, risk tier, or consensus/policy effect")
    _expect_strings(unit["dependencies"], f"{unit_id}: dependencies")
    _expect_strings(unit["conflicts"], f"{unit_id}: conflicts")
    _expect_strings(unit["provides"], f"{unit_id}: provides")
    conditions = _expect_object(unit["conditions"], f"{unit_id}: conditions")
    _expect_keys(conditions, f"{unit_id}: conditions", {"release_tags", "replaces"})
    _expect_strings(conditions["release_tags"], f"{unit_id}: conditions.release_tags")
    _expect_strings(conditions["replaces"], f"{unit_id}: conditions.replaces")
    if unit["phase"] not in PHASES:
        raise ManifestError(f"{unit_id}: invalid application phase")
    touched = _expect_object(unit["touched"], f"{unit_id}: touched")
    _expect_keys(touched, f"{unit_id}: touched", {"targets", "paths"})
    for field in ("targets", "paths"):
        _expect_strings(touched[field], f"{unit_id}: touched.{field}")
    generated = _expect_object(unit["generated"], f"{unit_id}: generated")
    _expect_keys(generated, f"{unit_id}: generated", {"inputs", "outputs"})
    for field in ("inputs", "outputs"):
        _expect_strings(generated[field], f"{unit_id}: generated.{field}")
    release_range = _expect_object(unit["supported_release_range"], f"{unit_id}: supported_release_range")
    _expect_keys(release_range, f"{unit_id}: supported_release_range", {"from", "through"})
    if any(not isinstance(release_range[key], str) or not release_range[key] for key in ("from", "through")):
        raise ManifestError(f"{unit_id}: invalid supported release range")
    application = _expect_object(unit["application"], f"{unit_id}: application")
    _expect_keys(application, f"{unit_id}: application", {"mechanism", "reference"})
    if application["mechanism"] not in MECHANISMS or not isinstance(application["reference"], str) or not application["reference"].strip():
        raise ManifestError(f"{unit_id}: invalid application mechanism")
    _expect_strings(unit["required_tests"], f"{unit_id}: required_tests", nonempty=True)
    disposition = _expect_object(unit["upstream_disposition"], f"{unit_id}: upstream_disposition")
    _expect_keys(disposition, f"{unit_id}: upstream_disposition", {"status", "evidence"})
    if disposition["status"] not in {"roots-only", "candidate", "submitted", "absorbed", "not-applicable"} or not isinstance(disposition["evidence"], str) or not disposition["evidence"].strip():
        raise ManifestError(f"{unit_id}: invalid upstream disposition")
    if not isinstance(unit["retirement_conditions"], str) or not unit["retirement_conditions"].strip() or unit["lifecycle"] not in LIFECYCLE:
        raise ManifestError(f"{unit_id}: lifecycle needs retirement conditions")
    if unit["lifecycle"] in {"absorbed", "obsolete", "rejected"} and application["mechanism"] not in MECHANISMS:
        raise ManifestError(f"{unit_id}: invalid terminal lifecycle")
    return unit


def _validate_graph(units: list[dict[str, Any]]) -> None:
    identifiers = [unit["id"] for unit in units]
    if len(identifiers) != len(set(identifiers)):
        raise ManifestError("unit IDs must be unique")
    known = set(identifiers)
    graph: dict[str, list[str]] = {}
    for unit in units:
        dependencies, conflicts = unit["dependencies"], unit["conflicts"]
        replacements = unit["conditions"]["replaces"]
        if any(item not in known or item == unit["id"] for item in dependencies + conflicts + replacements):
            raise ManifestError(f"{unit['id']}: unknown or self reference")
        if set(dependencies) & set(conflicts) or set(replacements) & set(dependencies):
            raise ManifestError(f"{unit['id']}: dependency cannot conflict")
        graph[unit["id"]] = dependencies
    visiting, complete = set(), set()
    def visit(unit_id: str) -> None:
        if unit_id in visiting:
            raise ManifestError("dependency cycle")
        if unit_id in complete:
            return
        visiting.add(unit_id)
        for dependency in graph[unit_id]:
            visit(dependency)
        visiting.remove(unit_id)
        complete.add(unit_id)
    for unit_id in identifiers:
        visit(unit_id)


def application_order(value: dict[str, Any], release_tag: str | None = None, selected_ids: list[str] | None = None) -> list[str]:
    """Return a stable topological order or fail before a candidate is touched."""
    validate_manifest(value)
    units = {unit["id"]: unit for unit in value["units"]}
    requested = set(selected_ids) if selected_ids is not None else set(units)
    if requested - set(units):
        raise ManifestError("selected adaptation is unknown")
    selected = {
        unit_id for unit_id in requested
        if units[unit_id]["lifecycle"] == "active" and (release_tag is None or not units[unit_id]["conditions"]["release_tags"] or release_tag in units[unit_id]["conditions"]["release_tags"])
    }
    for unit_id in sorted(selected):
        selected -= set(units[unit_id]["conditions"]["replaces"])
    for unit_id in selected:
        unit = units[unit_id]
        if set(unit["dependencies"]) - selected:
            raise ManifestError(f"{unit_id}: selected dependency is unavailable")
        if set(unit["conflicts"]) & selected:
            raise ManifestError(f"{unit_id}: mutually exclusive adaptations selected")
    providers: dict[str, str] = {}
    for unit_id in selected:
        for capability in units[unit_id]["provides"]:
            if capability in providers:
                raise ManifestError(f"duplicate provider: {capability}")
            providers[capability] = unit_id
    indegree = {unit_id: 0 for unit_id in selected}
    successors = {unit_id: [] for unit_id in selected}
    for unit_id in selected:
        for dependency in units[unit_id]["dependencies"]:
            successors[dependency].append(unit_id)
            indegree[unit_id] += 1
    phase_index = {phase: index for index, phase in enumerate(PHASES)}
    ready = sorted((unit_id for unit_id, degree in indegree.items() if degree == 0), key=lambda item: (phase_index[units[item]["phase"]], item))
    ordered = []
    while ready:
        unit_id = ready.pop(0)
        ordered.append(unit_id)
        for successor in sorted(successors[unit_id]):
            indegree[successor] -= 1
            if indegree[successor] == 0:
                ready.append(successor)
        ready.sort(key=lambda item: (phase_index[units[item]["phase"]], item))
    if len(ordered) != len(selected):
        raise ManifestError("dependency cycle")
    return ordered


def validate_manifest(value: dict[str, Any]) -> None:
    _expect_keys(value, "manifest", {"schema_version", "atlas_report_digest", "units"})
    if value["schema_version"] != 1 or not isinstance(value["atlas_report_digest"], str) or not DIGEST_RE.fullmatch(value["atlas_report_digest"]):
        raise ManifestError("manifest schema version or atlas report digest is invalid")
    if not isinstance(value["units"], list) or not value["units"]:
        raise ManifestError("manifest must contain at least one unit")
    units = [_validate_unit(unit) for unit in value["units"]]
    _validate_graph(units)


LAYER_DISPOSITIONS = {
    "core_to_knots": ("selected-knots-capability", "core-or-knots-origin-unresolved", "bitcoin-knots"),
    "knots_to_first_roots": ("roots-first-fork-change", "bitcoin-roots", "bitcoin-roots"),
    "first_roots_to_target": ("roots-later-change", "bitcoin-roots", "bitcoin-roots"),
}


def _classification_index(classification: dict[str, Any]) -> dict[tuple[str, str], dict[str, Any]]:
    records = classification.get("records")
    if classification.get("schema_version") != 1 or not isinstance(records, list):
        raise ManifestError("unsupported atlas classification")
    indexed: dict[tuple[str, str], dict[str, Any]] = {}
    for record in records:
        if not isinstance(record, dict) or not isinstance(record.get("layer"), str) or not isinstance(record.get("path"), str) or not isinstance(record.get("classification"), dict):
            raise ManifestError("malformed atlas classification record")
        key = (record["layer"], record["path"])
        if key in indexed:
            raise ManifestError("duplicate atlas classification record")
        indexed[key] = record["classification"]
    return indexed


def classify_layers(ledger: dict[str, Any], partition: dict[str, Any], report: dict[str, Any], classification: dict[str, Any]) -> dict[str, Any]:
    """Bind every L2 record to a truthful three-project provenance position.

    The Core-to-Knots comparison proves a selected Knots capability delta, but
    cannot by itself prove whether an individual Knots change first originated
    in Core. Such entries remain explicitly unresolved rather than guessed.
    """
    if report.get("schema_version") != 1 or partition.get("schema_version") != 1:
        raise ManifestError("unsupported atlas report or partition")
    if report.get("input_digests", {}).get("classification") != digest(classification):
        raise ManifestError("stale atlas classification")
    if partition.get("unexplained_direct_paths"):
        raise ManifestError("atlas partition has unexplained direct paths")
    forks = ledger.get("fork_starts")
    if not isinstance(forks, list) or len(forks) != 1 or not isinstance(forks[0], dict) or forks[0].get("path_count") != 40:
        raise ManifestError("lineage ledger lacks the verified 40-path first-fork boundary")
    classification_index = _classification_index(classification)
    layers = partition.get("layers")
    if not isinstance(layers, list) or [layer.get("id") for layer in layers] != list(LAYER_DISPOSITIONS):
        raise ManifestError("unexpected atlas layer ordering")
    output_layers = []
    all_keys: set[tuple[str, str]] = set()
    for layer in layers:
        layer_id = layer["id"]
        records = layer.get("records")
        if not isinstance(records, list) or layer.get("record_count") != len(records):
            raise ManifestError("malformed atlas layer")
        disposition, origin, source_project = LAYER_DISPOSITIONS[layer_id]
        output_records = []
        for raw in records:
            path = raw.get("path") if isinstance(raw, dict) else None
            key = (layer_id, path)
            if not isinstance(path, str) or key not in classification_index or key in all_keys:
                raise ManifestError("atlas layer/classification coverage mismatch")
            all_keys.add(key)
            output_records.append({
                "applied": True,
                "atlas_classification": classification_index[key],
                "knots_origin": origin,
                "layer_disposition": disposition,
                "path": path,
                "source_project": source_project,
            })
        output_layers.append({"id": layer_id, "record_count": len(output_records), "records": output_records})
    if all_keys != set(classification_index):
        raise ManifestError("atlas classification has unlayered records")
    return {
        "schema_version": 1,
        "input_digests": {"atlas_report": digest(report), "classification": digest(classification), "partition": digest(partition)},
        "layers": output_layers,
        "first_fork_boundary": {"path_count": 40, "evidence_digest": forks[0].get("evidence_digest")},
        "handoff": "L3.3 groups these path records into dependency-ordered logical adaptation units. Unresolved Core-or-Knots origins require provenance review before application.",
    }


def validate_layer_classification(value: dict[str, Any]) -> None:
    if set(value) != {"schema_version", "input_digests", "layers", "first_fork_boundary", "handoff"} or value.get("schema_version") != 1:
        raise ManifestError("invalid layer classification envelope")
    if not isinstance(value["input_digests"], dict) or set(value["input_digests"]) != {"atlas_report", "classification", "partition"}:
        raise ManifestError("invalid layer classification inputs")
    if any(not isinstance(item, str) or not DIGEST_RE.fullmatch(item) for item in value["input_digests"].values()):
        raise ManifestError("invalid layer classification digest")
    if not isinstance(value["layers"], list) or [layer.get("id") for layer in value["layers"]] != list(LAYER_DISPOSITIONS):
        raise ManifestError("invalid layer classification ordering")
    seen = set()
    for layer in value["layers"]:
        records = layer.get("records")
        if not isinstance(records, list) or layer.get("record_count") != len(records):
            raise ManifestError("invalid layer classification records")
        for record in records:
            if not isinstance(record, dict) or set(record) != {"applied", "atlas_classification", "knots_origin", "layer_disposition", "path", "source_project"} or record["applied"] is not True:
                raise ManifestError("invalid layer classification record")
            key = (layer["id"], record["path"])
            if not isinstance(record["path"], str) or key in seen:
                raise ManifestError("duplicate layer classification path")
            seen.add(key)
            expected = LAYER_DISPOSITIONS[layer["id"]]
            if (record["layer_disposition"], record["knots_origin"], record["source_project"]) != expected:
                raise ManifestError("invalid layer provenance position")
    if value["first_fork_boundary"].get("path_count") != 40:
        raise ManifestError("invalid first-fork boundary")


def _git(repository: Path, *args: str) -> str:
    completed = subprocess.run(["git", "-C", str(repository), *args], check=False, capture_output=True, text=True,
                               env={"LC_ALL": "C", "LANG": "C", "PATH": __import__("os").environ.get("PATH", "")})
    if completed.returncode:
        raise ManifestError(f"git {args[0]} failed")
    return completed.stdout.strip()


def _git_bytes(repository: Path, *args: str) -> bytes:
    completed = subprocess.run(["git", "-C", str(repository), *args], check=False, capture_output=True,
                               env={"LC_ALL": "C", "LANG": "C", "PATH": __import__("os").environ.get("PATH", "")})
    if completed.returncode:
        raise ManifestError(f"git {args[0]} failed")
    return completed.stdout


def _commit(repository: Path, revision: str) -> str:
    value = _git(repository, "rev-parse", "--verify", f"{revision.removeprefix('sha1:')}^{{commit}}")
    if not re.fullmatch(r"[0-9a-f]{40}", value):
        raise ManifestError("Git did not resolve a full commit")
    return "sha1:" + value


def validate_topology_record(value: dict[str, Any]) -> None:
    required = {"schema_version", "base_tag", "base_commit", "target_commit", "target_tree", "aggregate_diff_digest", "manifest_owned_diff_digest", "commit_map"}
    _expect_keys(value, "topology", required)
    if value["schema_version"] != 1 or not isinstance(value["base_tag"], str) or not value["base_tag"] or any(not isinstance(value[key], str) or not COMMIT_RE.fullmatch(value[key]) for key in ("base_commit", "target_commit")) or not isinstance(value["target_tree"], str) or not COMMIT_RE.fullmatch(value["target_tree"]) or any(not isinstance(value[key], str) or not DIGEST_RE.fullmatch(value[key]) for key in ("aggregate_diff_digest", "manifest_owned_diff_digest")):
        raise ManifestError("invalid topology identity")
    entries = value["commit_map"]
    if not isinstance(entries, list):
        raise ManifestError("topology commit map must be an array")
    seen = set()
    for entry in entries:
        entry = _expect_object(entry, "topology commit map entry")
        _expect_keys(entry, "topology commit map entry", {"commit", "adaptation_ids", "provenance"})
        if not isinstance(entry["commit"], str) or not COMMIT_RE.fullmatch(entry["commit"]) or entry["commit"] in seen:
            raise ManifestError("invalid topology commit map identity")
        seen.add(entry["commit"])
        adaptation_ids = _expect_strings(entry["adaptation_ids"], "topology adaptation_ids", nonempty=True)
        if any(not ID_RE.fullmatch(item) for item in adaptation_ids):
            raise ManifestError("invalid topology adaptation ID")
        _validate_provenance(entry["provenance"], "topology commit map")


def verify_topology(repository: Path, topology: dict[str, Any], manifest: dict[str, Any] | None = None) -> None:
    """Prove a candidate has only the declared Roots-owned commits after Core."""
    validate_topology_record(topology)
    if manifest is not None:
        validate_manifest(manifest)
        known_adaptations = {unit["id"] for unit in manifest["units"]}
    else:
        known_adaptations = None
    tag_type = _git(repository, "cat-file", "-t", topology["base_tag"])
    if tag_type != "tag":
        raise ManifestError("base must be an annotated tag")
    base, target = _commit(repository, topology["base_tag"]), _commit(repository, topology["target_commit"])
    if base != topology["base_commit"] or target != topology["target_commit"]:
        raise ManifestError("topology tag or target identity mismatch")
    base_git, target_git = base.removeprefix("sha1:"), target.removeprefix("sha1:")
    if _commit(repository, _git(repository, "merge-base", base_git, target_git)) != base:
        raise ManifestError("base is not the exact merge-base")
    commits = ["sha1:" + item for item in _git(repository, "rev-list", "--reverse", f"{base_git}..{target_git}").splitlines() if item]
    if not commits:
        raise ManifestError("topology has no Roots-owned commits")
    first_parent = _git(repository, "show", "-s", "--format=%P", commits[0].removeprefix("sha1:")).split()
    if first_parent != [base_git]:
        raise ManifestError("first Roots commit must directly parent the Core tag")
    for commit in commits:
        parents = _git(repository, "show", "-s", "--format=%P", commit.removeprefix("sha1:")).split()
        if len(parents) != 1:
            raise ManifestError("topology contains a merge commit")
    mapped = [entry["commit"] for entry in topology["commit_map"]]
    if mapped != commits:
        raise ManifestError("topology commit map is not complete first-parent order")
    if known_adaptations is not None and any(adaptation not in known_adaptations for entry in topology["commit_map"] for adaptation in entry["adaptation_ids"]):
        raise ManifestError("topology maps a commit to an unknown adaptation")
    actual_tree = "sha1:" + _git(repository, "rev-parse", f"{target_git}^{{tree}}")
    if actual_tree != topology["target_tree"]:
        raise ManifestError("topology target tree mismatch")
    actual_diff = "sha256:" + hashlib.sha256(_git_bytes(repository, "diff", "--binary", "--full-index", "--no-ext-diff", base_git, target_git)).hexdigest()
    if topology["aggregate_diff_digest"] != actual_diff or topology["manifest_owned_diff_digest"] != actual_diff:
        raise ManifestError("topology aggregate and manifest-owned diffs must equal the final tree diff")


def _owner_id(layer: str, classification: dict[str, Any]) -> str:
    prefix = "knots" if layer == "core_to_knots" else "roots"
    area = classification.get("area")
    purpose = classification.get("primary_purpose")
    if not isinstance(area, str) or not isinstance(purpose, str) or not re.fullmatch(r"[a-z0-9-]+", area) or not re.fullmatch(r"[a-z0-9-]+", purpose):
        raise ManifestError("invalid atlas classification owner")
    return f"roots-{prefix}-{area}-{purpose}"


def build_coverage(partition: dict[str, Any], classification: dict[str, Any]) -> dict[str, Any]:
    """Map each direct raw-entry delta to exactly one deterministic owner.

    These owner IDs are coverage boundaries that L3's completed logical
    manifest must refine, not permission to hide unreviewed semantics in a
    catch-all replay commit.
    """
    if partition.get("schema_version") != 1 or partition.get("unexplained_direct_paths"):
        raise ManifestError("partition cannot support complete coverage")
    index = _classification_index(classification)
    direct = partition.get("direct_core_to_roots", {}).get("records")
    if not isinstance(direct, list):
        raise ManifestError("partition lacks direct Core-to-Roots records")
    layers_by_path: dict[str, list[tuple[str, dict[str, Any]]]] = {}
    for (layer, path), entry in index.items():
        layers_by_path.setdefault(path, []).append((layer, entry))
    records = []
    for delta in sorted(direct, key=lambda item: item.get("path", "")):
        if not isinstance(delta, dict) or not isinstance(delta.get("path"), str):
            raise ManifestError("malformed direct delta")
        candidates = layers_by_path.get(delta["path"], [])
        if not candidates:
            raise ManifestError("direct delta lacks a classified layer")
        candidates.sort(key=lambda item: ({"core_to_knots": 0, "knots_to_first_roots": 1, "first_roots_to_target": 2}[item[0]], item[0]))
        primary_layer, primary_classification = candidates[-1]
        primary = _owner_id(primary_layer, primary_classification)
        co_owned = sorted({_owner_id(layer, entry) for layer, entry in candidates[:-1]} - {primary})
        records.append({"co_owned_adaptations": co_owned, "delta": delta, "primary_adaptation": primary})
    paths = [record["delta"]["path"] for record in records]
    if len(paths) != len(set(paths)):
        raise ManifestError("duplicate direct delta path")
    target_commit = partition.get("inputs", {}).get("roots_target")
    if not isinstance(target_commit, str) or not re.fullmatch(r"[0-9a-f]{40}", target_commit):
        raise ManifestError("partition lacks Roots target commit")
    return {
        "schema_version": 1,
        "input_digests": {"classification": digest(classification), "partition": digest(partition)},
        "target_commit": "sha1:" + target_commit,
        "target_scoped_tree_digest": digest([record["delta"] for record in records]),
        "record_count": len(records),
        "records": records,
    }


def validate_coverage(value: dict[str, Any], partition: dict[str, Any] | None = None, classification: dict[str, Any] | None = None) -> None:
    _expect_keys(value, "coverage", {"schema_version", "input_digests", "target_commit", "target_scoped_tree_digest", "record_count", "records"})
    if value["schema_version"] != 1 or not isinstance(value["input_digests"], dict) or set(value["input_digests"]) != {"classification", "partition"} or any(not isinstance(item, str) or not DIGEST_RE.fullmatch(item) for item in value["input_digests"].values()) or not isinstance(value["target_commit"], str) or not COMMIT_RE.fullmatch(value["target_commit"]) or not isinstance(value["target_scoped_tree_digest"], str) or not DIGEST_RE.fullmatch(value["target_scoped_tree_digest"]) or not isinstance(value["records"], list) or value["record_count"] != len(value["records"]):
        raise ManifestError("invalid coverage envelope")
    paths = []
    for record in value["records"]:
        record = _expect_object(record, "coverage record")
        _expect_keys(record, "coverage record", {"co_owned_adaptations", "delta", "primary_adaptation"})
        if not isinstance(record["primary_adaptation"], str) or not ID_RE.fullmatch(record["primary_adaptation"]):
            raise ManifestError("coverage record lacks a primary adaptation")
        co_owned = _expect_strings(record["co_owned_adaptations"], "coverage co-owners")
        if record["primary_adaptation"] in co_owned or any(not ID_RE.fullmatch(item) for item in co_owned):
            raise ManifestError("invalid coverage co-owner")
        delta = _expect_object(record["delta"], "coverage delta")
        if not isinstance(delta.get("path"), str) or not isinstance(delta.get("status"), str) or set(delta) - {"path", "status", "before", "after"}:
            raise ManifestError("invalid coverage delta")
        paths.append(delta["path"])
    if paths != sorted(paths) or len(paths) != len(set(paths)) or value["target_scoped_tree_digest"] != digest([record["delta"] for record in value["records"]]):
        raise ManifestError("coverage records are incomplete or non-deterministic")
    if partition is not None or classification is not None:
        if partition is None or classification is None:
            raise ManifestError("coverage verification requires both atlas inputs")
        expected = build_coverage(partition, classification)
        if value != expected:
            raise ManifestError("stale or incomplete coverage report")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    if len(sys.argv) > 1 and sys.argv[1] == "classify-layers":
        parser.add_argument("command")
        parser.add_argument("--ledger", required=True, type=Path)
        parser.add_argument("--partition", required=True, type=Path)
        parser.add_argument("--report", required=True, type=Path)
        parser.add_argument("--classification", required=True, type=Path)
        parser.add_argument("--output", required=True, type=Path)
    elif len(sys.argv) > 1 and sys.argv[1] == "verify-topology":
        parser.add_argument("command")
        parser.add_argument("topology", type=Path)
        parser.add_argument("--repository", required=True, type=Path)
        parser.add_argument("--manifest", type=Path)
    elif len(sys.argv) > 1 and sys.argv[1] in {"build-coverage", "verify-coverage"}:
        parser.add_argument("command")
        parser.add_argument("coverage", nargs="?", type=Path)
        parser.add_argument("--partition", required=True, type=Path)
        parser.add_argument("--classification", required=True, type=Path)
        parser.add_argument("--output", type=Path)
    else:
        parser.add_argument("manifest", type=Path)
    arguments = parser.parse_args()
    try:
        if getattr(arguments, "command", None) == "classify-layers":
            value = classify_layers(read_manifest(arguments.ledger), read_manifest(arguments.partition), read_manifest(arguments.report), read_manifest(arguments.classification))
            validate_layer_classification(value)
            write_json(arguments.output, value)
        elif getattr(arguments, "command", None) == "verify-topology":
            verify_topology(arguments.repository, read_manifest(arguments.topology), read_manifest(arguments.manifest) if arguments.manifest else None)
        elif getattr(arguments, "command", None) == "build-coverage":
            if arguments.output is None or arguments.coverage is not None:
                parser.error("build-coverage requires --output and no coverage path")
            write_json(arguments.output, build_coverage(read_manifest(arguments.partition), read_manifest(arguments.classification)))
        elif getattr(arguments, "command", None) == "verify-coverage":
            if arguments.coverage is None or arguments.output is not None:
                parser.error("verify-coverage requires a coverage path")
            validate_coverage(read_manifest(arguments.coverage), read_manifest(arguments.partition), read_manifest(arguments.classification))
        else:
            validate_manifest(read_manifest(arguments.manifest))
    except ManifestError as error:
        print(f"roots-adaptation-manifest: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
