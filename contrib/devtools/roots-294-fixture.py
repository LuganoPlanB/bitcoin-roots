#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Validate the bounded Bitcoin Core 29.3-to-29.4 migration fixture.

This is an offline test oracle, not a replay tool.  It reads immutable objects
already present in a supplied Core clone and refuses a moved tag, a missing
path ownership record, or an attempt to treat archive-substituted metadata as
a Git change.  It never fetches, checks out, applies patches, or edits Roots.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from typing import Any


class FixtureError(ValueError):
    pass


def canonical_json(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def digest(value: Any) -> str:
    return "sha256:" + hashlib.sha256(canonical_json(value)).hexdigest()


def read_fixture(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise FixtureError("cannot read fixture") from error
    if not isinstance(value, dict) or value.get("schema_version") != 1:
        raise FixtureError("unsupported fixture schema")
    return value


def git(repository: Path, *args: str) -> str:
    result = subprocess.run(["git", "-C", str(repository), *args], check=False, capture_output=True, text=True, encoding="utf-8", errors="strict", env={"LC_ALL": "C", "PATH": "/usr/bin:/bin"})
    if result.returncode:
        raise FixtureError("Git object lookup failed")
    return result.stdout.strip()


def changed_paths(repository: Path, before: str, after: str) -> list[str]:
    output = git(repository, "diff", "--name-only", "--no-renames", "--no-ext-diff", before, after)
    return sorted(path for path in output.splitlines() if path)


def fixture_paths(value: dict[str, Any]) -> list[str]:
    units = value.get("units")
    if not isinstance(units, list) or not units:
        raise FixtureError("fixture units are required")
    paths: list[str] = []
    for unit in units:
        commits = unit.get("upstream_commits") if isinstance(unit, dict) else None
        if not isinstance(unit, dict) or not isinstance(unit.get("id"), str) or not isinstance(unit.get("paths"), list) or not isinstance(commits, list) or not commits or any(not isinstance(commit, str) or len(commit) != 40 for commit in commits):
            raise FixtureError("invalid fixture unit")
        paths.extend(unit["paths"])
    if len(paths) != len(set(paths)):
        raise FixtureError("unit paths must be globally unique")
    return sorted(paths)


def validate(value: dict[str, Any], repository: Path | None = None) -> None:
    inputs = value.get("core_inputs")
    if not isinstance(inputs, dict) or set(inputs) != {"v29.3", "v29.4"}:
        raise FixtureError("fixture requires v29.3 and v29.4 inputs")
    for tag, record in inputs.items():
        if not isinstance(record, dict) or set(record) != {"tag", "commit", "tree"} or any(not isinstance(record[key], str) or len(record[key]) != 40 for key in record):
            raise FixtureError(f"invalid {tag} object record")
    paths = fixture_paths(value)
    snapshot_only = value.get("snapshot_only")
    if not isinstance(snapshot_only, dict) or snapshot_only.get("path") != "src/clientversion.cpp" or snapshot_only.get("reason") != "archive GIT_COMMIT_ID expansion; not a Git-tree delta":
        raise FixtureError("archive-only clientversion discrepancy must remain explicit")
    if len(paths) != 39 or value.get("snapshot_observed_path_count") != 40:
        raise FixtureError("fixture must retain the 39 Git-path / 40 snapshot-path boundary")
    outcomes = value.get("expected_outcomes")
    if not isinstance(outcomes, dict) or set(outcomes) != {"exact", "absorbed", "clean-textual", "generated", "manual"}:
        raise FixtureError("fixture requires bounded expected outcomes")
    outcome_paths = [path for group in outcomes.values() if isinstance(group, list) for path in group]
    if any(not isinstance(group, list) or group != sorted(group) for group in outcomes.values()) or sorted(outcome_paths) != paths:
        raise FixtureError("expected outcomes must cover each Git path exactly once")
    if {name: len(group) for name, group in outcomes.items()} != {"exact": 14, "absorbed": 1, "clean-textual": 12, "generated": 6, "manual": 6}:
        raise FixtureError("expected outcome counts are stale")
    conflicts = value.get("manual_conflicts")
    if not isinstance(conflicts, list) or len(conflicts) != 7:
        raise FixtureError("fixture requires seven manual conflict records")
    conflict_paths = []
    for conflict in conflicts:
        if not isinstance(conflict, dict) or set(conflict) != {"alternatives", "forbidden", "owner_area", "path", "required_tests", "risk"} or not isinstance(conflict["path"], str) or not conflict["alternatives"] or not conflict["forbidden"] or not conflict["required_tests"] or "resolution" in conflict:
            raise FixtureError("manual conflict must specify constraints, not a resolution")
        conflict_paths.append(conflict["path"])
    if sorted(conflict_paths) != sorted(outcomes["manual"] + [snapshot_only["path"]]):
        raise FixtureError("manual conflicts must bind the six Git paths and archive-only identity path")
    generated = value.get("generated_outputs")
    expected_outputs = outcomes["generated"]
    if not isinstance(generated, dict) or set(generated) != {"commands", "identity", "inputs", "outputs", "tool"} or generated["outputs"] != expected_outputs or generated["identity"] != "29.4-roots (candidate tag substitution required)" or not all(isinstance(item, str) and item for item in generated["commands"] + generated["inputs"]):
        raise FixtureError("generated outputs require one deterministic deferred recipe")
    acceptance = value.get("acceptance")
    if not isinstance(acceptance, dict) or set(acceptance) != {"decision_statuses", "gates", "platforms"} or acceptance["platforms"] != ["Linux", "macOS", "Windows"] or acceptance["decision_statuses"] != ["applied", "absorbed", "rewritten", "rejected", "manual"] or any(not isinstance(gate, str) or not gate for gate in acceptance["gates"]):
        raise FixtureError("acceptance matrix or decision ledger contract is incomplete")
    if repository is None:
        return
    for tag, record in inputs.items():
        if git(repository, "rev-parse", f"refs/tags/{tag}^{{tag}}") != record["tag"] or git(repository, "rev-parse", f"refs/tags/{tag}^{{commit}}") != record["commit"] or git(repository, "rev-parse", f"refs/tags/{tag}^{{tree}}") != record["tree"]:
            raise FixtureError(f"{tag} provenance mismatch")
    if changed_paths(repository, "v29.3", "v29.4") != paths:
        raise FixtureError("Git delta does not match the bounded fixture")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("--repository", type=Path)
    args = parser.parse_args()
    try:
        validate(read_fixture(args.fixture), args.repository)
    except FixtureError as error:
        print(f"roots-294-fixture: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
