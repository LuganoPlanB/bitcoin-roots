#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "ci/release/validate-release-tag.sh"


class ValidateReleaseTagTest(unittest.TestCase):
    def test_accepts_supported_version_tags(self):
        for tag in ["v29.3.0-roots.1", "v29.3.0-roots.2-rc1", "v30.0+roots.1"]:
            with self.subTest(tag=tag):
                subprocess.run([SCRIPT, tag], check=True)

    def test_rejects_unsafe_or_non_version_tags(self):
        for tag in ["29.3.0-roots.1", "v", "v1/other", "v1..2", "v1;false", "v1\nother"]:
            with self.subTest(tag=tag):
                result = subprocess.run([SCRIPT, tag], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
