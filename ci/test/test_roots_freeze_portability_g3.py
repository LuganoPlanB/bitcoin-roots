#!/usr/bin/env python3
"""Adversarial tests for deterministic Roots 29.4 G3 construction."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "contrib/devtools/roots-freeze-portability-g3.py"
SPEC = importlib.util.spec_from_file_location("roots_freeze_portability_g3", TOOL)
G3 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(G3)


def git(repository: Path, *arguments: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=check,
        capture_output=True,
        text=True,
    )


class FreezePortabilityG3Test(unittest.TestCase):
    def fresh(self, directory: Path, name: str = "fixture") -> Path:
        repository = directory / name
        subprocess.run(["git", "clone", "-q", "--local", str(ROOT), str(repository)], check=True)
        self.assertFalse((repository / ".git/objects/info/alternates").exists())
        # The control branch preserves the immutable G3 records. Constructor
        # fixtures need their own absent output paths to exercise build and
        # overwrite rejection without mutating those preserved records.
        git(repository, "update-index", "--skip-worktree", "--", *G3.ARTIFACTS.values())
        for relative in G3.ARTIFACTS.values():
            (repository / relative).unlink(missing_ok=True)
        return repository

    def run_tool(self, command: str, repository: Path, root: Path | None = None, reference: str | None = None, environment: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        arguments = [sys.executable, str(TOOL), command, "--repository", str(repository), "--root", str(root or repository)]
        if reference is not None:
            arguments.extend(("--ref", reference))
        return subprocess.run(arguments, check=False, capture_output=True, text=True, env=environment)

    def build(self, repository: Path) -> None:
        result = self.run_tool("build", repository)
        self.assertEqual(result.returncode, 0, result.stderr)

    def refs(self, repository: Path) -> str:
        return git(repository, "for-each-ref", "--format=%(refname) %(objectname)").stdout

    def test_constructs_twice_with_exact_identity_topology_and_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first, second = self.fresh(root, "first"), self.fresh(root, "second")
            first_refs, second_refs = self.refs(first), self.refs(second)
            self.build(first)
            self.build(second)
            self.assertEqual(self.refs(first), first_refs)
            self.assertEqual(self.refs(second), second_refs)
            for relative in G3.ARTIFACTS.values():
                self.assertEqual((first / relative).read_bytes(), (second / relative).read_bytes())
            for repository in (first, second):
                self.assertEqual(git(repository, "rev-parse", G3.G3_COMMIT + "^{tree}").stdout.strip(), G3.G3_TREE)
                self.assertEqual(git(repository, "show", "-s", "--format=%P", G3.G3_COMMIT).stdout.strip(), G3.G2_COMMIT)
                self.assertEqual(git(repository, "rev-list", "--count", G3.G2_COMMIT + ".." + G3.G3_COMMIT).stdout.strip(), "1")
                self.assertEqual(
                    git(repository, "diff-tree", "--no-commit-id", "--name-only", "-r", G3.G2_COMMIT, G3.G3_COMMIT).stdout.splitlines(),
                    list(G3.CHANGED_PATHS),
                )
                metadata = git(repository, "show", "-s", "--format=%an%n%ae%n%aI%n%cn%n%ce%n%cI%n%s", G3.G3_COMMIT).stdout.splitlines()
                self.assertEqual(metadata, [
                    "Bitcoin Roots", "release@bitcoin-roots.invalid", G3.G3_DATE,
                    "Bitcoin Roots", "release@bitcoin-roots.invalid", G3.G3_DATE,
                    G3.G3_SUBJECT.decode().strip(),
                ])

    def test_real_gate_reproduces_old_failure_and_accepts_g3(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            repository = self.fresh(root, "constructor")
            self.build(repository)
            git(repository, "update-ref", "refs/heads/g3-test-transfer", G3.G3_COMMIT)
            candidate = root / "candidate"
            subprocess.run(["git", "clone", "-q", "--local", str(repository), str(candidate)], check=True)
            git(repository, "update-ref", "-d", "refs/heads/g3-test-transfer")
            git(candidate, "checkout", "-q", "--detach", G3.G3_COMMIT)
            trusted = self.fresh(root, "trusted")
            git(trusted, "checkout", "-q", "--detach", G3.CANONICAL_COMMIT)
            git(candidate, "update-ref", "refs/roots-pr/candidate", G3.G3_COMMIT)
            bundle = root / "candidate.bundle"
            git(candidate, "bundle", "create", str(bundle), "refs/roots-pr/candidate")
            git(trusted, "fetch", "-q", "--no-tags", str(bundle), "refs/roots-pr/candidate:refs/roots-pr/candidate")

            def gate(label: str, bootstrap_commit: str) -> subprocess.CompletedProcess[str]:
                bootstrap = self.fresh(root, "bootstrap-" + label)
                git(bootstrap, "checkout", "-q", "--detach", bootstrap_commit)
                return subprocess.run(
                    [
                        sys.executable, str(bootstrap / "contrib/devtools/roots-pr-gate.py"),
                        "--repository", str(trusted),
                        "--trusted-tools", str(bootstrap / "contrib/devtools"),
                        "--record", str(candidate / G3.ACCOUNTING),
                        "--manifest", str(candidate / "contrib/roots/adaptation-manifest-29.3.json"),
                        "--methodology", str(candidate / "contrib/roots/methodology-v1.json"),
                        "--registry", str(candidate / G3.REGISTRY),
                        "--base", G3.CANONICAL_COMMIT,
                        "--candidate", G3.G3_COMMIT,
                        "--report", str(root / (label + "-report.json")),
                    ],
                    check=False,
                    capture_output=True,
                    text=True,
                )

            old = gate("old", G3.OLD_BOOTSTRAP)
            self.assertNotEqual(old.returncode, 0)
            self.assertIn("methodology bundle digest is invalid", old.stderr)
            accepted = gate("accepted", G3.ACCEPTED_BOOTSTRAP)
            self.assertEqual(accepted.returncode, 0, accepted.stderr)

            methodology = candidate / "contrib/roots/methodology-v1.json"
            saved_methodology = methodology.read_bytes()
            value = json.loads(saved_methodology)
            value["production_exclusions"] = []
            methodology.write_text(json.dumps(value), encoding="utf-8")
            changed = gate("changed-methodology", G3.ACCEPTED_BOOTSTRAP)
            self.assertNotEqual(changed.returncode, 0)
            methodology.write_bytes(saved_methodology)

            marker = root / "candidate-validator-executed"
            candidate_validator = candidate / "contrib/devtools/roots-methodology.py"
            candidate_validator.write_text(f"from pathlib import Path\nPath({str(marker)!r}).write_text('executed')\n", encoding="utf-8")
            self_approval = gate("self-approval", G3.ACCEPTED_BOOTSTRAP)
            self.assertNotEqual(self_approval.returncode, 0)
            self.assertIn("methodology frozen input drift", self_approval.stderr)
            self.assertFalse(marker.exists())

    def test_artifact_substitution_wrong_identity_and_overwrite_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repository = self.fresh(Path(temporary))
            self.build(repository)
            self.assertEqual(self.run_tool("verify", repository).returncode, 0)
            second_build = self.run_tool("build", repository)
            self.assertNotEqual(second_build.returncode, 0)
            self.assertIn("refusing artifact overwrite", second_build.stderr)
            cases = (
                ("replay", ("g3", "parent"), "sha1:" + "0" * 40),
                ("invariants", ("g3_tree",), "sha1:" + "0" * 40),
                ("accounting", ("production_commits", 1, "commit"), "sha1:" + G3.G2_COMMIT),
                ("review", ("patch_series_sha256",), "sha256:" + "0" * 64),
                ("freeze", ("g3_commit",), "sha1:" + G3.G2_COMMIT),
                ("acceptance", ("accepted_bootstrap",), "sha1:" + G3.OLD_BOOTSTRAP),
            )
            for kind, keys, replacement in cases:
                with self.subTest(kind=kind):
                    path = repository / G3.ARTIFACTS[kind]
                    saved = path.read_bytes()
                    value = json.loads(saved)
                    target = value
                    for key in keys[:-1]:
                        target = target[key]
                    target[keys[-1]] = replacement
                    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
                    result = self.run_tool("verify", repository)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("evidence differs", result.stderr)
                    path.write_bytes(saved)

    def test_hostile_repository_state_and_nondeterministic_environment_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            baseline = self.fresh(root, "baseline")
            self.build(baseline)
            baseline_bytes = {key: (baseline / path).read_bytes() for key, path in G3.ARTIFACTS.items()}
            hostile_environment = dict(os.environ)
            hostile_environment.update({
                "GIT_AUTHOR_NAME": "Hostile",
                "GIT_AUTHOR_DATE": "2001-01-01T00:00:00Z",
                "GIT_COMMITTER_NAME": "Hostile",
                "GIT_COMMITTER_DATE": "2001-01-01T00:00:00Z",
                "TZ": "Pacific/Kiritimati",
                "LC_ALL": "C.UTF-8",
            })
            hostile = self.fresh(root, "hostile-env")
            result = self.run_tool("build", hostile, environment=hostile_environment)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(baseline_bytes, {key: (hostile / path).read_bytes() for key, path in G3.ARTIFACTS.items()})

            def alternates(repository: Path) -> None:
                path = repository / ".git/objects/info/alternates"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(str(ROOT / ".git/objects") + "\n", encoding="utf-8")

            def replacement(repository: Path) -> None:
                git(repository, "replace", G3.G2_COMMIT, G3.CANONICAL_COMMIT)

            def graft(repository: Path) -> None:
                path = repository / ".git/info/grafts"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(G3.G2_COMMIT + " " + G3.CANONICAL_COMMIT + "\n", encoding="utf-8")

            def rerere(repository: Path) -> None:
                git(repository, "config", "rerere.enabled", "true")

            def hook(repository: Path) -> None:
                path = repository / ".git/hooks/pre-commit"
                path.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
                path.chmod(0o755)

            for name, mutate in (("alternates", alternates), ("replacement", replacement), ("graft", graft), ("rerere", rerere), ("hook", hook)):
                with self.subTest(name=name):
                    repository = self.fresh(root, name)
                    before = self.refs(repository)
                    mutate(repository)
                    result = self.run_tool("build", repository)
                    self.assertNotEqual(result.returncode, 0)
                    after = self.refs(repository)
                    if name == "replacement":
                        self.assertEqual(set(after.splitlines()) - set(before.splitlines()), {f"refs/replace/{G3.G2_COMMIT} {G3.CANONICAL_COMMIT}"})
                    else:
                        self.assertEqual(after, before)
                    self.assertNotIn(G3.G3_REF, after)

    def test_anchor_is_absent_only_and_preserves_g2_refs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repository = self.fresh(Path(temporary))
            git(repository, "update-ref", "refs/roots/29.4/frozen-production", "ad3126495925c93e1b1341146be9cc9512d3d448")
            git(repository, "update-ref", "refs/roots/29.4/frozen-production-g2", G3.G2_COMMIT)
            self.build(repository)
            wrong = self.run_tool("anchor", repository, reference="refs/heads/not-owned")
            self.assertNotEqual(wrong.returncode, 0)
            before = self.refs(repository)
            first = self.run_tool("anchor", repository)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(git(repository, "rev-parse", G3.G3_REF).stdout.strip(), G3.G3_COMMIT)
            self.assertEqual(git(repository, "rev-parse", "refs/roots/29.4/frozen-production").stdout.strip(), "ad3126495925c93e1b1341146be9cc9512d3d448")
            self.assertEqual(git(repository, "rev-parse", "refs/roots/29.4/frozen-production-g2").stdout.strip(), G3.G2_COMMIT)
            second = self.run_tool("anchor", repository)
            self.assertNotEqual(second.returncode, 0)
            self.assertIn("already exists", second.stderr)
            after = self.refs(repository)
            self.assertEqual(set(after.splitlines()) - set(before.splitlines()), {f"{G3.G3_REF} {G3.G3_COMMIT}"})


if __name__ == "__main__":
    unittest.main()
