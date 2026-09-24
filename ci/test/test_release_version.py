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

    def test_workflow_is_tag_only_and_uses_the_checked_out_tag(self):
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("tags:", workflow)
        self.assertIn("'v[0-9]*-roots.[0-9]*'", workflow)
        self.assertNotIn("workflow_dispatch:", workflow)
        self.assertNotIn("branches:", workflow)
        self.assertIn("RELEASE_TAG: ${{ github.ref_name }}", workflow)
        self.assertIn('ci/release/validate-release-source.sh "$RELEASE_TAG"', workflow)
        self.assertIn("environment: release", workflow)

    def test_workflow_references_only_present_local_release_files(self):
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        local_paths = re.findall(r"(?:ci|contrib)/[A-Za-z0-9_./-]+", workflow)
        for local_path in local_paths:
            with self.subTest(local_path=local_path):
                self.assertTrue((ROOT / local_path).is_file())

    def test_workflow_external_actions_are_intentionally_first_party(self):
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        actions = set(re.findall(r"uses: (\S+)", workflow))
        self.assertEqual(
            actions,
            {
                "actions/checkout@v6",
                "actions/download-artifact@v5",
                "actions/upload-artifact@v4",
            },
        )


if __name__ == "__main__":
    unittest.main()
