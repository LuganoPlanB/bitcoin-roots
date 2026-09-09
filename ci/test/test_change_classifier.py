#!/usr/bin/env python3
"""Regression fixtures for ci/change-classifier.py."""

import contextlib
import importlib.util
import io
import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock


CI_ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = CI_ROOT / "change-classifier.py"
POLICY = CI_ROOT / "change-classifier-policy.json"
FIXTURES = pathlib.Path(__file__).with_name("fixtures") / "change-classifier.json"
SPEC = importlib.util.spec_from_file_location("change_classifier", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ChangeClassifierTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.policy = json.loads(POLICY.read_text(encoding="utf-8"))

    def test_fixtures(self):
        for fixture in json.loads(FIXTURES.read_text(encoding="utf-8")):
            with self.subTest(fixture=fixture["name"]):
                result = MODULE.result_for(
                    fixture["files"], fixture["labels"],
                    fixture.get("truncated", False), fixture.get("error", False),
                    self.policy)
                self.assertEqual(fixture["expected"], {key: result[key] for key in fixture["expected"]})

    def test_github_output_is_stable(self):
        result = MODULE.result_for(["README.md"], ["ci:fuzz"], False, False, self.policy)
        self.assertEqual(result["labels"], ["ci:fuzz"])
        self.assertTrue(result["selected"]["fuzz"])
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = pathlib.Path(temporary_directory) / "github-output"
            MODULE.write_github_output(result, output)
            self.assertEqual(
                output.read_text(encoding="utf-8"),
                "baseline=true\ncompat=false\ndocs=true\nfuzz=true\ngui=false\n"
                "platforms=false\nsanitizers=false\nwallet=false\nbroad=false\n"
                "complete=true\ncategories=[\"docs-only\"]\nlabels=[\"ci:fuzz\"]\n"
                "result={\"broad\":false,\"categories\":[\"docs-only\"],\"complete\":true,"
                "\"labels\":[\"ci:fuzz\"],\"selected\":{\"baseline\":true,\"compat\":false,"
                "\"docs\":true,\"fuzz\":true,\"gui\":false,\"platforms\":false,"
                "\"sanitizers\":false,\"wallet\":false},\"version\":1}\n")

    def test_invalid_policy_fails_open_and_restores_path(self):
        original_policy_path = MODULE.POLICY_PATH
        with tempfile.TemporaryDirectory() as temporary_directory:
            policy_path = pathlib.Path(temporary_directory) / "invalid-policy.json"
            policy_path.write_text("{}", encoding="utf-8")
            output = io.StringIO()
            try:
                MODULE.POLICY_PATH = policy_path
                with mock.patch.object(sys, "argv", [str(SCRIPT), "--files-json", '["README.md"]']):
                    with contextlib.redirect_stdout(output):
                        MODULE.main()
            finally:
                MODULE.POLICY_PATH = original_policy_path
        result = json.loads(output.getvalue())
        self.assertEqual(MODULE.POLICY_PATH, original_policy_path)
        self.assertTrue(result["broad"])
        self.assertFalse(result["complete"])
        self.assertEqual(result["categories"], ["unknown"])


if __name__ == "__main__":
    unittest.main()
