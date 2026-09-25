#!/usr/bin/env python3
"""Regression fixtures for ci/nightly-change-gate.py."""

import importlib.util
import json
import pathlib
import unittest


CI_ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = CI_ROOT / "nightly-change-gate.py"
FIXTURES = pathlib.Path(__file__).with_name("fixtures") / "nightly-change-gate.json"
SPEC = importlib.util.spec_from_file_location("nightly_change_gate", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class NightlyChangeGateTest(unittest.TestCase):
    def test_fixtures(self):
        for fixture in json.loads(FIXTURES.read_text(encoding="utf-8")):
            with self.subTest(fixture=fixture["name"]):
                self.assertEqual(
                    MODULE.should_run(
                        fixture["event"], fixture["current_run_id"], fixture["current_sha"], fixture["history"]),
                    fixture["expected"])


if __name__ == "__main__":
    unittest.main()
