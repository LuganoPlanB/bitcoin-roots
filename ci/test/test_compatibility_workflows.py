# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.

"""Exercise selected-result shells and the fail-closed release dependency graph."""

import os
from pathlib import Path
import re
import subprocess
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]


def job(file, name):
    text = (ROOT / ".github/workflows" / file).read_text()
    match = re.search(rf"(?ms)^  {re.escape(name)}:\n(.*?)(?=^  [a-z0-9_-]+:\n|\Z)", text)
    if match is None:
        raise AssertionError("missing workflow job " + name)
    return match[1]


def needs(body):
    match = re.search(r"(?m)^    needs: (.*)$", body)
    return [part.strip() for part in match[1].strip("[]").split(",")] if match else []


def shell(body, environment):
    blocks = re.findall(r"(?m)^        run: \|\n((?:          .*\n|\n)+)", body)
    if len(blocks) != 1:
        raise AssertionError("expected one aggregation shell")
    return subprocess.run(["bash", "-c", textwrap.dedent(blocks[0])],
                          env={**os.environ, **environment}, capture_output=True).returncode


class CompatibilityWorkflowsTest(unittest.TestCase):
    def test_pr_runs_harness_failure_path_regressions(self):
        classify = job("ci.yml", "classify")
        self.assertRegex(classify, r"(?m)^          python3 ci/test/test_roots_compatibility\.py$")

    def test_all_workflow_environment_profiles_exist(self):
        for workflow in (ROOT / ".github/workflows").glob("*.yml"):
            profiles = set(re.findall(r"ci/test/00_setup_env_[A-Za-z0-9_-]+\.sh", workflow.read_text()))
            for profile in profiles:
                with self.subTest(workflow=workflow.name, profile=profile):
                    self.assertTrue((ROOT / profile).is_file())
        nightly = job("nightly.yml", "gate")
        self.assertEqual(nightly.count("00_setup_env_i686_no_ipc.sh"), 2)
        self.assertIn('"name":"i686 Debug"', nightly)
        profile = (ROOT / "ci/test/00_setup_env_i686_no_ipc.sh").read_text()
        self.assertIn("HOST=i686-pc-linux-gnu", profile)
        self.assertIn("DEBUG=1 NO_IPC=1", profile)
        self.assertIn("-DCMAKE_BUILD_TYPE=Debug", profile)

    def test_pr_selection_and_immutable_inputs(self):
        lane = job("ci.yml", "block-compatibility")
        self.assertEqual(needs(lane), ["classify"])
        self.assertIn("needs.classify.outputs.block_compatibility == 'true'", lane)
        self.assertIn("needs.classify.outputs.nightly_full != 'true'", lane)
        self.assertIn("profile: smoke", lane)
        self.assertIn("candidate-sha: ${{ needs.classify.outputs.compatibility_candidate }}", lane)
        self.assertIn("core-sha: ${{ needs.classify.outputs.compatibility_core }}", lane)
        classify = job("ci.yml", "classify")
        self.assertIn("CANDIDATE_SHA: ${{ github.event.pull_request.head.sha }}", classify)
        self.assertIn("ci/compatibility.py inputs", classify)
        self.assertIn("previous_filename", classify)
        self.assertIn("--error --truncated", classify)

    def test_required_pr_comparison_fails_for_all_nonsuccess_results(self):
        aggregate = job("ci.yml", "required-result")
        self.assertIn("block-compatibility", needs(aggregate))
        environment = {name: "success" for name in (
            "CLASSIFY", "LINT", "GUI", "SANITIZERS", "COMPAT", "BLOCK_COMPATIBILITY", "ARM32", "KERNEL",
            "WINDOWS", "MACOS", "NIGHTLY_SANITIZERS", "NIGHTLY_FUZZ", "NIGHTLY_PLATFORMS", "NIGHTLY_FULL")}
        environment.update({name: "false" for name in (
            "GUI_SELECTED", "SANITIZERS_SELECTED", "COMPAT_SELECTED", "BLOCK_COMPATIBILITY_SELECTED", "PLATFORMS_SELECTED",
            "NIGHTLY_SANITIZERS_SELECTED", "NIGHTLY_FUZZ_SELECTED", "NIGHTLY_PLATFORMS_SELECTED", "NIGHTLY_FULL_SELECTED")})
        for result in ("failure", "cancelled", "skipped", ""):
            for selected in ("true", "false"):
                with self.subTest(result=result, selected=selected):
                    actual = shell(aggregate, {**environment, "BLOCK_COMPATIBILITY_SELECTED": selected,
                                                "BLOCK_COMPATIBILITY": result})
                    self.assertEqual(actual == 0, selected == "false")

    def test_full_label_deduplicates_and_previous_release_lane_survives(self):
        full = job("ci.yml", "nightly-full")
        self.assertIn("suite: all", full)
        self.assertIn("context-ref: ${{ needs.classify.outputs.compatibility_context }}", full)
        self.assertIn("00_setup_env_native_previous_releases.sh", job("ci.yml", "compat"))
        self.assertIn("previous-release extended compatibility", job("nightly.yml", "gate"))

    def test_nightly_full_and_selectable_lane_are_gated(self):
        lane = job("nightly.yml", "block-compatibility")
        self.assertIn("inputs.suite == 'all'", lane)
        self.assertIn("inputs.suite == 'block-compatibility'", lane)
        self.assertIn("needs.gate.outputs.run == 'true'", lane)
        self.assertIn("profile: full", lane)
        self.assertIn("core-sha: ${{ needs.gate.outputs.compatibility-core }}", lane)
        self.assertIn("candidate-sha: ${{ needs.gate.outputs.compatibility-candidate }}", lane)
        gate = job("nightly.yml", "gate")
        self.assertIn("fetch-depth: 0", gate)
        self.assertIn("ci/compatibility.py inputs", gate)
        self.assertIn("block-compatibility|fuzz|macos-fuzz|windows-fuzz)", gate)
        # Existing change gate guarantees manual/workflow_call requests run,
        # even when the scheduled prior SHA is unchanged.
        self.assertIn("--event-name", gate)

    def test_nightly_aggregate_rejects_missing_or_skipped_required_comparison(self):
        aggregate = job("nightly.yml", "compatibility-result")
        self.assertEqual(needs(aggregate), ["gate", "block-compatibility"])
        for result in ("success", "failure", "cancelled", "skipped", ""):
            with self.subTest(result=result):
                self.assertEqual(shell(aggregate, {"GATE": "success", "RUN": "true", "SELECTED": "true", "RESULT": result}) == 0,
                                 result == "success")
        self.assertEqual(shell(aggregate, {"GATE": "success", "RUN": "false", "SELECTED": "true", "RESULT": "skipped"}), 0)
        self.assertEqual(shell(aggregate, {"GATE": "success", "RUN": "true", "SELECTED": "false", "RESULT": "skipped"}), 0)
        self.assertNotEqual(shell(aggregate, {"GATE": "failure", "RUN": "", "SELECTED": "true", "RESULT": "skipped"}), 0)

    def test_release_candidate_and_core_are_resolved_after_source_validation(self):
        metadata = job("release.yml", "release-metadata-tests")
        self.assertLess(metadata.index("ci/release/prepare-release-source.sh"), metadata.index("ci/compatibility.py inputs"))
        self.assertIn("ref: ${{ env.RELEASE_SOURCE_REF }}", metadata)
        self.assertIn("candidate=$(git rev-parse 'HEAD^{commit}')", metadata)
        lane = job("release.yml", "block-compatibility")
        self.assertEqual(needs(lane), ["release-metadata-tests"])
        self.assertIn("profile: full", lane)
        self.assertIn("candidate-sha: ${{ needs.release-metadata-tests.outputs.compatibility-candidate }}", lane)
        self.assertIn("core-sha: ${{ needs.release-metadata-tests.outputs.compatibility-core }}", lane)
        self.assertIn("release-tag: ${{ github.event_name == 'push' && github.ref_name || '' }}", lane)

    def test_release_platforms_and_draft_cannot_bypass_compatibility(self):
        for name in ("linux-release", "windows-release", "macos-release", "publish-release"):
            body = job("release.yml", name)
            with self.subTest(job=name):
                self.assertIn("block-compatibility", needs(body))
                self.assertIn("release-metadata-tests", needs(body))
                self.assertNotIn("always()", body)
        publish = job("release.yml", "publish-release")
        self.assertIn("if: github.event_name == 'push'", publish)
        self.assertIn("environment: release", publish)
        self.assertIn("contents: write", publish)
        self.assertIn("ci/release/create-ci-draft.py", publish)
        lane = job("release.yml", "block-compatibility")
        self.assertNotIn("secrets", lane)
        self.assertNotIn("contents: write", lane)


if __name__ == "__main__":
    unittest.main()
