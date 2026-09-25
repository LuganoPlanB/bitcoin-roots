#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from pathlib import Path
import re
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
BUILD_CONFIG = ROOT / "CMakeLists.txt"
ARCHIVE_TOOL = ROOT / "ci/release/archive.py"
RELEASE_WORKFLOW = ROOT / ".github/workflows/release.yml"


class ReleaseVersionTest(unittest.TestCase):
    def test_l7_version_and_release_tag_name_agree(self):
        build_config = BUILD_CONFIG.read_text(encoding="utf-8")
        self.assertIn("set(CLIENT_VERSION_MAJOR 29)", build_config)
        self.assertIn("set(CLIENT_VERSION_MINOR 4)", build_config)
        self.assertIn('string(APPEND CLIENT_VERSION_STRING "-roots.1")', build_config)
        result = subprocess.run(
            ["python3", ARCHIVE_TOOL, "root-name", "--tag", "v29.4-roots.1"],
            capture_output=True, text=True, check=True,
        )
        self.assertEqual(result.stdout.strip(), "bitcoin-roots-29.4-roots.1")

    def test_workflow_builds_tags_and_manual_dry_runs_from_the_tag(self):
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("tags:", workflow)
        self.assertIn("'v[0-9]*-roots.[0-9]*'", workflow)
        self.assertIn("workflow_dispatch:", workflow)
        self.assertNotIn("branches:", workflow)
        self.assertIn("github.event_name == 'push' && github.ref_name || inputs.release_tag", workflow)
        self.assertIn("ref: refs/tags/${{ env.RELEASE_TAG }}", workflow)
        self.assertIn("if: github.event_name == 'push'", workflow)
        self.assertIn('ci/release/validate-release-source.sh "$RELEASE_TAG"', workflow)
        self.assertIn('refs/heads/roots/${core_version}:refs/remotes/origin/roots/${core_version}', workflow)
        self.assertNotIn("origin main:refs/remotes/origin/main", workflow)
        self.assertIn("environment: release", workflow)

    def test_workflow_builds_complete_artifact_matrix_and_patch(self):
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        for artifact in [
            "bitcoin-roots-linux-x86_64",
            "bitcoin-roots-linux-aarch64",
            "bitcoin-roots-darwin-x86_64",
            "bitcoin-roots-darwin-arm64",
            "bitcoin-roots-windows-x86_64",
        ]:
            self.assertIn(artifact, workflow)
        self.assertIn("EXPECTED_PACKAGE_COUNT: 5", workflow)
        self.assertIn("ci/release/create-patch-series.sh", workflow)
        self.assertIn("ci/release/prepare-release.sh", workflow)
        self.assertIn("ci/release/sign-manifest.sh", workflow)
        self.assertIn("SHA512SUMS", workflow)
        self.assertIn("gh release upload", workflow)
        self.assertIn("gh release create", workflow)

    def test_workflow_references_only_present_local_release_files(self):
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        local_paths = re.findall(r"(?:ci|contrib)/[A-Za-z0-9_./-]+\.(?:asc|py|sh)", workflow)
        for local_path in local_paths:
            with self.subTest(local_path=local_path):
                self.assertTrue((ROOT / local_path).is_file())

    def test_workflow_actions_are_intentionally_bounded(self):
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        actions = set(re.findall(r"uses: (\S+)", workflow))
        self.assertEqual(
            actions,
            {
                "actions/checkout@v6",
                "actions/download-artifact@v5",
                "actions/upload-artifact@v4",
                "./.github/actions/configure-docker",
                "./.github/actions/configure-environment",
            },
        )


if __name__ == "__main__":
    unittest.main()
