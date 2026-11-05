#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
CI_WORKFLOW = ROOT / ".github/workflows/ci.yml"
NIGHTLY_WORKFLOW = ROOT / ".github/workflows/nightly.yml"


def job(workflow, name):
    text = workflow.read_text(encoding="utf-8")
    match = re.search(rf"(?ms)^  {re.escape(name)}:\n(.*?)(?=^  [a-z0-9_-]+:\n|\Z)", text)
    if match is None:
        raise AssertionError(f"Job {name!r} does not exist in {workflow}")
    return match.group(1)


class FuzzWorkflowTest(unittest.TestCase):
    def test_nightly_retains_diagnostics_for_success_and_failure(self):
        for workflow in (NIGHTLY_WORKFLOW, ROOT / '.github/workflows/reusable-sanitizer.yml'):
            uploads = re.findall(r'- name: Preserve [^\n]+\n        if: ([^\n]+)\n        uses: actions/upload-artifact@v4', workflow.read_text())
            self.assertTrue(uploads)
            for condition in uploads:
                self.assertIn('!cancelled()', condition)
                self.assertIn("env.ROOTS_ARTIFACTS_DIR != ''", condition)

    def test_failure_upload_uses_the_explicit_container_artifact_path(self):
        configure = (ROOT / '.github/actions/configure-environment/action.yml').read_text()
        self.assertIn('ROOTS_ARTIFACTS_DIR=${{ runner.temp }}/roots-failures', configure)
        self.assertIn('export ROOTS_ARTIFACTS_DIR=', (ROOT / 'ci/test/00_setup_env.sh').read_text())
        runner = (ROOT / 'ci/test/03_test_script.sh').read_text()
        self.assertEqual(runner.count('--artifacts "${ROOTS_ARTIFACTS_DIR:-${BASE_SCRATCH_DIR}/roots-failures}"'), 2)
        for workflow in (CI_WORKFLOW, NIGHTLY_WORKFLOW, ROOT / '.github/workflows/reusable-sanitizer.yml'):
            text = workflow.read_text()
            self.assertIn('${{ env.ROOTS_ARTIFACTS_DIR }}/', text)
            self.assertNotIn('${{ runner.temp }}/ci/scratch/roots-failures/', text)

    def test_full_linux_corpus_is_not_an_ordinary_pr_job(self):
        workflow = CI_WORKFLOW.read_text(encoding="utf-8")
        self.assertNotRegex(workflow, r"(?m)^  fuzz:$")
        self.assertNotIn("steps.classify.outputs.fuzz", workflow)

    def test_nightly_runs_linux_native_fuzz_corpus(self):
        linux_fuzz = job(NIGHTLY_WORKFLOW, "linux-fuzz")
        self.assertIn("inputs.suite == ''", linux_fuzz)
        self.assertIn("inputs.suite == 'all'", linux_fuzz)
        self.assertIn("inputs.suite == 'fuzz'", linux_fuzz)
        self.assertIn("FILE_ENV: ./ci/test/00_setup_env_native_fuzz.sh", linux_fuzz)
        self.assertIn("run: ./ci/test_run_all.sh", linux_fuzz)

    def test_ci_fuzz_label_requests_reusable_nightly_suite(self):
        nightly_fuzz = job(CI_WORKFLOW, "nightly-fuzz")
        self.assertIn("needs.classify.outputs.nightly_fuzz == 'true'", nightly_fuzz)
        self.assertIn("uses: ./.github/workflows/nightly.yml", nightly_fuzz)
        self.assertIn("suite: fuzz", nightly_fuzz)

        required_result = job(CI_WORKFLOW, "required-result")
        self.assertIn("nightly-fuzz", required_result)
        self.assertIn("check_selected \"$NIGHTLY_FUZZ_SELECTED\" \"$NIGHTLY_FUZZ\" nightly-fuzz", required_result)

    def test_macos_fuzz_waits_for_smoke_and_retains_explicit_selection(self):
        macos_fuzz = job(NIGHTLY_WORKFLOW, "macos-fuzz")
        self.assertIn('needs: [gate, macos-smoke]', macos_fuzz)
        # A status function overrides implicit success() when smoke is skipped,
        # while cancellation must still prevent a new expensive job starting.
        self.assertIn('!cancelled()', macos_fuzz)
        self.assertNotIn('always()', macos_fuzz)
        for suite in ['', 'all', 'fuzz', 'macos-fuzz']:
            self.assertIn(f"inputs.suite == '{suite}'", macos_fuzz)


if __name__ == "__main__":
    unittest.main()
