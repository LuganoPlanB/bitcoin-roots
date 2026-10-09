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
        self.assertNotIn("fuzz", result["selected"])
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = pathlib.Path(temporary_directory) / "github-output"
            MODULE.write_github_output(result, output)
            self.assertEqual(
                output.read_text(encoding="utf-8"),
                "baseline=true\nblock_compatibility=false\ncompat=false\ndocs=true\ngui=false\n"
                "nightly_full=false\nnightly_fuzz=true\nnightly_platforms=false\nnightly_sanitizers=false\nplatforms=false\nsanitizers=false\nwallet=false\nbroad=false\n"
                "complete=true\ncategories=[\"docs-only\"]\nlabels=[\"ci:fuzz\"]\n"
                "result={\"broad\":false,\"categories\":[\"docs-only\"],\"complete\":true,"
                "\"labels\":[\"ci:fuzz\"],\"selected\":{\"baseline\":true,\"block_compatibility\":false,\"compat\":false,"
                "\"docs\":true,\"gui\":false,\"nightly_full\":false,\"nightly_fuzz\":true,\"nightly_platforms\":false,\"nightly_sanitizers\":false,\"platforms\":false,"
                "\"sanitizers\":false,\"wallet\":false},\"version\":1}\n")

    def test_full_label_selects_reusable_nightly_assurance(self):
        result = MODULE.result_for(["README.md"], ["ci:full"], False, False, self.policy)
        self.assertTrue(result["selected"]["nightly_full"])
        self.assertFalse(any(result["selected"][name] for name in (
            "nightly_sanitizers", "nightly_fuzz", "nightly_platforms")))

    def test_individual_extended_labels_select_their_nightly_groups(self):
        for label, selection in (("ci:sanitizers", "nightly_sanitizers"),
                                 ("ci:fuzz", "nightly_fuzz"),
                                 ("ci:platforms", "nightly_platforms")):
            with self.subTest(label=label):
                result = MODULE.result_for(["README.md"], [label], False, False, self.policy)
                self.assertTrue(result["selected"][selection])

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

    def test_matching_core_selection_is_distinct_and_additive(self):
        for path in ("src/policy/policy.cpp", "src/validation.cpp", "src/kernel/chain.cpp", "src/consensus/tx_verify.cpp",
                     "src/script/interpreter.cpp", "src/primitives/transaction.cpp", "src/txrequest.cpp",
                     "contrib/roots/compatibility/compare.py", ".github/workflows/reusable-compatibility.yml"):
            with self.subTest(path=path):
                self.assertTrue(MODULE.result_for([path], [], False, False, self.policy)["selected"]["block_compatibility"])
        self.assertFalse(MODULE.result_for(["doc/build-unix.md"], [], False, False, self.policy)["selected"]["block_compatibility"])
        for label in ("ci:compat", "ci:full"):
            result = MODULE.result_for(["README.md"], [label], False, False, self.policy)
            self.assertTrue(result["selected"]["compat"])
            self.assertTrue(result["selected"]["block_compatibility"])

    def test_rename_and_incomplete_inventories_request_comparison(self):
        for files, truncated, error in ((["doc/new.md", "src/policy/old.cpp"], False, False),
                                        (["README.md"], True, False), (["README.md"], False, True),
                                        ([], False, False), (None, False, False)):
            with self.subTest(files=files, truncated=truncated, error=error):
                self.assertTrue(MODULE.result_for(files, [], truncated, error, self.policy)["selected"]["block_compatibility"])


if __name__ == "__main__":
    unittest.main()
