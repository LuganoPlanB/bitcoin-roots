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
GUI_TOOLS_ACTION = ROOT / ".github/actions/setup-windows-gui-tools/action.yml"


def job(workflow, name):
    text = workflow.read_text(encoding="utf-8")
    match = re.search(rf"(?ms)^  {re.escape(name)}:\n(.*?)(?=^  [a-z0-9_-]+:\n|\Z)", text)
    if match is None:
        raise AssertionError(f"Job {name!r} does not exist in {workflow}")
    return match.group(1)


class WindowsGuiWorkflowTest(unittest.TestCase):
    def assert_gui_asset_tools(self, windows_job):
        self.assertIn("uses: ./.github/actions/setup-windows-gui-tools", windows_job)
        self.assertLess(
            windows_job.index("uses: ./.github/actions/setup-windows-gui-tools"),
            windows_job.index("cmake -B build"),
        )
        self.assertIn('-DRSVG_CONVERT="$env:RSVG_CONVERT"', windows_job)
        self.assertIn('-DIMAGEMAGICK_CONVERT="$env:IMAGEMAGICK_CONVERT"', windows_job)

    def test_gui_asset_tools_are_pinned_and_exported(self):
        gui_tools = GUI_TOOLS_ACTION.read_text(encoding="utf-8")
        self.assertIn("choco install rsvg-convert --version=2.40.20", gui_tools)
        self.assertIn("choco install imagemagick --version=7.1.2.2500", gui_tools)
        self.assertIn("Get-Command rsvg-convert.exe", gui_tools)
        self.assertIn("Get-Command magick.exe", gui_tools)
        self.assertIn('"RSVG_CONVERT=$rsvgConvert" >> $env:GITHUB_ENV', gui_tools)
        self.assertIn('"IMAGEMAGICK_CONVERT=$imagemagickConvert" >> $env:GITHUB_ENV', gui_tools)

    def test_pr_smoke_builds_and_tests_windows_gui(self):
        windows_smoke = job(CI_WORKFLOW, "windows-smoke")
        self.assert_gui_asset_tools(windows_smoke)
        self.assertIn("-DBUILD_GUI=ON", windows_smoke)
        self.assertNotIn("-DBUILD_GUI=OFF", windows_smoke)
        self.assertIn("-DBUILD_TESTS=ON", windows_smoke)
        self.assertIn("ctest --test-dir build -C Release --output-on-failure", windows_smoke)

    def test_release_builds_and_packages_windows_gui(self):
        windows_release = job(RELEASE_WORKFLOW, "windows-x86_64-release")
        self.assert_gui_asset_tools(windows_release)
        self.assertIn("-DBUILD_GUI=ON", windows_release)
        self.assertNotIn("-DBUILD_GUI=OFF", windows_release)
        self.assertIn('"bin\\bitcoin-qt.exe"', windows_release)
        self.assertIn("Test-Path -PathType Leaf $gui", windows_release)


if __name__ == "__main__":
    unittest.main()
