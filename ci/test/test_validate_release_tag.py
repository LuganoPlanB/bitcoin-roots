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
    def test_accepts_roots_final_and_release_candidate_tags(self):
        for tag in ["v29.4-roots.1", "v29.4.1-roots.2", "v29.4rc1-roots.1"]:
            with self.subTest(tag=tag):
                subprocess.run([SCRIPT, tag], check=True)

    def test_rejects_unsafe_or_non_roots_tags(self):
        for tag in ["29.4-roots.1", "v29.4", "v29.4-rc1-roots.1", "v29.4-roots.0", "v29.4-roots.1/other", "v29.4-roots.1\nother"]:
            with self.subTest(tag=tag):
                result = subprocess.run([SCRIPT, tag], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
