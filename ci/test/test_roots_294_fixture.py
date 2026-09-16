#!/usr/bin/env python3
"""Focused contract tests for the bounded Core 29.4 migration fixture."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "contrib/devtools/roots-294-fixture.py"
FIXTURE = ROOT / "contrib/roots/core-29.4-migration-fixture.json"


def load_module():
    spec = importlib.util.spec_from_file_location("roots_294_fixture", SCRIPT)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


ORACLE = load_module()


class Core294FixtureTest(unittest.TestCase):
    def setUp(self):
        self.fixture = json.loads(FIXTURE.read_text(encoding="utf-8"))

    def test_bounded_paths_are_complete_and_unique(self):
        ORACLE.validate(self.fixture)
        paths = ORACLE.fixture_paths(self.fixture)
        self.assertEqual(len(paths), 39)
        self.assertEqual(self.fixture["snapshot_observed_path_count"], len(paths) + 1)

    def test_snapshot_only_identity_expansion_is_not_promoted_to_git_delta(self):
        self.assertEqual(self.fixture["snapshot_only"]["path"], "src/clientversion.cpp")
        self.assertNotIn("src/clientversion.cpp", ORACLE.fixture_paths(self.fixture))

    def test_conservative_outcome_ledger_covers_each_git_path_once(self):
        outcomes = self.fixture["expected_outcomes"]
        self.assertEqual({name: len(paths) for name, paths in outcomes.items()}, {"exact": 14, "absorbed": 1, "clean-textual": 12, "generated": 6, "manual": 6})
        self.assertIn("src/validation.cpp", outcomes["clean-textual"])
        self.assertIn("src/txrequest.cpp", outcomes["manual"])

    def test_manual_records_constrain_but_do_not_preapprove_a_resolution(self):
        conflicts = self.fixture["manual_conflicts"]
        self.assertEqual(len(conflicts), 7)
        self.assertTrue(all("resolution" not in conflict and conflict["alternatives"] for conflict in conflicts))
        self.assertIn("src/clientversion.cpp", {conflict["path"] for conflict in conflicts})

    def test_generated_outputs_have_one_deferred_candidate_recipe(self):
        generated = self.fixture["generated_outputs"]
        self.assertEqual(generated["outputs"], self.fixture["expected_outcomes"]["generated"])
        self.assertIn("candidate tag substitution required", generated["identity"])

    def test_acceptance_contract_retains_platforms_and_roots_invariants(self):
        acceptance = self.fixture["acceptance"]
        self.assertEqual(acceptance["platforms"], ["Linux", "macOS", "Windows"])
        self.assertIn("manual", acceptance["decision_statuses"])
        self.assertTrue(any("RDTS/BIP110" in gate for gate in acceptance["gates"]))

    def test_duplicate_or_missing_git_path_fails_closed(self):
        duplicate = copy.deepcopy(self.fixture)
        duplicate["units"][0]["paths"].append("CMakeLists.txt")
        with self.assertRaises(ORACLE.FixtureError):
            ORACLE.validate(duplicate)
        missing = copy.deepcopy(self.fixture)
        missing["units"][0]["paths"].pop()
        with self.assertRaises(ORACLE.FixtureError):
            ORACLE.validate(missing)

    def test_archive_discrepancy_and_tag_inputs_are_required(self):
        stale = copy.deepcopy(self.fixture)
        stale["snapshot_only"]["reason"] = "ignored"
        with self.assertRaises(ORACLE.FixtureError):
            ORACLE.validate(stale)
        stale = copy.deepcopy(self.fixture)
        stale["expected_outcomes"]["manual"].remove("src/txrequest.cpp")
        with self.assertRaises(ORACLE.FixtureError):
            ORACLE.validate(stale)
        stale = copy.deepcopy(self.fixture)
        stale["manual_conflicts"][0]["resolution"] = "take upstream"
        with self.assertRaises(ORACLE.FixtureError):
            ORACLE.validate(stale)
        stale = copy.deepcopy(self.fixture)
        stale["generated_outputs"]["outputs"] = []
        with self.assertRaises(ORACLE.FixtureError):
            ORACLE.validate(stale)
        stale = copy.deepcopy(self.fixture)
        stale["acceptance"]["platforms"] = ["Linux"]
        with self.assertRaises(ORACLE.FixtureError):
            ORACLE.validate(stale)
        stale = copy.deepcopy(self.fixture)
        del stale["core_inputs"]["v29.4"]["tree"]
        with self.assertRaises(ORACLE.FixtureError):
            ORACLE.validate(stale)


if __name__ == "__main__":
    unittest.main()
