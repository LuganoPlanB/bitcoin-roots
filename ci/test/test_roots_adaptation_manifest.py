#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Focused contract tests for the logical adaptation-manifest validator."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "contrib/devtools/roots-adaptation-manifest.py"
LEDGER = ROOT / "contrib/roots/lineage-ledger.json"
PARTITION = ROOT / "contrib/roots/atlas-layer-partition-29.3.json"
REPORT = ROOT / "contrib/roots/atlas-report-29.3.json"
CLASSIFICATION = ROOT / "contrib/roots/atlas-classification-29.3.json"
LAYER_REPORT = ROOT / "contrib/roots/adaptation-layer-29.3.json"
COVERAGE_REPORT = ROOT / "contrib/roots/adaptation-coverage-29.3.json"


def load_module():
    specification = importlib.util.spec_from_file_location("roots_adaptation_manifest", SCRIPT)
    assert specification and specification.loader
    module = importlib.util.module_from_spec(specification)
    sys.modules[specification.name] = module
    specification.loader.exec_module(module)
    return module


MANIFEST = load_module()
DIGEST = "sha256:" + "a" * 64
COMMIT = "sha1:" + "b" * 40
PATCH = "patch-id:sha1:" + "c" * 40


def unit(unit_id: str, mechanism="commit", lifecycle="active"):
    return {
        "id": unit_id,
        "owner_area": "identity",
        "purpose": "Keep Roots identity isolated from upstream behavior.",
        "provenance": [{"project": "bitcoin-roots", "commit": COMMIT, "confidence": "verified"}],
        "license": {"compatible": True, "evidence": "COPYING"},
        "original_patch_ids": [PATCH],
        "current_patch_ids": [PATCH],
        "layer": "roots-owned",
        "dependencies": [],
        "conflicts": [],
        "provides": [],
        "conditions": {"release_tags": [], "replaces": []},
        "phase": "ui-branding",
        "touched": {"targets": ["bitcoin-common"], "paths": ["src/clientversion.cpp"]},
        "generated": {"inputs": [], "outputs": []},
        "risk_tier": "low",
        "consensus_policy_effect": "consensus-neutral",
        "supported_release_range": {"from": "v29.3", "through": "v29.4"},
        "application": {"mechanism": mechanism, "reference": "contrib/roots/example"},
        "required_tests": ["ci/test/test_roots_adaptation_manifest.py"],
        "upstream_disposition": {"status": "roots-only", "evidence": "Roots product identity"},
        "retirement_conditions": "Replace when the Core branding contract is adopted.",
        "lifecycle": lifecycle,
    }


def manifest(units):
    return {"schema_version": 1, "atlas_report_digest": DIGEST, "units": units}


