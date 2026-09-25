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
SCRIPT = ROOT / "ci/release/create-patch-series.sh"
RELEASE_TAG = "v29.4-roots.1"


class CreatePatchSeriesTest(unittest.TestCase):
    def git(self, repository, *args, **kwargs):
        return subprocess.run(["git", "-C", repository, *args], check=True, **kwargs)

    def make_release_repository(self):
        temporary_dir = tempfile.TemporaryDirectory()
        repository = Path(temporary_dir.name)
        self.addCleanup(temporary_dir.cleanup)
        subprocess.run(["git", "init", "--initial-branch=main", repository], check=True, capture_output=True)
        self.git(repository, "config", "user.name", "Patch test")
        self.git(repository, "config", "user.email", "patch@example.invalid")
        (repository / "source").write_text("core\n", encoding="utf-8")
        workflow = repository / ".github/workflows/ci.yml"
        workflow.parent.mkdir(parents=True)
        workflow.write_text("name: Core CI\n", encoding="utf-8")
        self.git(repository, "add", "source", ".github")
        self.git(repository, "commit", "-m", "core", capture_output=True)
        core_commit = self.git(repository, "rev-parse", "HEAD", capture_output=True, text=True).stdout.strip()
        self.git(repository, "tag", "-a", "v29.4", "-m", "Core v29.4")
        self.git(repository, "switch", "-c", "roots/29.4", capture_output=True)
        (repository / "source").write_text("roots\n", encoding="utf-8")
        self.git(repository, "commit", "-am", "roots text", capture_output=True)
        (repository / "binary.dat").write_bytes(bytes(range(256)))
        self.git(repository, "add", "binary.dat")
        self.git(repository, "commit", "-m", "roots binary", capture_output=True)
        workflow.write_text("name: Roots CI\n", encoding="utf-8")
        self.git(repository, "commit", "-am", "ci: use Roots tests", capture_output=True)
        self.git(repository, "tag", "-a", RELEASE_TAG, "-m", RELEASE_TAG)
        self.git(repository, "checkout", "--detach", RELEASE_TAG, capture_output=True)
        return repository, core_commit

    def test_generates_product_series_and_omits_github_automation(self):
        repository, core_commit = self.make_release_repository()
        output = repository / "artifacts/bitcoin-roots-29.4-roots.1.patch"
        environment = {**os.environ, "RELEASE_CANONICAL_REF": "refs/heads/roots/29.4"}
        subprocess.run([SCRIPT, RELEASE_TAG, output], cwd=repository, env=environment, check=True)

        patch = output.read_text(encoding="utf-8")
        self.assertIn("GIT binary patch", patch)
        self.assertIn(f"base-commit: {core_commit}", patch)
        self.assertGreaterEqual(patch.count("Subject: [PATCH"), 2)
        self.assertNotIn("diff --git a/.github/", patch)
        self.assertNotIn("ci: use Roots tests", patch)

        with tempfile.TemporaryDirectory() as replay_dir:
            replay = Path(replay_dir) / "repository"
            subprocess.run(["git", "clone", "--quiet", repository, replay], check=True)
            self.git(replay, "checkout", "--detach", core_commit, capture_output=True)
            self.git(
                replay, "-c", "user.name=Patch replay", "-c", "user.email=replay@example.invalid",
                "am", "-3", output, capture_output=True,
            )
            self.git(
                replay, "diff", "--quiet", RELEASE_TAG, "HEAD", "--",
                ".", ":(exclude).github/**",
            )
            self.git(replay, "diff", "--quiet", core_commit, "HEAD", "--", ".github")
            self.assertEqual((replay / ".github/workflows/ci.yml").read_text(encoding="utf-8"), "name: Core CI\n")

    def test_rejects_noncanonical_release(self):
        repository, _ = self.make_release_repository()
        self.git(repository, "branch", "--force", "roots/29.4", "v29.4")
        output = repository / "release.patch"
        result = subprocess.run(
            [SCRIPT, RELEASE_TAG, output], cwd=repository,
            env={**os.environ, "RELEASE_CANONICAL_REF": "refs/heads/roots/29.4"},
            capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
