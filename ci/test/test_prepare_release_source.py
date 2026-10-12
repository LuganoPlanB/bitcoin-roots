#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "ci/release/prepare-release-source.sh"
RELEASE_TAG = "v29.4-roots.1"


class PrepareReleaseSourceTest(unittest.TestCase):
    def git(self, repository, *args, check=True, **kwargs):
        return subprocess.run(
            ["git", "-C", repository, *args], check=check, **kwargs
        )

    def make_checkout(self, *, publish_release_tag=False, checkout_tag=False):
        temporary_dir = tempfile.TemporaryDirectory()
        self.addCleanup(temporary_dir.cleanup)
        work = Path(temporary_dir.name)
        remote = work / "origin.git"
        source = work / "source"
        checkout = work / "checkout"

        subprocess.run(
            ["git", "init", "--bare", "--initial-branch=main", remote],
            check=True,
            capture_output=True,
        )
        subprocess.run(
            ["git", "init", "--initial-branch=main", source],
            check=True,
            capture_output=True,
        )
        self.git(source, "config", "user.name", "Release source test")
        self.git(source, "config", "user.email", "release-source@example.invalid")
        self.git(source, "remote", "add", "origin", str(remote))
        (source / "source").write_text("core\n", encoding="utf-8")
        self.git(source, "add", "source")
        self.git(source, "commit", "-m", "core", capture_output=True)
        self.git(source, "tag", "-a", "v29.4", "-m", "Core v29.4")
        self.git(source, "switch", "-c", "roots/29.4", capture_output=True)
        (source / "source").write_text("roots\n", encoding="utf-8")
        self.git(source, "commit", "-am", "roots", capture_output=True)
        release_commit = self.git(
            source, "rev-parse", "HEAD", capture_output=True, text=True
        ).stdout.strip()
        if publish_release_tag:
            self.git(source, "tag", "-a", RELEASE_TAG, "-m", RELEASE_TAG)
        self.git(source, "push", "origin", "roots/29.4", "v29.4", capture_output=True)
        if publish_release_tag:
            self.git(source, "push", "origin", RELEASE_TAG, capture_output=True)

        subprocess.run(
            ["git", "clone", "--no-tags", str(remote), checkout],
            check=True,
            capture_output=True,
        )
        if checkout_tag:
            self.git(
                checkout,
                "fetch",
                "--no-tags",
                "origin",
                f"refs/tags/{RELEASE_TAG}:refs/tags/{RELEASE_TAG}",
                capture_output=True,
            )
        self.git(
            checkout,
            "checkout",
            "--detach",
            RELEASE_TAG if checkout_tag else release_commit,
            capture_output=True,
        )
        return checkout, source, release_commit

    def prepare(self, checkout, release_commit, event_name):
        return subprocess.run(
            [SCRIPT, RELEASE_TAG, release_commit, event_name],
            cwd=checkout,
            env={**os.environ, "RELEASE_CORE_REMOTE": "origin"},
            capture_output=True,
            text=True,
        )

    def test_manual_rehearsal_creates_only_an_ephemeral_annotated_tag(self):
        checkout, source, release_commit = self.make_checkout()
        result = self.prepare(checkout, release_commit, "workflow_dispatch")
        self.assertEqual(result.returncode, 0, result.stderr)
        local_type = self.git(
            checkout,
            "cat-file",
            "-t",
            f"refs/tags/{RELEASE_TAG}",
            capture_output=True,
            text=True,
        ).stdout.strip()
        self.assertEqual(local_type, "tag")
        remote_tag = self.git(
            source,
            "ls-remote",
            "--tags",
            "origin",
            f"refs/tags/{RELEASE_TAG}",
            capture_output=True,
            text=True,
        ).stdout
        self.assertEqual(remote_tag, "")

    def test_manual_rehearsal_rejects_noncanonical_and_abbreviated_commits(self):
        checkout, _, release_commit = self.make_checkout()
        abbreviated = self.prepare(
            checkout, release_commit[:12], "workflow_dispatch"
        )
        self.assertNotEqual(abbreviated.returncode, 0)
        self.assertIn("full lowercase 40-hex commit", abbreviated.stderr)

        core_commit = self.git(
            checkout, "rev-parse", "v29.4^{commit}", capture_output=True, text=True
        ).stdout.strip()
        self.git(checkout, "checkout", "--detach", core_commit, capture_output=True)
        noncanonical = self.prepare(checkout, core_commit, "workflow_dispatch")
        self.assertNotEqual(noncanonical.returncode, 0)
        self.assertIn("does not match canonical branch tip", noncanonical.stderr)

    def test_manual_rehearsal_rejects_an_existing_remote_tag(self):
        checkout, _, release_commit = self.make_checkout(publish_release_tag=True)
        result = self.prepare(checkout, release_commit, "workflow_dispatch")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("requires the remote tag to be absent", result.stderr)

    def test_tag_push_validates_the_remote_annotated_tag(self):
        checkout, _, release_commit = self.make_checkout(
            publish_release_tag=True, checkout_tag=True
        )
        result = self.prepare(checkout, "", "push")
        self.assertEqual(result.returncode, 0, result.stderr)
        peeled = self.git(
            checkout,
            "rev-parse",
            f"{RELEASE_TAG}^{{commit}}",
            capture_output=True,
            text=True,
        ).stdout.strip()
        self.assertEqual(peeled, release_commit)

    def test_rejects_unknown_events_and_manual_input_on_tag_push(self):
        checkout, _, release_commit = self.make_checkout()
        unknown = self.prepare(checkout, release_commit, "pull_request")
        self.assertNotEqual(unknown.returncode, 0)
        self.assertIn("Unsupported release event", unknown.stderr)

        manual_input = self.prepare(checkout, release_commit, "push")
        self.assertNotEqual(manual_input.returncode, 0)
        self.assertIn("do not accept a manual release commit", manual_input.stderr)


if __name__ == "__main__":
    unittest.main()
