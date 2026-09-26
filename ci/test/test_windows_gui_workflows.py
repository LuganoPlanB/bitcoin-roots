#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
CI_WORKFLOW = ROOT / ".github/workflows/ci.yml"
RELEASE_WORKFLOW = ROOT / ".github/workflows/release.yml"
ROOTS_LOGO = ROOT / "src/qt/res/src/bitcoinroots-logo.svg"
WINDOWS_RESOURCES = ROOT / "src/qt/res/bitcoin-qt-res.rc"


def job(workflow, name):
    text = workflow.read_text(encoding="utf-8")
    match = re.search(rf"(?ms)^  {re.escape(name)}:\n(.*?)(?=^  [a-z0-9_-]+:\n|\Z)", text)
    if match is None:
        raise AssertionError(f"Job {name!r} does not exist in {workflow}")
    return match.group(1)


class WindowsGuiWorkflowTest(unittest.TestCase):
    def test_roots_logo_preserves_icon_rendering_dimensions(self):
        logo = ET.parse(ROOTS_LOGO).getroot()
        self.assertEqual(logo.attrib["width"], "1in")
        self.assertEqual(logo.attrib["height"], "1in")

        _, _, viewbox_width, viewbox_height = logo.attrib["viewBox"].split()
        self.assertEqual(viewbox_width, viewbox_height)

    def test_windows_resources_use_source_absolute_icon_paths(self):
        resources = WINDOWS_RESOURCES.read_text(encoding="utf-8")
        icon_lines = [line for line in resources.splitlines() if " ICON " in line]
        self.assertEqual(len(icon_lines), 4)
        for line in icon_lines:
            self.assertIn('@CMAKE_CURRENT_SOURCE_DIR@/res/icons/', line)

    def assert_static_gui_assets(self, windows_job):
        self.assertNotIn("setup-windows-gui-tools", windows_job)
        self.assertNotIn("-DRSVG_CONVERT=", windows_job)
        self.assertNotIn("-DIMAGEMAGICK_CONVERT=", windows_job)
        for filename in ["bitcoin.ico", "bitcoin.icns", "bitcoin.png", "bitcoinroots-splash.png"]:
            self.assertTrue((ROOT / "src/qt/res/icons" / filename).is_file(), filename)
        gui_cmake = (ROOT / "src/qt/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertNotIn("find_program(RSVG_CONVERT", gui_cmake)

    def test_pr_smoke_builds_and_tests_windows_gui(self):
        windows_smoke = job(CI_WORKFLOW, "windows-smoke")
        self.assert_static_gui_assets(windows_smoke)
        self.assertIn("-DBUILD_GUI=ON", windows_smoke)
        self.assertNotIn("-DBUILD_GUI=OFF", windows_smoke)
        self.assertIn("-DBUILD_TESTS=ON", windows_smoke)
        self.assertIn("ctest --test-dir build -C Release --output-on-failure", windows_smoke)

    def test_release_builds_and_packages_windows_gui(self):
        windows_release = job(RELEASE_WORKFLOW, "windows-release")
        self.assert_static_gui_assets(windows_release)
        self.assertIn("-DBUILD_GUI=ON", windows_release)
        self.assertNotIn("-DBUILD_GUI=OFF", windows_release)
        self.assertIn("-DWERROR=ON", windows_release)
        self.assertIn("python ci/release/archive.py root-name", windows_release)
        self.assertIn("Compress-Archive -Path $installDir", windows_release)
        self.assertNotIn('Compress-Archive -Path (Join-Path $installDir "*")', windows_release)
        self.assertIn("python ci/release/archive.py validate", windows_release)
        self.assertIn('Join-Path $installDir "bin\\bitcoin-qt.exe"', windows_release)
        self.assertIn("Test-Path -PathType Leaf", windows_release)


if __name__ == "__main__":
    unittest.main()
