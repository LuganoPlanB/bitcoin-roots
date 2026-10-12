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
RELEASE_TAG = "v29.4-roots.1"
CANONICAL_REF = "refs/heads/roots/29.4"


class ValidateReleaseSourceTest(unittest.TestCase):
    def git(self, repository, *args, **kwargs):
        return subprocess.run(["git", "-C", repository, *args], check=True, **kwargs)

    def make_repository(self, *, with_core_tag=True):
        temporary_dir = tempfile.TemporaryDirectory()
        repository = Path(temporary_dir.name)
        self.addCleanup(temporary_dir.cleanup)
        subprocess.run(["git", "init", "--initial-branch=main", repository], check=True, capture_output=True)
        self.git(repository, "config", "user.name", "Release test")
        self.git(repository, "config", "user.email", "release@example.invalid")
        (repository / "source").write_text("core\n", encoding="utf-8")
        self.git(repository, "add", "source")
        self.git(repository, "commit", "-m", "core", capture_output=True)
        if with_core_tag:
            self.git(repository, "tag", "-a", "v29.4", "-m", "Core v29.4")
        self.git(repository, "switch", "-c", "roots/29.4", capture_output=True)
        (repository / "source").write_text("roots\n", encoding="utf-8")
        self.git(repository, "commit", "-am", "roots", capture_output=True)
        return repository

    def tag_and_checkout(self, repository, tag=RELEASE_TAG, *, annotated=True):
        args = ["tag"]
        if annotated:
            args.extend(["-a", tag, "-m", tag])
        else:
            args.append(tag)
        self.git(repository, *args)
        self.git(repository, "checkout", "--detach", tag, capture_output=True)

    def validate(self, repository, tag=RELEASE_TAG, canonical_ref=CANONICAL_REF):
        return subprocess.run(
            [SCRIPT, tag], cwd=repository,
            env={**os.environ, "RELEASE_CANONICAL_REF": canonical_ref},
            capture_output=True, text=True,
        )

    def test_accepts_annotated_tag_at_exact_canonical_tip(self):
        repository = self.make_repository()
        self.tag_and_checkout(repository)
        self.assertEqual(self.validate(repository).returncode, 0)

    def test_rejects_lightweight_tag(self):
        repository = self.make_repository()
        self.tag_and_checkout(repository, annotated=False)
        self.assertNotEqual(self.validate(repository).returncode, 0)

    def test_rejects_head_mismatch(self):
        repository = self.make_repository()
        self.git(repository, "tag", "-a", RELEASE_TAG, "-m", "release")
        self.git(repository, "switch", "--detach", "v29.4", capture_output=True)
        self.assertNotEqual(self.validate(repository).returncode, 0)

    def test_rejects_missing_canonical_reference(self):
        repository = self.make_repository()
        self.tag_and_checkout(repository)
        self.assertNotEqual(self.validate(repository, canonical_ref="missing").returncode, 0)

    def test_rejects_tag_behind_canonical_tip(self):
        repository = self.make_repository()
        self.git(repository, "tag", "-a", RELEASE_TAG, "-m", "release")
        (repository / "later").write_text("later\n", encoding="utf-8")
        self.git(repository, "add", "later")
        self.git(repository, "commit", "-m", "later", capture_output=True)
        self.git(repository, "checkout", "--detach", RELEASE_TAG, capture_output=True)
        result = self.validate(repository)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match canonical branch tip", result.stderr)

    def test_rejects_missing_core_base(self):
        repository = self.make_repository(with_core_tag=False)
        self.tag_and_checkout(repository)
        result = self.validate(repository)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Bitcoin Core base reference is unavailable", result.stderr)

    def test_rejects_core_base_that_is_not_an_ancestor(self):
        repository = self.make_repository(with_core_tag=False)
        self.git(repository, "tag", "-a", RELEASE_TAG, "-m", "release")
        self.git(repository, "switch", "--orphan", "unrelated-core", capture_output=True)
        self.git(repository, "rm", "-rf", "--ignore-unmatch", "source", capture_output=True)
        (repository / "source").write_text("unrelated\n", encoding="utf-8")
        self.git(repository, "add", "source")
        self.git(repository, "commit", "-m", "unrelated core", capture_output=True)
        self.git(repository, "tag", "-a", "v29.4", "-m", "Core v29.4")
        self.git(repository, "checkout", "--detach", RELEASE_TAG, capture_output=True)
        result = self.validate(repository)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("is not based on", result.stderr)

    def test_rejects_merge_in_patch_stack(self):
        repository = self.make_repository()
        self.git(repository, "switch", "-c", "topic", capture_output=True)
        (repository / "topic").write_text("topic\n", encoding="utf-8")
        self.git(repository, "add", "topic")
        self.git(repository, "commit", "-m", "topic", capture_output=True)
        self.git(repository, "switch", "roots/29.4", capture_output=True)
        (repository / "mainline").write_text("mainline\n", encoding="utf-8")
        self.git(repository, "add", "mainline")
        self.git(repository, "commit", "-m", "mainline", capture_output=True)
        self.git(repository, "merge", "--no-ff", "topic", "-m", "merge topic", capture_output=True)
        self.tag_and_checkout(repository)
        result = self.validate(repository)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must be linear", result.stderr)


if __name__ == "__main__":
    unittest.main()
