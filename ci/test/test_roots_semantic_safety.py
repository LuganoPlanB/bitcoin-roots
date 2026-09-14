#!/usr/bin/env python3
"""Mutation matrix for the Roots portability semantic-safety contracts."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "contrib/devtools/roots-semantic-safety.py"
SPEC = importlib.util.spec_from_file_location("roots_semantic_safety", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def digest(character: str = "a") -> str:
    return "sha256:" + character * 64


def outcome(identifier: str, candidate: str, scoped: str, manifest: str) -> dict:
    return {"id": identifier, "status": "pass", "candidate_digest": candidate, "scoped_tree_digest": scoped, "manifest_digest": manifest, "fixture_digest": digest("d"), "artifact_digest": digest("e"), "finished_at": "2026-09-14T01:00:00Z", "command": "test command", "test_identity": "test identity"}


def artifact(identifier: str, candidate: str, scoped: str, manifest: str, summary: str) -> dict:
    return {"id": identifier, "candidate_digest": candidate, "scoped_tree_digest": scoped, "manifest_digest": manifest, "artifact_digest": digest("1"), "summary": summary, "produced_at": "2026-09-14T01:00:00Z"}


def evidence() -> dict:
    candidate, scoped, manifest = digest("a"), digest("b"), digest("c")
    invariants = []
    for identifier in sorted(MODULE.REQUIRED_INVARIANTS):
        item = outcome(identifier, candidate, scoped, manifest)
        item.update({"expectation_scope": MODULE.INVARIANT_SCOPES[identifier], "responsible_adaptation_id": "adaptation", "core_knots_differential_digest": digest("f") if identifier in MODULE.DIFFERENTIAL_INVARIANTS else None, "policy_block_acceptance_digest": digest("0") if identifier in MODULE.POLICY_BLOCK_INVARIANTS else None})
        invariants.append(item)
    performance = artifact("performance", candidate, scoped, manifest, "unused")
    performance.pop("summary")
    performance.update({"baseline": "baseline", "result": "result", "explanation": "explanation"})
    value = {"schema_version": 1, "candidate_digest": candidate, "scoped_tree_digest": scoped, "manifest_revision": 2, "manifest_digest": manifest, "immutable_inputs": [{"id": "manifest", "kind": "manifest", "digest": manifest}, {"id": "materials", "kind": "materials", "digest": digest("3")}, {"id": "plan", "kind": "plan", "digest": digest("4")}], "expected_unit_ids": ["semantic-suite"], "finalized_at": "2026-09-14T00:00:00Z", "produced_at": "2026-09-14T01:00:00Z", "unit_outcomes": [outcome("semantic-suite", candidate, scoped, manifest)], "approvals": [{"id": "expert", "role": "reviewer", "candidate_digest": candidate, "scoped_tree_digest": scoped, "manifest_digest": manifest, "evidence_digest": digest("3"), "produced_at": "2026-09-14T01:00:00Z"}], "invariants": invariants, "build_test_platform_matrix": [{"id": platform, "platform": platform, "status": "pass", "candidate_digest": candidate, "scoped_tree_digest": scoped, "manifest_digest": manifest, "artifact_digest": digest("4"), "limitation": None} for platform in sorted(MODULE.PLATFORMS)], "generated_outputs": [artifact("vectors", candidate, scoped, manifest, "output")], "compatibility_migration": [artifact("migration", candidate, scoped, manifest, "migration")], "reproducibility": [artifact("rebuild", candidate, scoped, manifest, "repro")], "performance_resources": [performance], "limitations": [artifact("limitations", candidate, scoped, manifest, "none")], "security_review": [artifact("security", candidate, scoped, manifest, "review")], "release_approval": False}
    value["digest"] = "sha256:" + hashlib.sha256(MODULE.canonical(value)).hexdigest()
    return value


def ledger() -> dict:
    candidate, scoped, manifest = digest("a"), digest("b"), digest("d")
    bound = {"candidate_digest": candidate, "scoped_tree_digest": scoped, "manifest_digest": manifest, "result_digest": digest("5"), "produced_at": "2026-09-14T01:00:00Z"}
    base_blob, input_blob = "sha1:" + "2" * 40, "sha1:" + "3" * 40
    conflict = {"id": "hunk", "base_digest": base_blob, "input_digest": input_blob, "range_digest": digest("4")}
    entry = {"id": "entry", "candidate_digest": candidate, "scoped_tree_digest": scoped, "prior_manifest_revision": 1, "prior_manifest_digest": digest("c"), "current_manifest_revision": 2, "current_manifest_digest": manifest, "base_blob": base_blob, "input_blobs": [input_blob], "conflict_hunks": [conflict], "resolution_records": [{"id": "hunk", "coverage_digest": "sha256:" + hashlib.sha256(MODULE.canonical(conflict)).hexdigest(), "result_digest": digest("3"), "range_digest": digest("4")}], "rationale": "policy", "alternatives": ["reject"], "consensus_policy_analysis": "policy only", "reviewer_evidence": [bound | {"id": "review", "role": "policy reviewer"}], "test_evidence": [bound | {"id": "test", "test_identity": "block acceptance"}], "artifact_hashes": [digest("3")], "promotion_mechanism": "patch", "zones": ["policy-consensus-boundary"], "expert_markers": ["policy expert"], "embargoed": False, "public_record": {}}
    return {"schema_version": 1, "prior_manifest": {"revision": 1, "digest": digest("c")}, "current_manifest": {"revision": 2, "digest": manifest}, "candidate_digest": candidate, "scoped_tree_digest": scoped, "candidate_finalized_at": "2026-09-14T00:00:00Z", "entries": [entry]}


def lifecycle() -> dict:
    candidate, manifest = digest("a"), digest("b")
    return {"schema_version": 1, "candidate_digest": candidate, "manifest_digest": manifest, "proposals": [{"id": scenario, "adaptation_id": "adaptation", "scenario": scenario, "proposed_lifecycle": MODULE.LIFECYCLE_POLICY[scenario], "patch_ids": ["patch"], "symbols": ["symbol"], "tests": ["test"], "release_notes": ["note"], "behavior": "behavior", "evidence_digest": digest("c"), "candidate_digest": candidate, "manifest_digest": manifest, "decision": "manual-required"} for scenario in MODULE.LIFECYCLE_POLICY]}


def governance() -> dict:
    return {"schema_version": 1, "cases": [{"scenario": scenario, "severity": severity, "owner_role": owner, "authority": authority, "evidence": "evidence", "disclosure_route": route, "outcome": outcome} for scenario, (severity, outcome, owner, authority, route) in MODULE.GOVERNANCE_POLICY.items()]}


def validate_schema_subset(schema: dict, value: object, root: dict | None = None) -> None:
    """Small deterministic Draft-2020-12 subset used by this local schema."""
    root = schema if root is None else root
    if "$ref" in schema:
        target: object = root
        for part in schema["$ref"].removeprefix("#/").split("/"):
            target = target[part]
        validate_schema_subset(target, value, root)
        return
    if "const" in schema and value != schema["const"]:
        raise ValueError("const")
    if "enum" in schema and value not in schema["enum"]:
        raise ValueError("enum")
    if "oneOf" in schema:
        matches = 0
        for option in schema["oneOf"]:
            try:
                validate_schema_subset(option, value, root)
                matches += 1
            except ValueError:
                pass
        if matches != 1:
            raise ValueError("oneOf")
        return
    expected = schema.get("type")
    types = expected if isinstance(expected, list) else [expected]
    type_matches = {"object": isinstance(value, dict), "array": isinstance(value, list), "string": isinstance(value, str), "integer": isinstance(value, int) and not isinstance(value, bool), "boolean": isinstance(value, bool), "null": value is None}
    if expected is not None and not any(type_matches.get(item, False) for item in types):
        raise ValueError("type")
    if "minimum" in schema and (not isinstance(value, (int, float)) or isinstance(value, bool) or value < schema["minimum"]):
        raise ValueError("minimum")
    if isinstance(value, dict):
        required = set(schema.get("required", []))
        if not required.issubset(value):
            raise ValueError("required")
        properties = schema.get("properties", {})
        if schema.get("additionalProperties") is False and set(value) - set(properties):
            raise ValueError("additionalProperties")
        for key, child in properties.items():
            if key in value:
                validate_schema_subset(child, value[key], root)
    if isinstance(value, list):
        if len(value) < schema.get("minItems", 0) or len(value) > schema.get("maxItems", len(value)):
            raise ValueError("item count")
        if schema.get("uniqueItems") and len({json.dumps(item, sort_keys=True) for item in value}) != len(value):
            raise ValueError("unique items")
        if "items" in schema:
            for item in value:
                validate_schema_subset(schema["items"], item, root)
    if isinstance(value, str) and "pattern" in schema and not re.fullmatch(schema["pattern"], value):
        raise ValueError("pattern")
    if isinstance(value, str) and len(value) < schema.get("minLength", 0):
        raise ValueError("minLength")


def schema_keywords(schema: object) -> set[str]:
    if isinstance(schema, list):
        return set().union(*(schema_keywords(item) for item in schema))
    if not isinstance(schema, dict):
        return set()
    result = {key for key in schema if key not in {"$defs", "properties"}}
    for key, value in schema.items():
        if key in {"$defs", "properties"} and isinstance(value, dict):
            for child in value.values():
                result.update(schema_keywords(child))
        elif key == "oneOf":
            result.update(schema_keywords(value))
    return result


class SemanticSafetyTest(unittest.TestCase):
    def test_all_risk_zones_and_evasions_require_manual_review(self) -> None:
        for zone, needles in MODULE.CRITICAL_ZONES.items():
            change = {"id": zone, "paths": [needles[0]], "symbols": [], "targets": [], "behavior": ["fixture"], "transitive": [], "content_checks": {"docs": True, "tests": True, "branding": True, "tooling": True}, "rename_from": None, "move_from": None, "shared_header": None}
            self.assertEqual(MODULE.classify({"schema_version": 1, "changes": [change]})["classifications"][0]["decision"], "manual-required")
        for field in ("behavior", "rename_from", "move_from", "shared_header"):
            change = {"id": field, "paths": ["doc/a.md"], "symbols": [], "targets": [], "behavior": ["fixture"], "transitive": [], "content_checks": {"docs": True, "tests": True, "branding": True, "tooling": True}, "rename_from": None, "move_from": None, "shared_header": None}
            change[field] = "src/validation.cpp" if field != "behavior" else ["src/validation.cpp"]
            self.assertEqual(MODULE.classify({"schema_version": 1, "changes": [change]})["classifications"][0]["decision"], "manual-required")
        transitive = {"id": "parent", "paths": ["doc/a.md"], "symbols": [], "targets": [], "behavior": ["fixture"], "transitive": [{"id": "child", "paths": ["doc/b.md"], "symbols": ["CChainParams"], "targets": [], "behavior": ["fixture"], "transitive": [], "content_checks": {"docs": True, "tests": True, "branding": True, "tooling": True}, "rename_from": None, "move_from": None, "shared_header": None}], "content_checks": {"docs": True, "tests": True, "branding": True, "tooling": True}, "rename_from": None, "move_from": None, "shared_header": None}
        self.assertEqual(MODULE.classify({"schema_version": 1, "changes": [transitive]})["classifications"][0]["decision"], "manual-required")

    def test_classifier_fail_closed_mutations(self) -> None:
        def change(identifier: str = "change") -> dict:
            return {"id": identifier, "paths": ["doc/a.md"], "symbols": [], "targets": [], "behavior": ["fixture"], "transitive": [], "content_checks": {"docs": True, "tests": True, "branding": True, "tooling": True}, "rename_from": None, "move_from": None, "shared_header": None}
        cyclic = change("cycle")
        cyclic["transitive"] = [cyclic]
        deep = change("0")
        node = deep
        for number in range(9):
            child = change(str(number + 1))
            node["transitive"] = [child]
            node = child
        mutations = [lambda item: item["content_checks"].pop("tests"), lambda item: item["content_checks"].update(tests=False), lambda item: item.update(paths=["src/unknown.cpp"]), lambda item: item.update(transitive=["bad"])]
        for mutate in mutations:
            item = change()
            mutate(item)
            with self.assertRaises(MODULE.SafetyError): MODULE.classify({"schema_version": 1, "changes": [item]})
        with self.assertRaises(MODULE.SafetyError): MODULE.classify({"schema_version": 1, "changes": [change(), change()]})
        with self.assertRaises(MODULE.SafetyError): MODULE.classify({"schema_version": 1, "changes": [cyclic]})
        with self.assertRaises(MODULE.SafetyError): MODULE.classify({"schema_version": 1, "changes": [deep]})
        for child in ({"id": "child"}, {**change("child"), "unexpected": True}):
            parent = change("parent")
            parent["transitive"] = [child]
            with self.assertRaises(MODULE.SafetyError): MODULE.classify({"schema_version": 1, "changes": [parent]})
            with tempfile.TemporaryDirectory() as directory:
                fixture = Path(directory) / "bad.json"
                fixture.write_text(json.dumps({"schema_version": 1, "changes": [parent]}), encoding="utf-8")
                result = subprocess.run([sys.executable, str(SCRIPT), "classify", str(fixture)], capture_output=True, text=True, check=False)
                self.assertEqual(result.returncode, 1)
                self.assertIn("roots-semantic-safety:", result.stderr)
                self.assertNotIn("Traceback", result.stderr)

    def test_ledger_hunk_coverage_and_typed_evidence_mutations(self) -> None:
        valid = ledger()
        MODULE.validate_ledger(valid)
        mutations = [lambda value: value["entries"][0]["resolution_records"].clear(), lambda value: value["entries"][0]["resolution_records"][0].update(id="extra"), lambda value: value["entries"][0]["resolution_records"][0].update(range_digest=digest("f")), lambda value: value["entries"][0]["resolution_records"][0].update(coverage_digest=digest("f")), lambda value: value["entries"][0]["resolution_records"][0].update(result_digest=digest("f")), lambda value: value["entries"][0]["conflict_hunks"][0].update(base_digest="sha1:bad"), lambda value: value["entries"][0]["conflict_hunks"][0].update(input_digest="sha1:" + "f" * 39), lambda value: value["entries"][0]["conflict_hunks"][0].update(base_digest="sha1:" + "f" * 40), lambda value: value["entries"][0]["reviewer_evidence"][0].update(produced_at="2026-09-13T00:00:00Z"), lambda value: value["entries"][0]["test_evidence"][0].update(produced_at="2026-09-13T00:00:00Z"), lambda value: value["entries"][0].update(current_manifest_digest=digest("f"))]
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                value = copy.deepcopy(valid)
                mutate(value)
                with self.assertRaises(MODULE.SafetyError):
                    MODULE.validate_ledger(value)

    def test_candidate_schema_and_semantic_mutations(self) -> None:
        valid = evidence()
        MODULE.validate_evidence(valid)
        schema = json.loads((ROOT / "contrib/roots/candidate-evidence.schema.json").read_text())
        supported = {"$schema", "title", "type", "additionalProperties", "required", "properties", "$defs", "$ref", "items", "minItems", "maxItems", "uniqueItems", "enum", "const", "pattern", "oneOf", "minimum", "minLength"}
        self.assertTrue(schema_keywords(schema).issubset(supported))
        validate_schema_subset(schema, valid)
        structural = [lambda value: value.pop("immutable_inputs"), lambda value: value.update(manifest_revision=0), lambda value: value["unit_outcomes"][0].update(status="manual"), lambda value: value["invariants"][0].pop("command"), lambda value: value["approvals"][0].update(evidence_digest="bad")]
        for mutate in structural:
            with self.subTest(structural=mutate):
                value = copy.deepcopy(valid)
                mutate(value)
                with self.assertRaises(MODULE.SafetyError): MODULE.validate_evidence(value)
                with self.assertRaises(ValueError): validate_schema_subset(schema, value)
        semantic = [lambda value: value["unit_outcomes"].append(copy.deepcopy(value["unit_outcomes"][0])), lambda value: value["expected_unit_ids"].append("extra"), lambda value: value["unit_outcomes"][0].update(finished_at="2026-09-13T00:00:00Z"), lambda value: value["immutable_inputs"][0].update(digest=digest("f")), lambda value: value["immutable_inputs"].append({"id": "second", "kind": "manifest", "digest": digest("f")}), lambda value: value.update(produced_at="2026-09-13T00:00:00Z"), lambda value: value["approvals"][0].update(produced_at="2026-09-13T00:00:00Z"), lambda value: value["generated_outputs"][0].update(produced_at="2026-09-13T00:00:00Z"), lambda value: value["build_test_platform_matrix"].pop(), lambda value: value["build_test_platform_matrix"][0].update(status="approved-limitation", limitation={"reason": "reason", "approval_id": "missing", "approval_digest": digest("3")}), lambda value: value["performance_resources"][0].update(explanation=""), lambda value: value.update(digest=digest("f"))]
        for mutate in semantic:
            with self.subTest(semantic=mutate):
                value = copy.deepcopy(valid)
                mutate(value)
                with self.assertRaises(MODULE.SafetyError): MODULE.validate_evidence(value)

    def test_each_invariant_and_governance_lifecycle_mapping_rejects_wrong_values(self) -> None:
        valid = evidence()
        for index, item in enumerate(valid["invariants"]):
            for field, bad in (("expectation_scope", "wrong"), ("status", "fail"), ("candidate_digest", digest("f")), ("scoped_tree_digest", digest("f")), ("manifest_digest", digest("f")), ("responsible_adaptation_id", ""), ("command", ""), ("test_identity", ""), ("fixture_digest", "bad"), ("artifact_digest", "bad"), ("finished_at", "bad")):
                with self.subTest(invariant=item["id"], field=field):
                    value = copy.deepcopy(valid)
                    value["invariants"][index][field] = bad
                    with self.assertRaises(MODULE.SafetyError): MODULE.validate_evidence(value)
            for field, required in (("core_knots_differential_digest", item["id"] in MODULE.DIFFERENTIAL_INVARIANTS), ("policy_block_acceptance_digest", item["id"] in MODULE.POLICY_BLOCK_INVARIANTS)):
                with self.subTest(invariant=item["id"], conditional=field):
                    value = copy.deepcopy(valid)
                    value["invariants"][index][field] = None if required else digest("f")
                    with self.assertRaises(MODULE.SafetyError): MODULE.validate_evidence(value)
        for validator, value, field in ((MODULE.validate_lifecycle, lifecycle(), "proposed_lifecycle"), (MODULE.validate_governance, governance(), "severity"), (MODULE.validate_governance, governance(), "authority"), (MODULE.validate_governance, governance(), "disclosure_route"), (MODULE.validate_governance, governance(), "outcome")):
            validator(value)
            for index in range(len(value["proposals"] if field == "proposed_lifecycle" else value["cases"])):
                broken = copy.deepcopy(value)
                (broken["proposals"] if field == "proposed_lifecycle" else broken["cases"])[index][field] = "wrong"
                with self.assertRaises(MODULE.SafetyError): validator(broken)


if __name__ == "__main__":
    unittest.main()
