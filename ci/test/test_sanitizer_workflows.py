#!/usr/bin/env python3
"""Regression contracts for shared PR and nightly sanitizer execution."""

import importlib.util
import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
CI_WORKFLOW = ROOT / ".github/workflows/ci.yml"
NIGHTLY_WORKFLOW = ROOT / ".github/workflows/nightly.yml"
REUSABLE_WORKFLOW = ROOT / ".github/workflows/reusable-sanitizer.yml"
CLASSIFIER = ROOT / "ci/change-classifier.py"
POLICY = ROOT / "ci/change-classifier-policy.json"


def job(workflow, name):
    text = workflow.read_text(encoding="utf-8")
    match = re.search(rf"(?ms)^  {re.escape(name)}:\n(.*?)(?=^  [a-z0-9_-]+:\n|\Z)", text)
    if match is None:
        raise AssertionError(f"Job {name!r} does not exist in {workflow}")
    return match.group(1)


class SanitizerWorkflowTest(unittest.TestCase):
    def test_reusable_workflow_owns_the_sanitizer_runner(self):
        workflow = REUSABLE_WORKFLOW.read_text(encoding="utf-8")
        for input_name in ("ref", "name", "file-env", "timeout", "cache-save", "enable-usdt", "configure-aslr"):
            self.assertIn(f"      {input_name}:", workflow)
        self.assertIn("permissions:\n  actions: read\n  contents: read", workflow)
        self.assertIn("persist-credentials: false", workflow)
        self.assertIn("save: ${{ inputs.cache-save }}", workflow)
        self.assertIn("if: inputs.enable-usdt", workflow)
        self.assertIn("if: inputs.configure-aslr", workflow)
        for shared_step in (
            "Configure environment",
            "Restore compiler cache",
            "Restore caches",
            "Configure Docker",
            "Run sanitizer coverage",
        ):
            self.assertEqual(workflow.count(shared_step), 1)

    def test_pr_asan_remains_selected_and_untrusted(self):
        workflow = CI_WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("permissions:\n  actions: read\n  contents: read\n  pull-requests: read", workflow)
        sanitizers = job(CI_WORKFLOW, "sanitizers")
        self.assertIn("needs.classify.outputs.sanitizers == 'true'", sanitizers)
        self.assertIn("uses: ./.github/workflows/reusable-sanitizer.yml", sanitizers)
        self.assertIn("ref: ${{ github.event.pull_request.head.sha }}", sanitizers)
        self.assertIn("name: ASan, LSan, UBSan, integer, wallet, GUI, and USDT", sanitizers)
        self.assertIn("file-env: ./ci/test/00_setup_env_native_asan.sh", sanitizers)
        self.assertIn("timeout: 120", sanitizers)
        self.assertIn("cache-save: false", sanitizers)
        self.assertIn("enable-usdt: true", sanitizers)
        self.assertIn("configure-aslr: false", sanitizers)
        self.assertNotIn("Restore compiler cache", sanitizers)
        self.assertNotIn("Enable USDT coverage", sanitizers)

    def test_nightly_sanitizer_matrices_and_failure_aggregation_remain_exact(self):
        workflow = NIGHTLY_WORKFLOW.read_text(encoding="utf-8")
        for expected in (
            "all) value=\"${all}\"; sanitizer_value=\"${sanitizers}\"",
            "tsan) value='{\"include\":[]}'; sanitizer_value='{\"include\":[{\"name\":\"ThreadSanitizer\",\"file-env\":\"./ci/test/00_setup_env_native_tsan.sh\",\"timeout\":240,\"configure-aslr\":true}]}'",
            "msan) value='{\"include\":[]}'; sanitizer_value='{\"include\":[{\"name\":\"MemorySanitizer\",\"file-env\":\"./ci/test/00_setup_env_native_msan.sh\",\"timeout\":240,\"configure-aslr\":true}]}'",
            "sanitizers) value='{\"include\":[]}'; sanitizer_value=\"${sanitizers}\"",
        ):
            self.assertIn(expected, workflow)

        sanitizers = job(NIGHTLY_WORKFLOW, "sanitizers")
        self.assertIn("strategy:\n      fail-fast: false", sanitizers)
        self.assertIn("matrix: ${{ fromJSON(needs.gate.outputs.sanitizer-matrix) }}", sanitizers)
        self.assertIn("uses: ./.github/workflows/reusable-sanitizer.yml", sanitizers)
        self.assertIn("enable-usdt: false", sanitizers)
        self.assertIn("configure-aslr: ${{ matrix.configure-aslr }}", sanitizers)

        required_result = job(CI_WORKFLOW, "required-result")
        self.assertIn("sanitizers", required_result.split("needs:", 1)[1].split("runs-on:", 1)[0])
        self.assertIn("SANITIZERS: ${{ needs.sanitizers.result }}", required_result)
        self.assertIn("check_selected \"$SANITIZERS_SELECTED\" \"$SANITIZERS\" sanitizers", required_result)

    def test_cpp_and_ci_sanitizer_selection_contracts_are_preserved(self):
        spec = importlib.util.spec_from_file_location("change_classifier", CLASSIFIER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        policy = json.loads(POLICY.read_text(encoding="utf-8"))

        cpp_result = module.result_for(["src/wallet/wallet.cpp"], [], False, False, policy)
        self.assertTrue(cpp_result["selected"]["sanitizers"])

        label_result = module.result_for(["README.md"], ["ci:sanitizers"], False, False, policy)
        self.assertTrue(label_result["selected"]["sanitizers"])
        self.assertTrue(label_result["selected"]["nightly_sanitizers"])


if __name__ == "__main__":
    unittest.main()
