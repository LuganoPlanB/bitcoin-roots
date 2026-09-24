#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "ci/release/validate-release-source.sh"


class ValidateReleaseSourceTest(unittest.TestCase):
    def make_repository(self):
        temporary_dir = tempfile.TemporaryDirectory()
        repository = Path(temporary_dir.name)
        self.addCleanup(temporary_dir.cleanup)
        subprocess.run(["git", "init", "--initial-branch=main", repository], check=True, capture_output=True)
        subprocess.run(["git", "-C", repository, "config", "user.name", "Release test"], check=True)
        subprocess.run(["git", "-C", repository, "config", "user.email", "release@example.invalid"], check=True)
        (repository / "source").write_text("first\n", encoding="utf-8")
        subprocess.run(["git", "-C", repository, "add", "source"], check=True)
        subprocess.run(["git", "-C", repository, "commit", "-m", "initial"], check=True, capture_output=True)
        return repository

    def tag_and_checkout(self, repository, tag):
        subprocess.run(["git", "-C", repository, "tag", "-a", tag, "-m", tag], check=True)
        subprocess.run(["git", "-C", repository, "checkout", "--detach", tag], check=True, capture_output=True)

    def validate(self, repository, tag, main_ref="main"):
        return subprocess.run(
            [SCRIPT, tag], cwd=repository, env={**os.environ, "RELEASE_MAIN_REF": main_ref},
            capture_output=True, text=True,
        )

    def test_accepts_annotated_tag_at_checked_out_main_ancestor(self):
        repository = self.make_repository()
        self.tag_and_checkout(repository, "v29.4-roots.1")
        self.assertEqual(self.validate(repository, "v29.4-roots.1").returncode, 0)

    def test_rejects_lightweight_tag(self):
        repository = self.make_repository()
        subprocess.run(["git", "-C", repository, "tag", "v29.4-roots.1"], check=True)
        self.assertNotEqual(self.validate(repository, "v29.4-roots.1").returncode, 0)

    def test_rejects_head_mismatch(self):
        repository = self.make_repository()
        subprocess.run(["git", "-C", repository, "tag", "-a", "v29.4-roots.1", "-m", "release"], check=True)
        (repository / "source").write_text("second\n", encoding="utf-8")
        subprocess.run(["git", "-C", repository, "commit", "-am", "next"], check=True, capture_output=True)
        self.assertNotEqual(self.validate(repository, "v29.4-roots.1").returncode, 0)

    def test_rejects_missing_main_reference(self):
        repository = self.make_repository()
        self.tag_and_checkout(repository, "v29.4-roots.1")
        self.assertNotEqual(self.validate(repository, "v29.4-roots.1", "missing").returncode, 0)

    def test_rejects_tag_not_reachable_from_main(self):
        repository = self.make_repository()
        subprocess.run(["git", "-C", repository, "checkout", "-b", "release"], check=True, capture_output=True)
        (repository / "release").write_text("release\n", encoding="utf-8")
        subprocess.run(["git", "-C", repository, "add", "release"], check=True)
        subprocess.run(["git", "-C", repository, "commit", "-m", "release"], check=True, capture_output=True)
        self.tag_and_checkout(repository, "v29.4-roots.1")
        self.assertNotEqual(self.validate(repository, "v29.4-roots.1").returncode, 0)


if __name__ == "__main__":
    unittest.main()
