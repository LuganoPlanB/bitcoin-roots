#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Validate a frozen Roots methodology bundle without executing candidate code."""
import hashlib
import json
from pathlib import Path
import sys
import re
import argparse


SHA256_PREFIX = "sha256:"
SHA256_RE = re.compile(r"^sha256:[0-9a-f]{64}$")
TREE_RE = re.compile(r"^sha1:[0-9a-f]{40}$")


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def fail(message):
    raise ValueError(message)


def exact_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


def validate_runtime(value):
    if not isinstance(value, dict) or value != {"python": "3.10+", "git": "git >= 2.30 with clone, checkout, write-tree, commit-tree, and format-patch", "bash": "bash >= 5", "locale": "C", "timezone": "UTC"}:
        fail("methodology runtime is invalid")


def validate_contracts(value):
    required = {"replay_tool", "replay_material_schemas", "retrospective_schema", "candidate_evidence_schema", "adaptation_manifest_schema", "delta_atlas_schema", "replay_report_schema", "risk_policy_version"}
    schemas = value.get("replay_material_schemas") if isinstance(value, dict) else None
    if not isinstance(value, dict) or set(value) != required or value["replay_tool"] != "1" or not isinstance(schemas, list) or any(not exact_int(item) for item in schemas) or schemas != [1, 2] or any(not exact_int(value[key]) or value[key] < 1 for key in required - {"replay_tool", "replay_material_schemas"}): fail("methodology contracts are invalid")


def expected_input_paths(root, governing_inputs, reconstructions, exclusions):
    paths = set(governing_inputs)
    for result in reconstructions.values():
        for relative in result["input_roots"]:
            directory = root / relative
            if not directory.is_dir() or directory.is_symlink():
                fail("methodology reconstruction directory is invalid")
            for item in directory.rglob("*"):
                if item.is_symlink() or (not item.is_dir() and not item.is_file()):
                    fail("methodology reconstruction entry is invalid")
                if item.is_file():
                    path = item.relative_to(root).as_posix()
                    if path not in exclusions:
                        paths.add(path)
    return paths


def validate_reconstructions(value):
    if not isinstance(value, dict) or not value:
        fail("methodology reconstruction roles are invalid")
    roles = set()
    for name, result in value.items():
        required = {"role", "input_roots", "record", "run_count", "target_tree", "result", "artifacts", "artifact_digests", "comparison", "comparison_test", "subsequent_manual_boundary"}
        if not isinstance(name, str) or not name or not isinstance(result, dict) or set(result) != required or not isinstance(result["role"], str) or result["role"] not in {"upstream-to-downstream", "downstream-release", "upstream-release"} or result["role"] in roles:
            fail("methodology reconstruction shape is invalid")
        roles.add(result["role"])
        if not isinstance(result["input_roots"], list) or not result["input_roots"] or any(not isinstance(item, str) or not item or item == "." or Path(item).is_absolute() or ".." in Path(item).parts for item in result["input_roots"]) or len(result["input_roots"]) != len(set(result["input_roots"])): fail("methodology reconstruction roots are invalid")
        if result["run_count"] != 2 or result["comparison"] != "two clean runs byte-identical":
            fail("methodology reconstruction run contract is invalid")
        if not isinstance(result["target_tree"], str) or not TREE_RE.fullmatch(result["target_tree"]):
            fail("methodology reconstruction tree is invalid")
        if result["artifacts"] != ["state", "report-json", "report-text", "generated-export"]:
            fail("methodology reconstruction artifacts are invalid")
        if not all(isinstance(result[key], str) and result[key] for key in ("record", "result", "comparison", "comparison_test", "subsequent_manual_boundary")) or not isinstance(result["artifact_digests"], dict) or set(result["artifact_digests"]) != {"state", "report_json", "report_text", "generated_export", "outcome_set"} or any(not isinstance(digest, str) or not SHA256_RE.fullmatch(digest) for digest in result["artifact_digests"].values()):
            fail("methodology reconstruction artifact digests are invalid")
        if result["result"] not in {"exact-tree", "exact-stage-tree", "manual-boundary"}: fail("methodology reconstruction result is invalid")
    if roles != {"upstream-to-downstream", "downstream-release", "upstream-release"}: fail("methodology reconstruction roles are incomplete")


