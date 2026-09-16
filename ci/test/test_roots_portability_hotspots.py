#!/usr/bin/env python3
"""Regression tests for manifest-owned portability hotspot evidence."""

from __future__ import annotations

import json
import unittest
from collections import defaultdict
from pathlib import Path
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[2]


def load_json(relative_path):
    return json.loads((ROOT / relative_path).read_text())


def path_owners(manifest):
    owners = defaultdict(list)
    for unit in manifest["units"]:
        for path in unit["touched"]["paths"]:
            owners[path].append(unit["id"])
    return owners


def manual_paths_by_owner(manifest, manual):
    owners = path_owners(manifest)
    result = defaultdict(list)
    for outcome in manual["outcomes"]:
        if outcome.get("l4_expected_outcome") != "manual":
            continue
        path = outcome["path"]
        if len(owners[path]) != 1:
            raise AssertionError(f"manual path must have one manifest owner: {path}")
        result[owners[path][0]].append(path)
    return {unit_id: sorted(paths) for unit_id, paths in result.items()}


class PortabilityHotspotTests(unittest.TestCase):
    def setUp(self):
        self.budget = load_json("contrib/roots/portability-delta-budget-29.4.json")
        self.hotspots = load_json("contrib/roots/portability-hotspots-29.4.json")
        self.seams = load_json("contrib/roots/portability-seams-29.4.json")
        self.manifest = load_json("contrib/roots/adaptation-manifest-29.3.json")
        self.outcomes = load_json("contrib/roots/replay-29.4-proposal/downstream-unit-outcomes.json")
        self.retrospective = load_json("contrib/roots/method-retrospective-29.4.json")
        self.manifest_ids = {unit["id"] for unit in self.manifest["units"]}
        self.owners = path_owners(self.manifest)
        self.manual_by_owner = manual_paths_by_owner(self.manifest, {"outcomes": [
            {"path": outcome["path"], "l4_expected_outcome": outcome["actual"]}
            for outcome in self.retrospective["outcomes"]
        ]})
        self.outcome_by_id = {outcome["id"]: outcome for outcome in self.outcomes["outcomes"]}

    def assert_surface(self, adaptation_id, surface):
        outcome = self.outcome_by_id[adaptation_id]
        self.assertEqual(surface, {
            "declared_paths": outcome["declared_paths"],
            "changed_paths": outcome["changed_paths"],
            "absorbed_paths": len(outcome["absorbed_paths"]),
        })

    def test_delta_budget_is_recomputed_from_accepted_inputs(self):
        baseline = self.budget["baseline"]
        metrics = self.retrospective["metrics"]
        all_paths = {path for unit in self.manifest["units"] for path in unit["touched"]["paths"]}
        expected_rate = {
            "numerator": metrics["manual_effort"]["manual_outcomes"],
            "denominator": metrics["automation"]["total_outcomes"],
            "basis_points": metrics["manual_effort"]["manual_outcomes"] * 10000 // metrics["automation"]["total_outcomes"],
        }
        ranked_ids = [item["adaptation_ids"][0] for item in self.hotspots["queue"]]

        self.assertEqual(baseline["logical_adaptation_units"], len(self.manifest["units"]))
        self.assertEqual(baseline["upstream_files_touched"], len(all_paths))
        self.assertEqual(baseline["manual_outcomes"], metrics["manual_effort"]["manual_outcomes"])
        self.assertEqual(sum(map(len, self.manual_by_owner.values())), baseline["manual_outcomes"])
        self.assertEqual(baseline["manual_adaptation_units"], ranked_ids)
        self.assertEqual(baseline["window_manual_rate"], expected_rate)
        self.assertEqual(baseline["generated_outcomes"], metrics["generated_files"]["rewritten_generated_outcomes"])
        self.assertEqual(baseline["critical_invariants"], {
            "passing": metrics["invariants"]["passing_critical_gates"],
            "total": metrics["invariants"]["critical_gates"],
        })
        self.assertEqual(baseline["hotspots"]["ranked_manual_ids"], ranked_ids)
        self.assertEqual(baseline["longitudinal_conflict_rate"], "not-recorded")
        self.assertEqual(sorted(ranked_ids), sorted(self.manual_by_owner))
        self.assertIn("roots-knots-policy-local-policy", baseline["ownership"]["cohesive_queue_ids"])
        self.assertIn("roots-roots-common-maintenance", baseline["ownership"]["scattered_ids"])

    def test_queue_has_exhaustive_manifest_owned_manual_paths_and_surfaces(self):
        queue_ids = []
        for item in self.hotspots["queue"]:
            self.assertEqual(len(item["adaptation_ids"]), 1)
            adaptation_id = item["adaptation_ids"][0]
            queue_ids.append(adaptation_id)
            self.assertEqual(sorted(item["conflict_paths"]), self.manual_by_owner[adaptation_id])
            self.assertEqual(item["conflict_frequency"]["manual_paths"], len(self.manual_by_owner[adaptation_id]))
            self.assertEqual(item["conflict_frequency"]["migration_samples"], 1)
            self.assert_surface(adaptation_id, item["diff_surface"])
            for path in item["conflict_paths"]:
                self.assertEqual(self.owners[path], [adaptation_id])

        expected_order = sorted(self.manual_by_owner, key=lambda unit_id: (
            -len(self.manual_by_owner[unit_id]), -self.outcome_by_id[unit_id]["changed_paths"], unit_id))
        self.assertEqual(queue_ids, expected_order)
        self.assertEqual(len(queue_ids), len(set(queue_ids)))

    def test_accounting_is_set_equal_and_duplicate_free(self):
        recorded = [adaptation_id for item in self.hotspots["queue"] + self.hotspots["leave_inline"]
                    for adaptation_id in item["adaptation_ids"]]
        self.assertEqual(len(recorded), len(set(recorded)))
        self.assertEqual(set(recorded), self.manifest_ids)
        self.assertEqual({item["adaptation_ids"][0] for item in self.hotspots["queue"]}, set(self.manual_by_owner))

    def test_seams_keep_each_owner_and_surface_explicit(self):
        required = {"adaptation_ids", "current_paths", "evidence_by_adaptation", "contract", "ownership",
                    "dependency_direction", "migration_stages", "characterization_tests",
                    "policy_consensus_protection", "expected_conflict_reduction", "performance_resource_gates",
                    "rollback", "dependencies_risks", "leave_inline_alternative"}
        for seam in self.seams["seams"]:
            self.assertTrue(required <= set(seam))
            self.assertEqual(len(seam["adaptation_ids"]), len(set(seam["adaptation_ids"])))
            self.assertEqual(set(seam["adaptation_ids"]), {entry["adaptation_id"] for entry in seam["evidence_by_adaptation"]})
            evidence_paths = set()
            for evidence in seam["evidence_by_adaptation"]:
                adaptation_id = evidence["adaptation_id"]
                self.assertIn(adaptation_id, self.manifest_ids)
                self.assert_surface(adaptation_id, evidence["diff_surface"])
                self.assertEqual(sorted(evidence["manual_paths"]), self.manual_by_owner.get(adaptation_id, []))
                for path in evidence["manual_paths"]:
                    self.assertEqual(self.owners[path], [adaptation_id])
                evidence_paths.update(evidence["manual_paths"])
            for path in seam["current_paths"]:
                self.assertTrue((ROOT / path).exists())
                self.assertEqual(len(self.owners[path]), 1)
                self.assertIn(self.owners[path][0], seam["adaptation_ids"])
            self.assertTrue(evidence_paths or seam["decision"] == "propose")

        policy_seam = self.seams["seams"][1]
        self.assertIn("roots-knots-p2p-networking", policy_seam["adaptation_ids"])
        self.assertIn("roots-knots-policy-local-policy", policy_seam["adaptation_ids"])
        self.assertIn("must not alter block validity", policy_seam["policy_consensus_protection"])
        self.assertIn("RDTS/BIP110 consensus enforcement remains absent", policy_seam["policy_consensus_protection"])

    def test_normalization_cases_execute_identity_contract(self):
        fixture = load_json("ci/test/data/roots_delta_budget_normalization_fixture.json")
        for case in fixture["cases"]:
            ids = [event["adaptation_id"] for event in case["events"]]
            self.assertTrue(set(ids) <= self.manifest_ids)
            normalized_ids = sorted(set(ids))
            self.assertEqual(normalized_ids, case["expected_normalized_ids"])
            if case["kind"] == "generated":
                self.assertFalse(case["counts_toward_scattering"])
            elif case["kind"] == "scattered":
                self.assertTrue(case["requires_review"])
                self.assertTrue(case["counts_toward_scattering"])
            elif case["kind"] == "absorbed":
                self.assertTrue(all(event["reviewed_absorption"] for event in case["events"]))
                self.assertFalse(case["counts_toward_scattering"])
            else:
                self.assertEqual(case["counts_toward_scattering"], len(normalized_ids) > 1)
            for event in case["events"]:
                if "historical_path" in event:
                    self.assertEqual(self.owners[event["historical_path"]], [event["adaptation_id"]])

    def test_identity_fixture_uses_manifest_lineage_not_labels(self):
        fixture = load_json("ci/test/data/roots_portability_hotspots_identity_fixture.json")
        for case in fixture["cases"]:
            adaptation_id = case["origin_adaptation_id"]
            self.assertIn(adaptation_id, self.manifest_ids)
            for historical_path in case["historical_paths"]:
                self.assertEqual(self.owners[historical_path], [adaptation_id])
            self.assertEqual(case["expected_adaptation_id"], adaptation_id)
        self.assertNotEqual(fixture["cases"][0]["historical_paths"][0], fixture["cases"][0]["replayed_path"])
        self.assertNotEqual(fixture["cases"][2]["historical_paths"][0], fixture["cases"][2]["replayed_path"])

    def test_upstream_dispositions_are_traceable_and_absorption_requires_evidence(self):
        record = load_json("contrib/roots/upstream-dispositions-29.4.json")
        fixture = load_json("ci/test/data/roots_upstream_absorption_fixture.json")
        contract = record["lifecycle_contract"]
        item = fixture["disposition"]
        required = {"adaptation_ids", "category", "rationale", "tests", "roots_provenance",
                    "public_upstream_link", "retirement_condition", "owner", "lifecycle", "retirement_state"}
        roots_only = {"knots-origin-feature", "roots-default"}
        self.assertEqual({item["category"] for item in record["dispositions"]}, {
            "generic-fix", "knots-origin-feature", "roots-default", "test-improvement", "build-fix"})
        self.assertIn("RDTS/BIP110 enforcement remains absent", record["rules"]["consensus_rule"])
        self.assertIn("outside public manifests", record["rules"]["embargo_rule"])
        self.assertEqual(contract["legal_transitions"], {"active": ["submitted"], "submitted": ["absorbed"]})
        for disposition in record["dispositions"]:
            self.assertTrue(required <= set(disposition))
            self.assertTrue(set(disposition["adaptation_ids"]) <= self.manifest_ids)
            self.assertEqual(disposition["retirement_state"], contract["retirement_states"][disposition["lifecycle"]])
            if disposition["category"] in roots_only:
                self.assertEqual(disposition["public_upstream_link"], "not-applicable")
            elif disposition["lifecycle"] == "active":
                self.assertEqual(disposition["public_upstream_link"], "not-submitted")
            else:
                parsed = urlparse(disposition["public_upstream_link"])
                self.assertEqual(parsed.scheme, "https")
                self.assertTrue(parsed.netloc)
            self.assertTrue(disposition["tests"])
            self.assertNotIn("embargoed", disposition)
        self.assertNotIn("confidential-security", {entry["category"] for entry in record["dispositions"]})
        self.assertTrue(fixture["synthetic"])
        self.assertEqual(item["retirement_state"], contract["retirement_states"]["absorbed"])
        evidence = item["reviewed_absorption_evidence"]
        self.assertEqual(evidence["local_adaptation_id"], item["adaptation_ids"][0])
        self.assertEqual(evidence["upstream_change"], item["public_upstream_link"])
        parsed = urlparse(item["public_upstream_link"])
        self.assertEqual(parsed.scheme, "https")
        self.assertTrue(parsed.netloc)
        missing_evidence = dict(item)
        missing_evidence.pop("reviewed_absorption_evidence")
        self.assertFalse(all(key in missing_evidence for key in contract["absorbed_requires"]))
        tabletop = fixture["confidential_tabletop"]
        self.assertTrue(tabletop["synthetic"])
        self.assertEqual(tabletop["lifecycle"], "outside-public-manifest")
        self.assertEqual(tabletop["public_upstream_link"], "not-applicable")
        self.assertFalse({"adaptation_ids", "affected_paths", "tests", "reproducer", "private_link"} & set(tabletop))

    def test_review_triggers_remain_corrected_and_policy_safe(self):
        triggers = {trigger["id"]: trigger for trigger in self.budget["review_triggers"]}
        self.assertEqual(set(triggers), {"manual-hotspot", "generated-noise", "policy-boundary", "scattering"})
        self.assertEqual(triggers["manual-hotspot"]["adaptation_ids"], self.budget["baseline"]["manual_adaptation_units"])
        self.assertTrue(all(trigger["action"] for trigger in triggers.values()))
        self.assertTrue(all(set(trigger["adaptation_ids"]) <= self.manifest_ids for trigger in triggers.values()))
        self.assertEqual(triggers["policy-boundary"]["adaptation_ids"], ["roots-knots-policy-local-policy"])
        self.assertIn("RDTS/BIP110 enforcement remains absent", triggers["policy-boundary"]["invariant"])
        self.assertIn("outside block validity", triggers["policy-boundary"]["invariant"])


if __name__ == "__main__":
    unittest.main()
