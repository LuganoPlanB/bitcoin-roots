#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Derive and validate a deterministic migration retrospective from locked evidence.

The tool deliberately has no release-, path-, or count-specific knowledge.  A
migration fixture supplies forecast classifications, the resolution proposal
supplies actual outcomes, and acceptance/validation/comparison records supply
the observations that must have an accountable methodology action.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
from typing import Any


class RetrospectiveError(ValueError):
    pass


def canonical_json(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def digest(value: Any) -> str:
    return "sha256:" + hashlib.sha256(canonical_json(value)).hexdigest()


def file_digest(path: Path) -> str:
    return "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RetrospectiveError(f"cannot read {path}") from error
    if not isinstance(value, dict):
        raise RetrospectiveError(f"{path} is not an object")
    return value


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise RetrospectiveError(message)


def _exact_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def forecast_paths(fixture: dict[str, Any]) -> dict[str, str]:
    outcomes = fixture.get("expected_outcomes")
    snapshot = fixture.get("snapshot_only")
    _require(isinstance(outcomes, dict) and isinstance(snapshot, dict), "fixture lacks forecast outcomes")
    result: dict[str, str] = {}
    for forecast, paths in outcomes.items():
        _require(isinstance(forecast, str) and isinstance(paths, list), "invalid fixture outcome group")
        for path in paths:
            _require(isinstance(path, str) and path not in result, "fixture forecast paths are not unique")
            result[path] = forecast
    path = snapshot.get("path")
    _require(isinstance(path, str) and path not in result, "fixture snapshot path is invalid")
    result[path] = "snapshot-only"
    return result


def outcome_rows(fixture: dict[str, Any], proposal: dict[str, Any]) -> list[dict[str, str]]:
    forecast = forecast_paths(fixture)
    outcomes = proposal.get("outcomes")
    _require(isinstance(outcomes, list), "proposal lacks outcomes")
    rows: list[dict[str, str]] = []
    seen: set[str] = set()
    for outcome in outcomes:
        _require(isinstance(outcome, dict), "proposal outcome is invalid")
        path, expected, actual = outcome.get("path"), outcome.get("l4_expected_outcome"), outcome.get("proposed_status")
        _require(isinstance(path, str) and isinstance(expected, str) and isinstance(actual, str), "proposal outcome fields are invalid")
        _require(path not in seen and forecast.get(path) == expected, "proposal outcome does not match locked forecast")
        seen.add(path)
        rows.append({"path": path, "forecast": expected, "actual": actual})
    _require(seen == set(forecast), "proposal omits or adds a forecast path")
    return sorted(rows, key=lambda row: row["path"])


def grouped_transitions(rows: list[dict[str, str]]) -> list[dict[str, Any]]:
    groups: dict[tuple[str, str], list[str]] = {}
    for row in rows:
        groups.setdefault((row["forecast"], row["actual"]), []).append(row["path"])
    return [
        {"forecast": forecast, "actual": actual, "paths": sorted(paths)}
        for (forecast, actual), paths in sorted(groups.items())
    ]


def metrics(rows: list[dict[str, str]], acceptance: dict[str, Any], validation: dict[str, Any], incremental: dict[str, Any], locked_input_count: int) -> dict[str, Any]:
    accounting = acceptance.get("l4_accounting")
    actions = validation.get("github_actions")
    fresh = validation.get("fresh_replay")
    raw = incremental.get("comparisons", {}).get("raw_tree")
    _require(isinstance(accounting, dict) and isinstance(actions, dict) and isinstance(fresh, dict) and isinstance(raw, dict), "evidence records are incomplete")
    _require(accounting.get("unaccounted") == [], "acceptance evidence has unaccounted outcomes")
    _require(actions.get("run_conclusion") != "success" and actions.get("lint", {}).get("status") == "known-inherited-failure", "platform evidence does not preserve its qualified failure")
    _require(fresh.get("status") == "pass" and _exact_int(fresh.get("runs")) and fresh["runs"] >= 2, "fresh reconstruction evidence is incomplete")
    _require(incremental.get("accepted_lineage") is False and incremental.get("accounting", {}).get("unowned_differences") == [], "incremental comparison is not fail-closed")
    _require(_exact_int(raw.get("difference_count")) and raw["difference_count"] == len(raw.get("differences", [])), "incremental differences are not complete")
    automatic = [row["path"] for row in rows if row["actual"] == "automatic"]
    false_clean = [row["path"] for row in rows if row["forecast"] in {"exact", "clean-textual"} and row["actual"] != "automatic"]
    false_conflict = [row["path"] for row in rows if row["forecast"] == "manual" and row["actual"] != "manual"]
    generated = [row["path"] for row in rows if row["forecast"] == "generated"]
    rewritten_generated = [row["path"] for row in rows if row["forecast"] == "generated" and row["actual"] == "rewritten"]
    manual = [row["path"] for row in rows if row["actual"] == "manual"]
    critical_gates = [gate for gate in acceptance.get("gates", []) if isinstance(gate, dict) and gate.get("critical")]
    _require(len(critical_gates) > 0 and all(gate.get("status") == "pass" for gate in critical_gates), "critical invariant gate is not passing")
    artifacts = fresh.get("artifacts")
    _require(isinstance(artifacts, dict) and artifacts, "fresh replay report artifacts are missing")
    total = len(rows)
    return {
        "automation": {"total_outcomes": total, "automatic_outcomes": len(automatic), "basis_points": 10000 * len(automatic) // total},
        "classification": {"false_clean_paths": false_clean, "false_conflict_paths": false_conflict},
        "manual_effort": {"manual_outcomes": len(manual), "forecast_manual_outcomes": sum(row["forecast"] == "manual" for row in rows), "eliminated_manual_boundaries": len(false_conflict)},
        "provenance": {"locked_inputs": locked_input_count, "stale_or_missing_records": 0},
        "invariants": {"critical_gates": len(critical_gates), "passing_critical_gates": len(critical_gates)},
        "generated_files": {"forecast_generated_outcomes": len(generated), "rewritten_generated_outcomes": len(rewritten_generated)},
        "replay_restarts": {"clean_runs": fresh["runs"], "restart_events_status": "not-recorded"},
        "report_usability": {"published_artifacts": len(artifacts), "independent_reproduction_status": "not-recorded"},
        "fresh_vs_incremental": {"fresh_runs": fresh["runs"], "incremental_differences": raw["difference_count"], "incremental_lineage_accepted": incremental["accepted_lineage"]},
    }


def observations(metrics_value: dict[str, Any]) -> list[dict[str, str]]:
    false_conflict_status = "action-required" if metrics_value["classification"]["false_conflict_paths"] else "resolved"
    return [
        {"category": "automation", "status": "resolved", "owner": "methodology", "severity": "high", "action": "retain complete forecast-to-outcome accounting", "acceptance_rationale": "all locked forecast paths are accounted"},
        {"category": "classification", "status": false_conflict_status, "owner": "methodology", "severity": "medium", "action": "require proof before a forecast manual boundary is absorbed", "acceptance_rationale": "clean forecasts and conservative escalations are separately measurable"},
        {"category": "manual-effort", "status": "resolved", "owner": "methodology", "severity": "medium", "action": "record manual outcomes and eliminated boundaries", "acceptance_rationale": "manual effort remains bounded by outcome evidence"},
        {"category": "provenance", "status": "resolved", "owner": "methodology", "severity": "high", "action": "retain digest-locked evidence inputs", "acceptance_rationale": "no stale or missing record is accepted"},
        {"category": "invariants", "status": "resolved", "owner": "safety", "severity": "high", "action": "require all critical invariant gates", "acceptance_rationale": "all recorded critical gates passed"},
        {"category": "generated-files", "status": "resolved", "owner": "replay", "severity": "medium", "action": "derive generated outputs after candidate construction", "acceptance_rationale": "generated outcomes are separately recorded"},
        {"category": "replay-restarts", "status": "action-required", "owner": "replay", "severity": "medium", "action": "version explicit restart-event evidence", "acceptance_rationale": "current evidence records clean runs but not restart events"},
        {"category": "report-usability", "status": "action-required", "owner": "operations", "severity": "medium", "action": "record independent report-consumer reproduction", "acceptance_rationale": "current evidence records artifacts but not usability observation"},
        {"category": "fresh-vs-incremental", "status": "resolved", "owner": "methodology", "severity": "high", "action": "treat incremental history only as a classified diagnostic", "acceptance_rationale": "all observed differences are owned and incremental lineage is not accepted"},
        {"category": "platform-result", "status": "resolved", "owner": "operations", "severity": "medium", "action": "preserve lane-level classification separate from aggregate conclusion", "acceptance_rationale": "inherited failures must not be called candidate success"},
    ]


def build_record(fixture_path: Path, proposal_path: Path, acceptance_path: Path, validation_path: Path, incremental_path: Path) -> dict[str, Any]:
    fixture = read_json(fixture_path)
    proposal = read_json(proposal_path)
    acceptance = read_json(acceptance_path)
    validation = read_json(validation_path)
    incremental = read_json(incremental_path)
    rows = outcome_rows(fixture, proposal)
    inputs = [
        {"role": role, "digest": file_digest(path)}
        for role, path in sorted((("fixture", fixture_path), ("proposal", proposal_path), ("acceptance", acceptance_path), ("validation", validation_path), ("incremental", incremental_path)))
    ]
    metrics_value = metrics(rows, acceptance, validation, incremental, len(inputs))
    record = {
        "schema_version": 1,
        "inputs": inputs,
        "outcomes": rows,
        "transitions": grouped_transitions(rows),
        "metrics": metrics_value,
        "observations": observations(metrics_value),
    }
    record["digest"] = digest(record)
    return record


def validate_schema(record: dict[str, Any]) -> None:
    _require(set(record) == {"schema_version", "inputs", "outcomes", "transitions", "metrics", "observations", "digest"}, "retrospective schema is invalid")
    _require(_exact_int(record.get("schema_version")) and record["schema_version"] == 1 and isinstance(record["digest"], str) and record["digest"].startswith("sha256:"), "retrospective schema version or digest is invalid")
    _require(isinstance(record["inputs"], list) and len(record["inputs"]) >= 1 and len({item.get("role") for item in record["inputs"] if isinstance(item, dict)}) == len(record["inputs"]), "retrospective inputs are invalid")
    _require(all(isinstance(item, dict) and set(item) == {"role", "digest"} and isinstance(item["role"], str) and isinstance(item["digest"], str) and item["digest"].startswith("sha256:") for item in record["inputs"]), "retrospective input shape is invalid")
    _require(isinstance(record["outcomes"], list) and record["outcomes"] and all(isinstance(item, dict) and set(item) == {"path", "forecast", "actual"} and all(isinstance(item[key], str) and item[key] for key in item) for item in record["outcomes"]), "retrospective outcomes are invalid")
    _require(isinstance(record["transitions"], list) and record["transitions"] and all(isinstance(item, dict) and set(item) == {"forecast", "actual", "paths"} and isinstance(item["forecast"], str) and isinstance(item["actual"], str) and isinstance(item["paths"], list) and item["paths"] for item in record["transitions"]), "retrospective transitions are invalid")
    _require(isinstance(record["metrics"], dict) and set(record["metrics"]) == {"automation", "classification", "manual_effort", "provenance", "invariants", "generated_files", "replay_restarts", "report_usability", "fresh_vs_incremental"}, "retrospective metric categories are incomplete")
    metrics_value = record["metrics"]
    count_shapes = {
        "automation": {"total_outcomes", "automatic_outcomes", "basis_points"},
        "manual_effort": {"manual_outcomes", "forecast_manual_outcomes", "eliminated_manual_boundaries"},
        "provenance": {"locked_inputs", "stale_or_missing_records"},
        "invariants": {"critical_gates", "passing_critical_gates"},
        "generated_files": {"forecast_generated_outcomes", "rewritten_generated_outcomes"},
    }
    _require(all(isinstance(metrics_value[name], dict) and set(metrics_value[name]) == keys and all(_exact_int(value) and value >= 0 for value in metrics_value[name].values()) for name, keys in count_shapes.items()), "retrospective count metrics are invalid")
    _require(isinstance(metrics_value["classification"], dict) and set(metrics_value["classification"]) == {"false_clean_paths", "false_conflict_paths"} and all(isinstance(value, list) and all(isinstance(path, str) and path for path in value) for value in metrics_value["classification"].values()), "retrospective classification metrics are invalid")
    _require(isinstance(metrics_value["replay_restarts"], dict) and set(metrics_value["replay_restarts"]) == {"clean_runs", "restart_events_status"} and _exact_int(metrics_value["replay_restarts"]["clean_runs"]) and metrics_value["replay_restarts"]["clean_runs"] >= 0 and metrics_value["replay_restarts"]["restart_events_status"] in {"recorded", "not-recorded"}, "retrospective restart metric is invalid")
    _require(isinstance(metrics_value["report_usability"], dict) and set(metrics_value["report_usability"]) == {"published_artifacts", "independent_reproduction_status"} and _exact_int(metrics_value["report_usability"]["published_artifacts"]) and metrics_value["report_usability"]["published_artifacts"] >= 0 and metrics_value["report_usability"]["independent_reproduction_status"] in {"recorded", "not-recorded"}, "retrospective usability metric is invalid")
    _require(isinstance(metrics_value["fresh_vs_incremental"], dict) and set(metrics_value["fresh_vs_incremental"]) == {"fresh_runs", "incremental_differences", "incremental_lineage_accepted"} and _exact_int(metrics_value["fresh_vs_incremental"]["fresh_runs"]) and _exact_int(metrics_value["fresh_vs_incremental"]["incremental_differences"]) and isinstance(metrics_value["fresh_vs_incremental"]["incremental_lineage_accepted"], bool), "retrospective comparison metric is invalid")
    metric_categories = {name.replace("_", "-") for name in record["metrics"]}
    _require(isinstance(record["observations"], list) and record["observations"] and {item.get("category") for item in record["observations"] if isinstance(item, dict)} >= metric_categories, "retrospective findings do not cover every metric category")
    required = {"category", "status", "owner", "severity", "action", "acceptance_rationale"}
    _require(all(isinstance(item, dict) and set(item) == required and item["status"] in {"resolved", "action-required", "accepted-limitation"} and item["severity"] in {"low", "medium", "high", "critical"} and all(isinstance(item[key], str) and item[key] for key in required) for item in record["observations"]), "retrospective finding shape is invalid")


def validate_record(record: dict[str, Any], fixture_path: Path, proposal_path: Path, acceptance_path: Path, validation_path: Path, incremental_path: Path) -> None:
    validate_schema(record)
    expected = build_record(fixture_path, proposal_path, acceptance_path, validation_path, incremental_path)
    _require(record == expected, "retrospective does not exactly derive from locked evidence")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("build", "validate"))
    parser.add_argument("fixture", type=Path)
    parser.add_argument("proposal", type=Path)
    parser.add_argument("acceptance", type=Path)
    parser.add_argument("validation", type=Path)
    parser.add_argument("incremental", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "build":
            record = build_record(args.fixture, args.proposal, args.acceptance, args.validation, args.incremental)
            encoded = canonical_json(record) + b"\n"
            if args.output is None:
                sys.stdout.buffer.write(encoded)
            else:
                args.output.write_bytes(encoded)
        else:
            _require(args.output is not None, "validate requires --output RECORD")
            validate_record(read_json(args.output), args.fixture, args.proposal, args.acceptance, args.validation, args.incremental)
    except RetrospectiveError as error:
        print(f"roots-retrospective: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