def validate(path):
    value = json.loads(path.read_text(encoding="utf-8"))
    digest = value.pop("digest", None)
    if set(value) != {"schema_version", "methodology_version", "runtime", "contracts", "governing_inputs", "frozen_inputs", "reconstructions", "limitations", "production_exclusions"} or not exact_int(value.get("schema_version")) or value.get("schema_version") != 1 or value.get("methodology_version") != "1" or not isinstance(digest, str) or digest != SHA256_PREFIX + hashlib.sha256(canonical(value)).hexdigest():
        fail("methodology bundle digest is invalid")
    validate_runtime(value["runtime"])
    validate_contracts(value["contracts"])
    validate_reconstructions(value["reconstructions"])
    exclusions = value.get("production_exclusions")
    upstream_roots = [record["input_roots"] for record in value["reconstructions"].values() if record["role"] == "upstream-release"]
    if len(upstream_roots) != 1 or len(upstream_roots[0]) != 1:
        fail("methodology production exclusion root is invalid")
    upstream_root = upstream_roots[0][0]
    if exclusions != [
        {"path": f"{upstream_root}/incremental-oracle.bash", "rationale": "requires a rejected non-ancestral Roots 29.3 comparison and is excluded from production reconstruction"},
        {"path": f"{upstream_root}/post-candidate-production-overlay.patch", "rationale": "is a later control-plane overlay bound by post-candidate replay evidence"},
    ]:
        fail("methodology production exclusions are invalid")
    excluded_paths = {item["path"] for item in exclusions}
    inputs = value.get("frozen_inputs")
    if not isinstance(inputs, list) or not inputs or inputs != sorted(inputs, key=lambda item: item.get("path", "") if isinstance(item, dict) else ""):
        fail("methodology frozen inputs are invalid")
    root = path.parents[2]
    seen = set()
    for item in inputs:
        if not isinstance(item, dict) or set(item) != {"path", "sha256"} or not isinstance(item["path"], str) or not isinstance(item["sha256"], str) or not SHA256_RE.fullmatch(item["sha256"]):
            fail("methodology frozen input shape is invalid")
        relative = Path(item["path"])
        if relative.is_absolute() or ".." in relative.parts or not item["path"] or item["path"] in seen:
            fail("methodology frozen input path is invalid")
        seen.add(item["path"])
        candidate = root / item["path"]
        if not candidate.is_file() or candidate.is_symlink() or "sha256:" + hashlib.sha256(candidate.read_bytes()).hexdigest() != item["sha256"]:
            fail("methodology frozen input drift")
    governing = value["governing_inputs"]
    if not isinstance(governing, list) or any(not isinstance(item, str) or not item or item == "." or Path(item).is_absolute() or ".." in Path(item).parts for item in governing) or len(governing) != len(set(governing)) or seen != expected_input_paths(root, governing, value["reconstructions"], excluded_paths):
        fail("methodology frozen input closure is incomplete")
    for result in value["reconstructions"].values():
        record = result["record"]
        if not isinstance(record, str) or not record or record == "." or Path(record).is_absolute() or ".." in Path(record).parts or record not in seen or not any(record.startswith(item + "/") for item in result["input_roots"]):
            fail("methodology reconstruction record is invalid")
    if not isinstance(value["limitations"], list) or not value["limitations"] or any(not isinstance(item, str) or not item for item in value["limitations"]) or len(value["limitations"]) != len(set(value["limitations"])):
        fail("methodology limitations are invalid")
    return value


if __name__ == "__main__":
    try:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("bundle", type=Path)
        validate(parser.parse_args().bundle)
    except (IndexError, OSError, UnicodeError, json.JSONDecodeError, ValueError) as error:
        print(f"roots-methodology: {error}", file=sys.stderr)
        raise SystemExit(1)
