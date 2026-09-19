#!/usr/bin/env python3
"""Adversarial tests for the deterministic Roots 29.4 G4 freeze."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "contrib/devtools/roots-freeze-g4.py"
SPEC = importlib.util.spec_from_file_location("roots_freeze_g4", TOOL)
G4 = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(G4)


def git(repository: Path, *arguments: str, check: bool = True) -> str:
    result = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=False,
        capture_output=True,
        text=True,
    )
    if check and result.returncode:
        raise AssertionError(result.stderr)
    return result.stdout.strip()


class FreezeG4Test(unittest.TestCase):
    def repository(self, root: Path) -> Path:
        repository = root / "repository"
        subprocess.run(["git", "clone", "--quiet", "--no-hardlinks", ROOT, repository], check=True)
        git(repository, "checkout", "--quiet", "--detach", G4.G3_COMMIT)
        return repository

    def materials(self, root: Path) -> Path:
        materials = root / "materials"
        paths = [
            *G4.OVERLAY_PATHS,
            G4.FROZEN_TRUSTED_REPLAY_WORKFLOW,
            "contrib/devtools/roots-continuous-accounting.py",
            *G4.G3_ARTIFACTS.values(),
        ]
        for relative in paths:
            source = ROOT / relative
            destination = materials / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
        return materials

    def variant(self, repository: Path, commit: str, *, parent: str = G4.G3_COMMIT, mode: str | None = None) -> str:
        with tempfile.TemporaryDirectory() as temporary:
            index = Path(temporary) / "index"
            G4.BASE.git(repository, "read-tree", commit, index=index)
            if mode is not None:
                option = "--chmod=+x" if mode == "100755" else "--chmod=-x"
                G4.BASE.git(repository, "update-index", option, "--", G4.VALIDATOR, index=index)
            tree = G4.BASE.git_text(repository, "write-tree", index=index)
        return G4.BASE.git_text(repository, "commit-tree", tree, "-p", parent, input_data=b"hostile variant\n")

    def test_constructs_twice_with_exact_identity_parent_scope_and_metadata(self) -> None:
        identities = []
        for _ in range(2):
            with tempfile.TemporaryDirectory() as temporary:
                repository = self.repository(Path(temporary))
                commit, tree, _ = G4.construct(repository, ROOT)
                identities.append((commit, tree))
                self.assertEqual(git(repository, "show", "-s", "--format=%P", commit), G4.G3_COMMIT)
                self.assertEqual(git(repository, "show", "-s", "--format=%an <%ae> %aI%n%cn <%ce> %cI%n%s", commit),
                    "Bitcoin Roots <release@bitcoin-roots.invalid> 2026-09-18T00:00:00Z\n"
                    "Bitcoin Roots <release@bitcoin-roots.invalid> 2026-09-18T00:00:00Z\n"
                    "fix(release): freeze deterministic G4 candidate")
        self.assertEqual(identities, [(G4.G4_COMMIT, G4.G4_TREE)] * 2)

    def test_post_g4_control_workflow_does_not_change_frozen_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repository = self.repository(root)
            materials = self.materials(root)
            frozen = (materials / G4.FROZEN_TRUSTED_REPLAY_WORKFLOW).read_bytes()
            current = (materials / G4.TRUSTED_REPLAY_WORKFLOW).read_bytes()
            self.assertEqual(G4.git_blob_id(frozen), G4.FROZEN_TRUSTED_REPLAY_WORKFLOW_BLOB)
            self.assertNotEqual(current, frozen)
            first = G4.construct(repository, materials)[:2]
            (materials / G4.TRUSTED_REPLAY_WORKFLOW).write_bytes(b"post-G4 control drift\n")
            second = G4.construct(repository, materials)[:2]
            self.assertEqual(first, (G4.G4_COMMIT, G4.G4_TREE))
            self.assertEqual(second, first)
            (materials / G4.FROZEN_TRUSTED_REPLAY_WORKFLOW).write_bytes(frozen + b"# drift\n")
            with self.assertRaises(G4.FreezeError):
                G4.construct(repository, materials)

    def test_candidate_contract_rejects_wrong_mode_non_ff_and_workflow_blob(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repository = self.repository(root)
            materials = self.materials(root)
            commit, _, _ = G4.construct(repository, materials)
            wrong_mode = self.variant(repository, commit, mode="100644")
            with self.assertRaises(G4.FreezeError):
                G4.verify_candidate(repository, materials, wrong_mode)
            non_ff = self.variant(repository, commit, parent=G4.CANONICAL_COMMIT)
            with self.assertRaises(G4.FreezeError):
                G4.verify_candidate(repository, materials, non_ff)
            workflow = materials / G4.RELEASE_WORKFLOW
            workflow.write_bytes(workflow.read_bytes() + b"# drift\n")
            with self.assertRaises(G4.FreezeError):
                G4.verify_candidate(repository, materials, commit)

    def test_bdb_acknowledgement_is_single_and_per_commit_only(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repository = self.repository(Path(temporary))
            value = G4.transform_ci(repository)
            G4.validate_ci(value)
            self.assertEqual(value.count(b"-DWARN_INCOMPATIBLE_BDB=OFF"), 1)
            with self.assertRaises(G4.FreezeError):
                G4.validate_ci(value + b"\n-DWARN_INCOMPATIBLE_BDB=OFF\n")
            with self.assertRaises(G4.FreezeError):
                G4.validate_ci(value.replace(b"git rebase --exec", b"git rebase --noop"))

    def test_release_workflow_is_exact_and_candidate_builds_have_no_credentials(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repository = self.repository(Path(temporary))
            commit, _, _ = G4.construct(repository, ROOT)
            workflow = G4.BASE.blob(repository, commit, G4.RELEASE_WORKFLOW)
            self.assertEqual(workflow, (ROOT / G4.RELEASE_WORKFLOW).read_bytes())
            self.assertNotIn(b"persist-credentials: true", workflow)
            self.assertNotIn(b"id-token:", workflow)
            build_jobs = workflow.split(b"  release-metadata-tests:", 1)[1].split(b"  sign-release:", 1)[0]
            self.assertNotIn(b"secrets.", build_jobs)
            self.assertIn(b"candidate_commit", workflow)
            self.assertIn(b"candidate_tree", workflow)
            self.assertIn(b"control_sha", workflow)
            self.assertIn(b"control_tree", workflow)
            publish_mode = G4.BASE.git_text(
                repository, "ls-tree", commit, "--", "ci/release/publish-verified-draft.sh"
            ).split()[0]
            self.assertEqual(publish_mode, "100755")

    def test_candidate_accounting_passes_exact_trusted_pr_gate(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repository = self.repository(root)
            commit, _, _ = G4.construct(repository, ROOT)
            git(repository, "checkout", "--quiet", "--detach", commit)
            result = subprocess.run([
                "python3", ROOT / "contrib/devtools/roots-pr-gate.py",
                "--repository", repository,
                "--trusted-tools", ROOT / "contrib/devtools",
                "--record", repository / G4.ACCOUNTING,
                "--manifest", repository / "contrib/roots/adaptation-manifest-29.3.json",
                "--methodology", repository / "contrib/roots/methodology-v1.json",
                "--registry", repository / G4.REGISTRY,
                "--base", G4.CANONICAL_COMMIT,
                "--candidate", commit,
            ], check=False, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_host_state_is_rejected_and_failed_construction_mutates_no_ref(self) -> None:
        cases = ("rerere", "replacement", "hook", "alternate")
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                repository = self.repository(root)
                git_dir = Path(git(repository, "rev-parse", "--absolute-git-dir"))
                environment = {}
                if case == "rerere":
                    git(repository, "config", "rerere.enabled", "true")
                elif case == "replacement":
                    git(repository, "replace", G4.G3_COMMIT, G4.CANONICAL_COMMIT)
                elif case == "hook":
                    hook = git_dir / "hooks/pre-commit"
                    hook.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
                else:
                    environment["GIT_ALTERNATE_OBJECT_DIRECTORIES"] = str(root / "host-objects")
                with mock.patch.dict(os.environ, environment, clear=False), self.assertRaises(G4.BASE.FreezeError):
                    G4.construct(repository, ROOT)
                self.assertNotEqual(git(repository, "show-ref", "--verify", G4.G4_REF, check=False), G4.G4_COMMIT)

    def test_artifacts_are_absent_only_and_anchor_preserves_existing_refs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repository = self.repository(root)
            materials = self.materials(root)
            G4.build(repository, materials)
            with self.assertRaises(G4.FreezeError):
                G4.build(repository, materials)
            G4.verify(repository, materials)
            target = root / "dirty-control"
            subprocess.run(["git", "clone", "--quiet", "--no-hardlinks", ROOT, target], check=True)
            (target / "README.md").write_bytes((target / "README.md").read_bytes() + b"\n")
            git(target, "update-ref", "refs/roots/29.4/frozen-production-g3", G4.G3_COMMIT)
            git(target, "update-ref", "refs/roots/legacy-preserved", G4.CANONICAL_COMMIT)
            before = git(target, "rev-parse", "refs/roots/legacy-preserved")
            G4.anchor(repository, materials, G4.G4_REF, target)
            self.assertEqual(git(target, "rev-parse", G4.G4_REF), G4.G4_COMMIT)
            self.assertEqual(git(target, "rev-parse", "refs/roots/29.4/frozen-production-g3"), G4.G3_COMMIT)
            self.assertEqual(git(target, "rev-parse", "refs/roots/legacy-preserved"), before)
            self.assertTrue(git(target, "status", "--porcelain"))
            with self.assertRaises(G4.FreezeError):
                G4.anchor(repository, materials, G4.G4_REF, target)


if __name__ == "__main__":
    unittest.main()
