#!/usr/bin/env python3
"""Tests for immutable release rehearsal source resolution."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "ci/release/resolve-release-source.py"
SPEC = importlib.util.spec_from_file_location("resolve_release_source", TOOL)
SOURCE = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(SOURCE)


def git(repository: Path, *arguments: str) -> str:
    return subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


class ReleaseSourceLockTest(unittest.TestCase):
    def fixture(self, root: Path) -> tuple[Path, Path, str, str, str, str]:
        remote = root / "remote.git"
        subprocess.run(["git", "init", "--quiet", "--bare", str(remote)], check=True)
        builder = root / "builder"
        subprocess.run(["git", "init", "--quiet", str(builder)], check=True)
        git(builder, "config", "user.name", "Release Fixture")
        git(builder, "config", "user.email", "release-fixture@invalid")
        workflow = builder / SOURCE.RELEASE_WORKFLOW
        workflow.parent.mkdir(parents=True)
        workflow.write_text("name: exact release workflow\n", encoding="utf-8")
        (builder / "candidate.txt").write_text("base\n", encoding="utf-8")
        git(builder, "add", ".")
        git(builder, "commit", "--quiet", "-m", "control")
        control = git(builder, "rev-parse", "HEAD")
        control_tree = git(builder, "rev-parse", "HEAD^{tree}")
        git(builder, "branch", "-M", "codex/roots-29-4-release")
        git(builder, "branch", "main")
        git(builder, "checkout", "--quiet", "-b", "integration/roots-29.4")
        (builder / "candidate.txt").write_text("candidate\n", encoding="utf-8")
        git(builder, "add", "candidate.txt")
        git(builder, "commit", "--quiet", "-m", "candidate")
        candidate = git(builder, "rev-parse", "HEAD")
        tree = git(builder, "rev-parse", "HEAD^{tree}")
        git(builder, "tag", "-a", "v29.4-roots.1", "-m", "release", candidate)
        git(builder, "remote", "add", "origin", str(remote))
        git(builder, "push", "--quiet", "origin", "codex/roots-29-4-release", "integration/roots-29.4", "main", "v29.4-roots.1")
        trusted = root / "trusted"
        subprocess.run(
            ["git", "clone", "--quiet", "--no-checkout", "--no-hardlinks", str(remote), str(trusted)],
            check=True,
        )
        git(trusted, "checkout", "--quiet", "--detach", control)
        return remote, trusted, control, control_tree, candidate, tree

    def resolve(self, remote: Path, trusted: Path, control: str, expected_control_tree: str, candidate: str, tree: str, **changes):
        values = {
            "trusted_repository": trusted,
            "remote": str(remote),
            "event_name": "workflow_dispatch",
            "event_ref": SOURCE.CONTROL_REF,
            "event_sha": control,
            "candidate_commit": candidate,
            "candidate_tree": tree,
            "control_sha": control,
            "control_tree": expected_control_tree,
        }
        values.update(changes)
        return SOURCE.resolve(**values)

    def test_rehearsal_resolves_exact_candidate_and_equal_workflow(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            remote, trusted, control, control_tree, candidate, tree = self.fixture(Path(temporary))
            self.assertEqual(self.resolve(remote, trusted, control, control_tree, candidate, tree), {
                "rehearsal": True,
                "source_commit": candidate,
                "source_tree": tree,
            })

    def test_malformed_rehearsal_identities_run_no_git_subprocess(self) -> None:
        valid = "1" * 40
        values = {
            "trusted_repository": Path("/unused/trusted-repository"),
            "remote": "/unused/remote",
            "event_name": "workflow_dispatch",
            "event_ref": SOURCE.CONTROL_REF,
            "event_sha": valid,
            "candidate_commit": valid,
            "candidate_tree": valid,
            "control_sha": valid,
            "control_tree": valid,
        }
        for name in ("candidate_commit", "candidate_tree", "control_sha", "control_tree"):
            with self.subTest(name=name), mock.patch.object(SOURCE.subprocess, "run") as run:
                with self.assertRaisesRegex(SOURCE.SourceError, "must be a full commit ID"):
                    SOURCE.resolve(**(values | {name: "not-an-object-id"}))
                run.assert_not_called()
        with self.subTest(name="control_event_mismatch"), mock.patch.object(SOURCE.subprocess, "run") as run:
            with self.assertRaisesRegex(SOURCE.SourceError, "declared control SHA differs from event SHA"):
                SOURCE.resolve(**(values | {"control_sha": "2" * 40}))
            run.assert_not_called()

    def test_wrong_identity_tree_ref_and_workflow_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            remote, trusted, control, control_tree, candidate, tree = self.fixture(root)
            cases = (
                {"event_sha": "0" * 40},
                {"control_sha": "0" * 40},
                {"control_tree": "0" * 40},
                {"candidate_commit": control},
                {"candidate_tree": "0" * 40},
                {"event_ref": "refs/heads/main"},
            )
            for changes in cases:
                with self.subTest(changes=changes), self.assertRaises(SOURCE.SourceError):
                    self.resolve(remote, trusted, control, control_tree, candidate, tree, **changes)
            builder = root / "mutator"
            subprocess.run(["git", "clone", "--quiet", "--no-hardlinks", str(remote), str(builder)], check=True)
            git(builder, "config", "user.name", "Release Fixture")
            git(builder, "config", "user.email", "release-fixture@invalid")
            git(builder, "checkout", "--quiet", "integration/roots-29.4")
            (builder / SOURCE.RELEASE_WORKFLOW).write_text("name: changed workflow\n", encoding="utf-8")
            git(builder, "add", SOURCE.RELEASE_WORKFLOW)
            git(builder, "commit", "--quiet", "-m", "change workflow")
            moved = git(builder, "rev-parse", "HEAD")
            git(builder, "push", "--quiet", "origin", "integration/roots-29.4")
            with self.assertRaises(SOURCE.SourceError):
                self.resolve(remote, trusted, control, control_tree, moved, git(builder, "rev-parse", "HEAD^{tree}"))

    def test_tag_path_rejects_rehearsal_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            remote, trusted, control, control_tree, candidate, tree = self.fixture(Path(temporary))
            git(trusted, "checkout", "--quiet", "--detach", candidate)
            tag_result = SOURCE.resolve(
                trusted_repository=trusted,
                remote=str(remote),
                event_name="workflow_dispatch",
                event_ref=SOURCE.RELEASE_TAG,
                event_sha=candidate,
            )
            self.assertEqual(tag_result["source_commit"], candidate)
            self.assertEqual(tag_result["source_tree"], tree)
            with self.assertRaises(SOURCE.SourceError):
                SOURCE.resolve(
                    trusted_repository=trusted,
                    remote=str(remote),
                    event_name="workflow_dispatch",
                    event_ref=SOURCE.RELEASE_TAG,
                    event_sha=candidate,
                    candidate_commit=candidate,
                )

    def test_hostile_git_state_is_rejected_without_executing_candidate(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            remote, trusted, control, control_tree, candidate, tree = self.fixture(root)
            marker = root / "candidate-executed"
            mutator = root / "candidate-mutator"
            subprocess.run(["git", "clone", "--quiet", "--no-hardlinks", str(remote), str(mutator)], check=True)
            git(mutator, "config", "user.name", "Release Fixture")
            git(mutator, "config", "user.email", "release-fixture@invalid")
            git(mutator, "checkout", "--quiet", "integration/roots-29.4")
            payload = mutator / "candidate-payload.py"
            payload.write_text(f"from pathlib import Path\nPath({str(marker)!r}).write_text('executed')\n", encoding="utf-8")
            git(mutator, "add", "candidate-payload.py")
            git(mutator, "commit", "--quiet", "-m", "hostile candidate")
            hostile = git(mutator, "rev-parse", "HEAD")
            hostile_tree = git(mutator, "rev-parse", "HEAD^{tree}")
            git(mutator, "push", "--quiet", "origin", "integration/roots-29.4")
            self.resolve(remote, trusted, control, control_tree, hostile, hostile_tree)
            self.assertFalse(marker.exists())
            git(trusted, "config", "rerere.enabled", "true")
            with self.assertRaises(SOURCE.SourceError):
                self.resolve(remote, trusted, control, control_tree, hostile, hostile_tree)

    def test_environment_cannot_supply_alternates_or_git_config(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            remote, trusted, control, control_tree, candidate, tree = self.fixture(Path(temporary))
            hostile = dict(os.environ)
            hostile.update({
                "GIT_CONFIG_COUNT": "1",
                "GIT_CONFIG_KEY_0": "core.hooksPath",
                "GIT_CONFIG_VALUE_0": "/host/hooks",
                "TZ": "Pacific/Kiritimati",
                "LC_ALL": "C.UTF-8",
            })
            with mock.patch.dict(os.environ, hostile, clear=True):
                self.assertEqual(self.resolve(remote, trusted, control, control_tree, candidate, tree)["source_commit"], candidate)


if __name__ == "__main__":
    unittest.main()
