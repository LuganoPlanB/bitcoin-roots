#!/usr/bin/env python3
"""Focused release lineage, accounting, and build-attestation tests."""

import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "ci/release/roots-release-evidence.py"
BUILD_SCRIPT = ROOT / "ci/release/roots-build-evidence.py"
CANONICAL = (
    "contrib/roots/lineage-ledger.json",
    "contrib/roots/adaptation-manifest-29.3.json",
    "contrib/roots/replay-29.4-proposal/acceptance-evidence.json",
    "contrib/roots/core-29.4-migration-fixture.json",
    "contrib/roots/post-methodology-adaptations.json",
    "contrib/roots/continuous-accounting-pr.json",
    "contrib/roots/release-accounting.json",
)
spec = importlib.util.spec_from_file_location("accounting", ROOT / "contrib/devtools/roots-continuous-accounting.py")
ACCOUNTING = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ACCOUNTING)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def git(repository, *arguments, check=True):
    return subprocess.run(["git", "-C", repository, *arguments], check=check, capture_output=True, text=True).stdout.strip()


def deterministic_commit(repository, *arguments):
    environment = {**os.environ, "GIT_AUTHOR_DATE": "2000-01-01T00:00:00Z", "GIT_COMMITTER_DATE": "2000-01-01T00:00:00Z"}
    return subprocess.run(["git", "-C", repository, "commit", *arguments], check=True, capture_output=True, text=True, env=environment).stdout.strip()


