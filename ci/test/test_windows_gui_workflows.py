#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
CI_WORKFLOW = ROOT / ".github/workflows/ci.yml"
RELEASE_WORKFLOW = ROOT / ".github/workflows/release.yml"


def job(workflow, name):
    text = workflow.read_text(encoding="utf-8")
    match = re.search(rf"(?ms)^  {re.escape(name)}:\n(.*?)(?=^  [a-z0-9_-]+:\n|\Z)", text)
    if match is None:
        raise AssertionError(f"Job {name!r} does not exist in {workflow}")
    return match.group(1)


class WindowsGuiWorkflowTest(unittest.TestCase):
    def test_pr_smoke_builds_and_tests_windows_gui(self):
        windows_smoke = job(CI_WORKFLOW, "windows-smoke")
        self.assertIn("-DBUILD_GUI=ON", windows_smoke)
        self.assertNotIn("-DBUILD_GUI=OFF", windows_smoke)
        self.assertIn("-DBUILD_TESTS=ON", windows_smoke)
        self.assertIn("ctest --test-dir build -C Release --output-on-failure", windows_smoke)

    def test_release_builds_and_packages_windows_gui(self):
        windows_release = job(RELEASE_WORKFLOW, "windows-x86_64-release")
        self.assertIn("-DBUILD_GUI=ON", windows_release)
        self.assertNotIn("-DBUILD_GUI=OFF", windows_release)
        self.assertIn('"bin\\bitcoin-qt.exe"', windows_release)
        self.assertIn("Test-Path -PathType Leaf $gui", windows_release)


if __name__ == "__main__":
    unittest.main()
