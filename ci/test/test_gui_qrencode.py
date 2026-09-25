#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CI_ENV_DIR = ROOT / "ci/test"
WORKFLOW_DIR = ROOT / ".github/workflows"


class GuiQrencodeTest(unittest.TestCase):
    def test_gui_ci_environments_require_qrencode(self):
        gui_environments = []
        for path in sorted(CI_ENV_DIR.glob("00_setup_env_*.sh")):
            content = path.read_text(encoding="utf-8")
            if "-DBUILD_GUI=ON" in content:
                gui_environments.append(path)
                self.assertIn("-DWITH_QRENCODE=ON", content, path)

        self.assertTrue(gui_environments)

    def test_direct_gui_workflow_builds_require_qrencode(self):
        gui_commands = []
        for path in sorted(WORKFLOW_DIR.glob("*.yml")):
            for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
                if "cmake -B" in line and "-DBUILD_GUI=ON" in line:
                    gui_commands.append((path, line_number))
                    self.assertIn("-DWITH_QRENCODE=ON", line, f"{path}:{line_number}")

        self.assertTrue(gui_commands)


if __name__ == "__main__":
    unittest.main()