class ReleaseEvidenceTest(unittest.TestCase):
    def write_release_accounting(self, root, anchor):
        manifest = json.loads((root / CANONICAL[1]).read_text())
        registry = json.loads((root / CANONICAL[4]).read_text())
        ownership = {path: unit["id"] for unit in manifest["units"] for path in unit["touched"]["paths"]}
        ownership.update({path: unit["id"] for unit in registry["units"] for path in unit["paths"]})
        head = git(root, "rev-parse", "HEAD")
        atoms = sorted(atom for atom in ACCOUNTING.atoms(root, anchor, head) if atom[0] not in {CANONICAL[-2], CANONICAL[-1]})
        changes = []
        for path, kind, atom_digest in atoms:
            changes.append({
                "path": path, "kind": kind, "digest": atom_digest, "disposition": "update",
                "adaptation": ownership.get(path, "unowned-test-adaptation"), "risk": "high",
                "tests": "ci/test/test_roots_pr_gate.py functional manifest doc generator test",
                "dependencies": "production Roots anchor", "provenance": "test fixture",
                "replay_impact": "production release delta", "scope": path, "rationale": "fixture",
            })
        anchor_tree = "sha1:" + git(root, "rev-parse", f"{anchor}^{{tree}}")
        value = {
            "schema_version": 2,
            "range": {"base_commit": "sha1:" + anchor, "base_tree": anchor_tree, "excluded_paths": [CANONICAL[-2], CANONICAL[-1]]},
            "changes": changes,
        }
        target = root / CANONICAL[-1]
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(value, sort_keys=True))

    def make_build_evidence(self, directory, source, revision):
        artifacts = Path(directory) / "artifacts"
        artifacts.mkdir(exist_ok=True)
        for name in (
            "bitcoin-roots-linux-x86_64.tar.gz",
            "bitcoin-roots-darwin-arm64.zip",
            "bitcoin-roots-windows-x86_64.zip",
        ):
            package = artifacts / name
            package.write_bytes(name.encode())
            subprocess.run([
                sys.executable, BUILD_SCRIPT, "attest", "--artifact", package,
                "--source-repository", source, "--source-revision", revision,
                "--output", artifacts / (name + ".build-attestation.json"),
            ], check=True)
        output_directory = Path(directory) / "build-evidence"
        output_directory.mkdir(exist_ok=True)
        subprocess.run([
            sys.executable, BUILD_SCRIPT, "aggregate", "--artifacts", artifacts,
            "--source-repository", source, "--source-revision", revision,
            "--expected-count", "3", "--output", "roots-release-build-evidence.json",
        ], cwd=output_directory, check=True)
        return output_directory / "roots-release-build-evidence.json"

    def fixture(self, directory, replay_mismatch=False):
        root = Path(directory) / "source"
        root.mkdir()
        git(root, "init", "--quiet")
        git(root, "config", "user.email", "test@example.invalid")
        git(root, "config", "user.name", "Test")
        (root / "base.txt").write_text("production Roots anchor\n")
        git(root, "add", "base.txt")
        deterministic_commit(root, "--quiet", "-m", "Roots release anchor")
        anchor = git(root, "rev-parse", "HEAD")
        anchor_tree = git(root, "rev-parse", "HEAD^{tree}")

        for relative in CANONICAL[:-1]:
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            if relative == CANONICAL[5]:
                target.write_text(json.dumps({"schema_version": 1, "changes": []}))
            else:
                shutil.copyfile(ROOT / relative, target)
        ledger_path = root / CANONICAL[0]
        ledger = json.loads(ledger_path.read_text())
        release = next(item for item in ledger["releases"] if item["id"] == "roots-29.3-roots.1")
        release["peeled_commit"] = "sha1:" + anchor
        release["tree"] = "sha1:" + anchor_tree
        ledger_path.write_text(json.dumps(ledger))

        replay_path = root / CANONICAL[2]
        replay = json.loads(replay_path.read_text())
        missing_commit = "f" * 40
        replay["reproducibility"]["canonical_lineage_commits"] = ["sha1:" + missing_commit] * 2
        if replay_mismatch:
            replay["candidate"]["tree"] = "sha1:" + "e" * 40
            replay["approval"]["candidate_tree"] = replay["candidate"]["tree"]
        replay.pop("digest")
        replay["digest"] = "sha256:" + hashlib.sha256(canonical(replay)).hexdigest()
        replay_path.write_text(json.dumps(replay))

        manifest = json.loads((root / CANONICAL[1]).read_text())
        owned = {path for unit in manifest["units"] for path in unit["touched"]["paths"]}
        registry_path = root / CANONICAL[4]
        registry = {"schema_version": 1, "units": [{
            "id": "roots-post-methodology-release-fixture-v1",
            "paths": sorted(path for path in (*CANONICAL[:-1], "release.txt") if path not in owned),
        }]}
        registry_path.write_text(json.dumps(registry))
        (root / "release.txt").write_text("production delta\n")
        git(root, "add", ".")
        deterministic_commit(root, "--quiet", "-m", "Production release delta")
        self.write_release_accounting(root, anchor)
        git(root, "add", CANONICAL[-1])
        deterministic_commit(root, "--amend", "--quiet", "--no-edit")
        revision = git(root, "rev-parse", "HEAD")
        self.assertNotEqual(
            subprocess.run(["git", "-C", root, "cat-file", "-e", missing_commit], capture_output=True).returncode,
            0,
        )
        return root, revision, self.make_build_evidence(directory, root, revision)

    def command(self, directory, source, revision, build, verify=False):
        command = [
            sys.executable, str(SCRIPT), "--ledger", source / CANONICAL[0], "--manifest", source / CANONICAL[1],
            "--replay-result", source / CANONICAL[2], "--fixture", source / CANONICAL[3],
            "--registry", source / CANONICAL[4], "--accounting", source / CANONICAL[5],
            "--release-accounting", source / CANONICAL[6], "--build-evidence", build,
            "--source-repository", source, "--source-revision", revision,
            "--candidate-tree", "sha1:" + git(source, "rev-parse", f"{revision}^{{tree}}"),
            "--output", "roots-release-evidence.json",
        ]
        if verify:
            command.append("--verify")
        return subprocess.run(command, cwd=directory, text=True, capture_output=True)

    def test_actual_branch_shape_accepts_absent_nonancestor_replay_commit(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source, revision, build = self.fixture(directory)
            result = self.command(directory, source, revision, build)
            self.assertEqual(result.returncode, 0, result.stderr)
            evidence = json.loads((directory / "roots-release-evidence.json").read_text())
            ledger = json.loads((source / CANONICAL[0]).read_text())
            anchor = next(item for item in ledger["releases"] if item["id"] == "roots-29.3-roots.1")
            self.assertEqual(evidence["production_anchor_commit"], anchor["peeled_commit"])
            self.assertEqual(evidence["future_base_replay_commits"], ["sha1:" + "f" * 40] * 2)
            self.assertEqual(self.command(directory, source, revision, build, verify=True).returncode, 0)

    def test_unrelated_production_ancestry_and_unowned_change_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source, _, build = self.fixture(directory)
            git(source, "checkout", "--quiet", "--orphan", "unrelated")
            git(source, "rm", "-rf", "--quiet", ".")
            (source / "other").write_text("other\n")
            git(source, "add", "other")
            deterministic_commit(source, "--quiet", "-m", "unrelated")
            self.assertNotEqual(self.command(directory, source, git(source, "rev-parse", "HEAD"), build).returncode, 0)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source, _, build = self.fixture(directory)
            ledger = json.loads((source / CANONICAL[0]).read_text())
            anchor = next(item for item in ledger["releases"] if item["id"] == "roots-29.3-roots.1")["peeled_commit"].split(":", 1)[1]
            (source / "unowned.cpp").write_text("int unowned;\n")
            git(source, "add", "unowned.cpp")
            deterministic_commit(source, "--quiet", "-m", "unowned source")
            self.write_release_accounting(source, anchor)
            git(source, "add", CANONICAL[-1])
            deterministic_commit(source, "--amend", "--quiet", "--no-edit")
            self.assertNotEqual(self.command(directory, source, git(source, "rev-parse", "HEAD"), build).returncode, 0)

    def test_stale_registry_accounting_and_mismatched_replay_tree_fail(self):
        for relative in (CANONICAL[4], CANONICAL[6]):
            with self.subTest(relative=relative), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                source, revision, build = self.fixture(directory)
                path = source / relative
                path.write_bytes(path.read_bytes() + b"\n")
                self.assertNotEqual(self.command(directory, source, revision, build).returncode, 0)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source, revision, build = self.fixture(directory, replay_mismatch=True)
            self.assertNotEqual(self.command(directory, source, revision, build).returncode, 0)

    def test_build_evidence_rejects_missing_duplicate_stale_and_tampered_attestations(self):
        for case in ("missing", "duplicate", "stale-tree", "package-mismatch"):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                source, revision, build = self.fixture(directory)
                artifacts = directory / "artifacts"
                attestation = next(artifacts.glob("*linux*.build-attestation.json"))
                package = artifacts / attestation.name.removesuffix(".build-attestation.json")
                if case == "missing":
                    attestation.unlink()
                elif case == "duplicate":
                    duplicate = artifacts / "duplicate"
                    duplicate.mkdir()
                    shutil.copy(attestation, duplicate / attestation.name)
                elif case == "stale-tree":
                    value = json.loads(attestation.read_text())
                    value["source_tree"] = "sha1:" + "0" * 40
                    value.pop("digest")
                    value["digest"] = "sha256:" + hashlib.sha256(canonical(value)).hexdigest()
                    attestation.write_text(json.dumps(value))
                else:
                    package.write_bytes(package.read_bytes() + b"tamper")
                command = [sys.executable, BUILD_SCRIPT, "aggregate", "--artifacts", artifacts, "--source-repository", source, "--source-revision", revision, "--expected-count", "3", "--output", "roots-release-build-evidence.json"]
                self.assertNotEqual(subprocess.run(command, cwd=directory, capture_output=True).returncode, 0)
                self.assertTrue(build.is_file())


if __name__ == "__main__":
    unittest.main()