class RootsAdaptationManifestTest(unittest.TestCase):
    def test_representative_mechanisms_and_terminal_states_validate(self):
        units = []
        for suffix, mechanism, lifecycle in [
            ("binary-asset", "module/data", "active"),
            ("build", "commit", "active"),
            ("compatibility", "manual", "active"),
            ("documentation", "generator", "active"),
            ("gui", "patch", "active"),
            ("policy", "commit", "active"),
            ("revert", "patch", "obsolete"),
            ("upstream-absorbed", "commit", "absorbed"),
            ("rejected", "manual", "rejected"),
        ]:
            units.append(unit(f"roots-{suffix}", mechanism, lifecycle))
        MANIFEST.validate_manifest(manifest(sorted(units, key=lambda item: item["id"])))

    def test_missing_provenance_and_license_evidence_fail(self):
        value = manifest([unit("roots-branding")])
        value["units"][0]["provenance"] = []
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.validate_manifest(value)
        value = manifest([unit("roots-branding")])
        value["units"][0]["license"]["evidence"] = ""
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.validate_manifest(value)

    def test_unstable_id_and_unclassified_risk_fail(self):
        value = manifest([unit("branding")])
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.validate_manifest(value)
        value = manifest([unit("roots-branding")])
        value["units"][0]["risk_tier"] = ""
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.validate_manifest(value)

    def test_dependency_cycle_and_unknown_reference_fail(self):
        first, second = unit("roots-alpha"), unit("roots-beta")
        first["dependencies"] = ["roots-beta"]
        second["dependencies"] = ["roots-alpha"]
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.validate_manifest(manifest([first, second]))
        value = manifest([unit("roots-alpha")])
        value["units"][0]["dependencies"] = ["roots-missing"]
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.validate_manifest(value)

    def test_sorted_deterministic_collections_are_required(self):
        value = manifest([unit("roots-branding")])
        value["units"][0]["touched"]["paths"] = ["z", "a"]
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.validate_manifest(value)
        value = manifest([unit("roots-alpha"), unit("roots-alpha")])
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.validate_manifest(value)

    def test_extra_fields_fail_closed(self):
        value = manifest([unit("roots-branding")])
        value["units"][0]["unsafe"] = True
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.validate_manifest(value)

    def test_cli_accepts_a_canonical_manifest_and_rejects_duplicate_keys(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            temporary = Path(temporary_dir)
            valid = temporary / "valid.json"
            valid.write_text(json.dumps(manifest([unit("roots-branding")]), sort_keys=True), encoding="utf-8")
            result = subprocess.run([sys.executable, SCRIPT, valid], cwd=ROOT, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            duplicate = temporary / "duplicate.json"
            duplicate.write_text('{"schema_version":1,"schema_version":1}', encoding="utf-8")
            result = subprocess.run([sys.executable, SCRIPT, duplicate], cwd=ROOT, text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("duplicate JSON key", result.stderr)

    def test_layer_classification_is_complete_deterministic_and_marks_unresolved_knots_origin(self):
        ledger = MANIFEST.read_manifest(LEDGER)
        partition = MANIFEST.read_manifest(PARTITION)
        report = MANIFEST.read_manifest(REPORT)
        classification = MANIFEST.read_manifest(CLASSIFICATION)
        first = MANIFEST.classify_layers(ledger, partition, report, classification)
        second = MANIFEST.classify_layers(ledger, partition, report, classification)
        self.assertEqual(MANIFEST.canonical_json(first), MANIFEST.canonical_json(second))
        self.assertEqual(first, MANIFEST.read_manifest(LAYER_REPORT))
        self.assertEqual([layer["record_count"] for layer in first["layers"]], [718, 40, 158])
        self.assertEqual(first["first_fork_boundary"]["path_count"], 40)
        self.assertEqual(first["layers"][0]["records"][0]["knots_origin"], "core-or-knots-origin-unresolved")
        samples = {record["path"] for layer in first["layers"] for record in layer["records"]}
        for path in ["CMakeLists.txt", "README.md", "src/policy/policy.cpp", "src/wallet/wallet.cpp", "src/qt/bitcoin.cpp", "src/validation.cpp"]:
            self.assertIn(path, samples)

    def test_layer_classification_rejects_stale_atlas_or_unaccounted_records(self):
        ledger = MANIFEST.read_manifest(LEDGER)
        partition = MANIFEST.read_manifest(PARTITION)
        report = MANIFEST.read_manifest(REPORT)
        classification = MANIFEST.read_manifest(CLASSIFICATION)
        stale = copy.deepcopy(classification)
        stale["records"][0]["path"] = "stale-path"
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.classify_layers(ledger, partition, report, stale)
        unaccounted = copy.deepcopy(classification)
        unaccounted["records"].append(copy.deepcopy(unaccounted["records"][0]))
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.classify_layers(ledger, partition, report, unaccounted)

    def test_layer_classification_cannot_mark_an_exclusion_as_applied(self):
        value = MANIFEST.read_manifest(LAYER_REPORT)
        value["layers"][0]["records"][0]["applied"] = False
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.validate_layer_classification(value)

    def test_application_order_is_stable_across_input_order_and_diamonds(self):
        foundation = unit("roots-foundation")
        foundation["phase"] = "foundation"
        module = unit("roots-module")
        module["phase"] = "module"
        module["dependencies"] = ["roots-foundation"]
        behavior = unit("roots-behavior")
        behavior["phase"] = "behavior"
        behavior["dependencies"] = ["roots-foundation"]
        tests = unit("roots-tests")
        tests["phase"] = "tests"
        tests["dependencies"] = ["roots-behavior", "roots-module"]
        expected = ["roots-foundation", "roots-module", "roots-behavior", "roots-tests"]
        self.assertEqual(MANIFEST.application_order(manifest([tests, behavior, foundation, module])), expected)
        self.assertEqual(MANIFEST.application_order(manifest([foundation, module, behavior, tests])), expected)

    def test_application_order_rejects_duplicate_provider_conflict_and_unavailable_dependency(self):
        first, second = unit("roots-alpha"), unit("roots-beta")
        first["provides"] = ["identity"]
        second["provides"] = ["identity"]
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.application_order(manifest([first, second]))
        first, second = unit("roots-alpha"), unit("roots-beta")
        first["conflicts"] = ["roots-beta"]
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.application_order(manifest([first, second]))
        first, second = unit("roots-alpha"), unit("roots-beta")
        first["dependencies"] = ["roots-beta"]
        second["lifecycle"] = "absorbed"
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.application_order(manifest([first, second]))

    def test_application_order_applies_release_conditioned_replacement(self):
        old, new = unit("roots-old"), unit("roots-new")
        old["phase"] = "behavior"
        new["phase"] = "module"
        new["conditions"] = {"release_tags": ["v29.4"], "replaces": ["roots-old"]}
        self.assertEqual(MANIFEST.application_order(manifest([new, old]), "v29.3"), ["roots-old"])
        self.assertEqual(MANIFEST.application_order(manifest([new, old]), "v29.4"), ["roots-new"])

    def test_application_order_skips_upstream_absorbed_unit(self):
        active, absorbed = unit("roots-active"), unit("roots-absorbed", lifecycle="absorbed")
        self.assertEqual(MANIFEST.application_order(manifest([absorbed, active])), ["roots-active"])

    def test_topology_requires_annotated_core_tag_complete_commit_map_and_known_units(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            repository = Path(temporary_dir) / "repo"
            subprocess.run(["git", "init", repository], check=True, capture_output=True)
            for key, value in (("user.name", "Roots test"), ("user.email", "roots@test.invalid")):
                subprocess.run(["git", "-C", repository, "config", key, value], check=True)
            (repository / "base").write_text("base\n", encoding="utf-8")
            subprocess.run(["git", "-C", repository, "add", "base"], check=True)
            subprocess.run(["git", "-C", repository, "commit", "-m", "Core base"], check=True, capture_output=True)
            base = subprocess.run(["git", "-C", repository, "rev-parse", "HEAD"], text=True, check=True, capture_output=True).stdout.strip()
            subprocess.run(["git", "-C", repository, "tag", "-a", "v29.4", "-m", "Core release"], check=True)
            commits = []
            for name in ("alpha", "beta"):
                (repository / name).write_text(name + "\n", encoding="utf-8")
                subprocess.run(["git", "-C", repository, "add", name], check=True)
                subprocess.run(["git", "-C", repository, "commit", "-m", name], check=True, capture_output=True)
                commits.append(subprocess.run(["git", "-C", repository, "rev-parse", "HEAD"], text=True, check=True, capture_output=True).stdout.strip())
            tree = subprocess.run(["git", "-C", repository, "rev-parse", "HEAD^{tree}"], text=True, check=True, capture_output=True).stdout.strip()
            diff_digest = "sha256:" + hashlib.sha256(MANIFEST._git_bytes(repository, "diff", "--binary", "--full-index", "--no-ext-diff", base, commits[-1])).hexdigest()
            topology = {
                "schema_version": 1, "base_tag": "v29.4", "base_commit": "sha1:" + base,
                "target_commit": "sha1:" + commits[-1], "target_tree": "sha1:" + tree,
                "aggregate_diff_digest": diff_digest, "manifest_owned_diff_digest": diff_digest,
                "commit_map": [
                    {"commit": "sha1:" + commits[0], "adaptation_ids": ["roots-alpha"], "provenance": [{"project": "bitcoin-roots", "commit": "sha1:" + commits[0], "confidence": "verified"}]},
                    {"commit": "sha1:" + commits[1], "adaptation_ids": ["roots-beta"], "provenance": [{"project": "bitcoin-roots", "commit": "sha1:" + commits[1], "confidence": "verified"}]},
                ],
            }
            MANIFEST.verify_topology(repository, topology, manifest([unit("roots-alpha"), unit("roots-beta")]))
            incomplete = copy.deepcopy(topology)
            incomplete["commit_map"].pop()
            with self.assertRaises(MANIFEST.ManifestError):
                MANIFEST.verify_topology(repository, incomplete)
            unknown = copy.deepcopy(topology)
            unknown["commit_map"][0]["adaptation_ids"] = ["roots-missing"]
            with self.assertRaises(MANIFEST.ManifestError):
                MANIFEST.verify_topology(repository, unknown, manifest([unit("roots-alpha"), unit("roots-beta")]))
            subprocess.run(["git", "-C", repository, "tag", "-d", "v29.4"], check=True, capture_output=True)
            subprocess.run(["git", "-C", repository, "tag", "v29.4", base], check=True)
            with self.assertRaises(MANIFEST.ManifestError):
                MANIFEST.verify_topology(repository, topology)

    def test_coverage_assigns_every_direct_delta_once_and_is_deterministic(self):
        partition = MANIFEST.read_manifest(PARTITION)
        classification = MANIFEST.read_manifest(CLASSIFICATION)
        first = MANIFEST.build_coverage(partition, classification)
        second = MANIFEST.build_coverage(partition, classification)
        self.assertEqual(MANIFEST.canonical_json(first), MANIFEST.canonical_json(second))
        self.assertEqual(first, MANIFEST.read_manifest(COVERAGE_REPORT))
        self.assertEqual(first["record_count"], len(partition["direct_core_to_roots"]["records"]))
        self.assertEqual(len({record["delta"]["path"] for record in first["records"]}), first["record_count"])
        self.assertTrue(any(record["co_owned_adaptations"] for record in first["records"]))
        MANIFEST.validate_coverage(first, partition, classification)

    def test_coverage_rejects_hunk_tree_and_ownership_mutations(self):
        partition = MANIFEST.read_manifest(PARTITION)
        classification = MANIFEST.read_manifest(CLASSIFICATION)
        coverage = MANIFEST.build_coverage(partition, classification)
        changed = copy.deepcopy(coverage)
        changed["records"][0]["delta"]["status"] = "injected"
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.validate_coverage(changed, partition, classification)
        changed = copy.deepcopy(coverage)
        changed["records"][0]["co_owned_adaptations"] = [changed["records"][0]["primary_adaptation"]]
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.validate_coverage(changed)
        changed = copy.deepcopy(coverage)
        changed["records"].pop()
        changed["record_count"] -= 1
        changed["target_scoped_tree_digest"] = MANIFEST.digest([record["delta"] for record in changed["records"]])
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.validate_coverage(changed, partition, classification)
        value = manifest([unit("roots-branding")])
        value["units"][0]["application"]["command"] = "arbitrary"
        with self.assertRaises(MANIFEST.ManifestError):
            MANIFEST.validate_manifest(value)


if __name__ == "__main__":
    unittest.main()
