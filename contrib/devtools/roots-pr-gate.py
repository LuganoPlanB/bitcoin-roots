#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Validate candidate data against the trusted continuous-accounting contract."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
from pathlib import PurePosixPath
import re
import sys
from typing import Any


MAX_CHANGED_FILES = 2_000
MAX_REPORT_BYTES = 65_536
SHA1_RE = re.compile(r"^[0-9a-f]{40}$")
ID_RE = re.compile(r"^roots-[a-z0-9]+(?:-[a-z0-9]+)*$")
ACCOUNTING_RECORD = "contrib/roots/continuous-accounting-pr.json"
POST_METHODOLOGY_REGISTRY = "contrib/roots/post-methodology-adaptations.json"
REGISTRY_PATH = "contrib/roots/post-methodology-adaptations.json"


class GateError(ValueError):
    pass


def load_validator(path: Path, module_name: str) -> Any:
    specification = importlib.util.spec_from_file_location(module_name, path)
    if specification is None or specification.loader is None:
        raise GateError("cannot load adaptation-manifest validator")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def canonical(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def validate_revision(value: str, field: str) -> None:
    if not SHA1_RE.fullmatch(value):
        raise GateError(f"{field} must be a full lowercase SHA-1")


def trusted_tool(tools_directory: Path, name: str) -> Path:
    path = tools_directory / name
    if not path.is_file() or path.is_symlink():
        raise GateError("trusted validator is unavailable")
    return path


def read_record(path: Path) -> dict[str, Any]:
    if not path.is_file() or path.is_symlink():
        raise GateError("candidate accounting record is unavailable")
    try:
        return json.loads(path.read_text(encoding="utf-8", errors="strict"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise GateError("candidate accounting record is invalid") from error


def read_json(path: Path, message: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8", errors="strict"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise GateError(message) from error
    if not isinstance(value, dict):
        raise GateError(message)
    return value


def candidate_file(candidate_root: Path, path: Path, message: str) -> Path:
    """Return a regular candidate file only if no path component is a link."""
    root = candidate_root.absolute()
    candidate = path.absolute()
    try:
        relative = candidate.relative_to(root)
    except ValueError as error:
        raise GateError(message) from error
    if not relative.parts or any(part in {"", ".", ".."} for part in relative.parts):
        raise GateError(message)
    current = root
    for component in relative.parts:
        current /= component
        if current.is_symlink():
            raise GateError(message)
    if not candidate.is_file():
        raise GateError(message)
    return candidate


def validate_methodology_inputs(candidate_root: Path, methodology_path: Path) -> None:
    value = read_json(methodology_path, "candidate methodology is invalid")
    inputs = value.get("frozen_inputs")
    if not isinstance(inputs, list):
        raise GateError("candidate methodology is invalid")
    for item in inputs:
        if not isinstance(item, dict) or not isinstance(item.get("path"), str):
            raise GateError("candidate methodology is invalid")
        candidate_file(candidate_root, candidate_root / item["path"], "candidate methodology frozen input is unavailable")


def validate_ownership(record: dict[str, Any], manifest: dict[str, Any], registry: dict[str, Any]) -> None:
    known = {unit["id"]: set(unit["touched"]["paths"]) for unit in manifest["units"]}
    manifest_paths = set().union(*known.values())
    if registry.get("schema_version") != 1 or not isinstance(registry.get("units"), list):
        raise GateError("post-methodology adaptation registry is invalid")
    registry_ids, registry_paths = set(), set()
    if registry["units"] != sorted(registry["units"], key=lambda item: item.get("id", "") if isinstance(item, dict) else ""):
        raise GateError("post-methodology adaptation registry order is invalid")
    for unit in registry["units"]:
        if not isinstance(unit, dict) or set(unit) != {"id", "paths"} or not isinstance(unit["id"], str) or not unit["id"] or not isinstance(unit["paths"], list) or not unit["paths"] or any(not isinstance(path, str) or not path for path in unit["paths"]):
            raise GateError("post-methodology adaptation registry is invalid")
        if not ID_RE.fullmatch(unit["id"]):
            raise GateError("post-methodology adaptation registry id is invalid")
        if unit["id"] in known or unit["id"] in registry_ids or unit["paths"] != sorted(unit["paths"]):
            raise GateError("post-methodology adaptation registry ownership is ambiguous")
        for path in unit["paths"]:
            normalized = PurePosixPath(path)
            bootstrap_registry_path = (
                unit["id"] == "roots-post-methodology-pr-gate-v1"
                and path == POST_METHODOLOGY_REGISTRY
            )
            if not bootstrap_registry_path and (normalized.is_absolute() or path != normalized.as_posix() or "." in normalized.parts or ".." in normalized.parts or any(char in path for char in "*?[]\\") or path in registry_paths or path in manifest_paths):
                raise GateError("post-methodology adaptation registry path is invalid")
            registry_paths.add(path)
        registry_ids.add(unit["id"])
        known[unit["id"]] = set(unit["paths"])
    ordered = sorted(record["changes"], key=lambda item: (item["path"], item["kind"], item["digest"]))
    if record["changes"] != ordered:
        raise GateError("accounting record order is invalid")
    for item in record["changes"]:
        if item["disposition"] != "embargoed" and item["adaptation"] not in known:
            raise GateError("accounting adaptation is unknown")
        if item["disposition"] in {"update", "absorb", "exempt"} and item["path"] not in known[item["adaptation"]]:
            raise GateError("accounting adaptation does not own changed path")
        if item["disposition"] == "add" and item["path"] not in known.get(item["adaptation"], set()):
            raise GateError("new adaptation lacks a versioned registry entry")
        if item["path"] == "src/validation.cpp" and (item["risk"] != "critical" or "functional" not in item["tests"]):
            raise GateError("critical path accounting evidence is insufficient")
        if item["path"].startswith("src/") and item["path"] != "src/validation.cpp" and (item["risk"] not in {"medium", "high", "critical"} or "test" not in item["tests"]):
            raise GateError("source path accounting evidence is insufficient")
        if item["path"].startswith("doc/") and (item["risk"] not in {"low", "medium"} or "doc" not in item["tests"]):
            raise GateError("documentation accounting evidence is insufficient")
        if item["path"].startswith("generated/") and (item["risk"] not in {"medium", "high", "critical"} or "generator" not in item["tests"]):
            raise GateError("generated accounting evidence is insufficient")
        if item["path"] == "contrib/roots/adaptation-manifest-29.3.json" and (item["risk"] not in {"high", "critical"} or "manifest" not in item["tests"]):
            raise GateError("manifest accounting evidence is insufficient")
        if item["path"] == ".github/workflows/roots-portability.yml" and (item["risk"] not in {"high", "critical"} or "ci/test/test_roots_pr_gate.py" not in item["tests"]):
            raise GateError("workflow accounting evidence is insufficient")
        if item["path"].startswith("ci/test/") and (item["risk"] not in {"medium", "high", "critical"} or "ci/test/test_roots_pr_gate.py" not in item["tests"]):
            raise GateError("CI test accounting evidence is insufficient")
        if item["path"] == "contrib/devtools/roots-pr-gate.py" and (item["risk"] not in {"high", "critical"} or "ci/test/test_roots_pr_gate.py" not in item["tests"]):
            raise GateError("PR-gate accounting evidence is insufficient")


def gate(repository: Path, record_path: Path, manifest_path: Path, methodology_path: Path, registry_path: Path, base: str, candidate: str, tools_directory: Path) -> dict[str, Any]:
    validate_revision(base, "base")
    validate_revision(candidate, "candidate")
    if not repository.is_dir() or repository.is_symlink():
        raise GateError("repository must be a real directory")
    candidate_root = manifest_path.absolute().parents[2]
    if not candidate_root.is_dir() or candidate_root.is_symlink():
        raise GateError("candidate root must be a real directory")
    record_path = candidate_file(candidate_root, record_path, "candidate accounting record is unavailable")
    record = read_record(record_path)
    schema_path = manifest_path.with_name("adaptation-manifest.schema.json")
    for control in (manifest_path, methodology_path, schema_path, registry_path):
        candidate_file(candidate_root, control, "candidate control input is unavailable")
    validate_methodology_inputs(candidate_root, methodology_path)
    accounting = load_validator(trusted_tool(tools_directory, "roots-continuous-accounting.py"), "roots_continuous_accounting")
    manifest_validator = load_validator(trusted_tool(tools_directory, "roots-adaptation-manifest.py"), "roots_adaptation_manifest")
    manifest = manifest_validator.read_manifest(manifest_path)
    manifest_validator.validate_manifest(manifest)
    schema = read_json(schema_path, "candidate manifest schema is invalid")
    if schema.get("$schema") != "https://json-schema.org/draft/2020-12/schema" or schema.get("title") != "Bitcoin Roots logical adaptation manifest":
        raise GateError("candidate manifest schema is invalid")
    trusted_schema = tools_directory.parents[1] / "contrib/roots/adaptation-manifest.schema.json"
    trusted_manifest = tools_directory.parents[1] / "contrib/roots/adaptation-manifest-29.3.json"
    trusted_methodology = tools_directory.parents[1] / "contrib/roots/methodology-v1.json"
    if not trusted_schema.is_file() or trusted_schema.read_bytes() != schema_path.read_bytes():
        raise GateError("candidate manifest schema differs from the trusted contract")
    if not trusted_manifest.is_file() or trusted_manifest.read_bytes() != manifest_path.read_bytes():
        raise GateError("candidate manifest differs from the trusted contract")
    methodology = load_validator(trusted_tool(tools_directory, "roots-methodology.py"), "roots_methodology")
    methodology.validate(methodology_path)
    if not trusted_methodology.is_file() or trusted_methodology.read_bytes() != methodology_path.read_bytes():
        raise GateError("candidate methodology differs from the trusted contract")
    validate_ownership(record, manifest, read_json(registry_path, "post-methodology adaptation registry is invalid"))
    try:
        observed = accounting.atoms(repository, base, candidate)
    except ValueError as error:
        raise GateError("cannot compare the requested immutable revisions") from error
    self_atoms = {atom for atom in observed if atom[0] == ACCOUNTING_RECORD}
    observed -= self_atoms
    if len(observed) > MAX_CHANGED_FILES:
        raise GateError("changed-file count exceeds the pull-request safety limit")
    try:
        accounting.validate(record, observed)
    except ValueError as error:
        raise GateError(str(error)) from error
    changes = sorted(record["changes"], key=lambda item: (item["path"], item["kind"], item["digest"]))
    report = {
        "schema_version": 1,
        "base": base,
        "candidate": candidate,
        "accounting_record": ACCOUNTING_RECORD,
        "atom_count": len(observed),
        "changes": changes,
    }
    payload = canonical(report)
    if len(payload) > MAX_REPORT_BYTES:
        raise GateError("deterministic report exceeds the safety limit")
    report["digest"] = "sha256:" + hashlib.sha256(payload).hexdigest()
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--methodology", type=Path, required=True)
    parser.add_argument("--registry", type=Path, required=True)
    parser.add_argument("--base", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--trusted-tools", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        report = gate(args.repository, args.record, args.manifest, args.methodology, args.registry, args.base, args.candidate, args.trusted_tools)
        output = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.report is None:
            sys.stdout.write(output)
        else:
            args.report.write_text(output, encoding="utf-8", newline="\n")
    except (GateError, OSError, UnicodeError, json.JSONDecodeError, ValueError) as error:
        print(f"roots-pr-gate: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
