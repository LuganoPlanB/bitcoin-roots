# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "accounting",
    ROOT / "contrib/devtools/roots-continuous-accounting.py",
)
ACCOUNTING = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ACCOUNTING)


def git(repository, *args):
    return subprocess.run(
        ["git", "-C", str(repository), *args],
        check=True,
        text=True,
        capture_output=True,
    )


def oid(repository):
    return git(repository, "rev-parse", "HEAD").stdout.strip()


def normal_entry(path, kind, digest, disposition="update"):
    return {
        "path": path,
        "kind": kind,
        "digest": digest,
        "disposition": disposition,
        "adaptation": "roots:unit",
        "risk": "low",
        "tests": "unit",
        "dependencies": "none",
        "provenance": "local",
        "replay_impact": "replay",
        "scope": path,
        "rationale": "specific hunk",
    }


class ContinuousAccountingTest(unittest.TestCase):
    def test_malformed_envelope_and_disposition_fail_closed(self):
        for value in (
            {"schema_version": True, "changes": []},
            {"schema_version": 1, "changes": [{"disposition": {}}]},
            {"schema_version": 1, "changes": [{"disposition": []}]},
        ):
            with self.assertRaises(ValueError):
                ACCOUNTING.validate(value)

    def test_seven_documented_then_undocumented_atom(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory) / "repo"
            subprocess.run(["git", "init", str(repo)], check=True, capture_output=True)
            git(repo, "config", "user.email", "test@example.invalid")
            git(repo, "config", "user.name", "Test")
            documented = {
                "ordinary.txt": "update",
                "feature.txt": "add",
                "upstream.txt": "absorb",
                "generated.txt": "update",
                "docs.txt": "exempt",
                "revert.txt": "update",
                "security.txt": "embargoed",
            }
            for path in documented:
                (repo / path).write_text("base\n")
            git(repo, "add", ".")
            git(repo, "commit", "-m", "base")
            base = oid(repo)
            for path in documented:
                (repo / path).write_text("changed\n")
            git(repo, "commit", "-am", "documented")
            documented_head = oid(repo)
            observed = ACCOUNTING.atoms(repo, base, documented_head)
            self.assertEqual({path for path, _, _ in observed}, set(documented))
            changes = []
            for path, kind, digest in observed:
                disposition = documented[path]
                if disposition == "embargoed":
                    changes.append(
                        {
                            "path": path,
                            "kind": kind,
                            "digest": digest,
                            "disposition": disposition,
                            "adaptation": "roots:security",
                            "tracking_reference": "SEC-2026-001",
                            "reconcile_by": "2026-12-31",
                            "reconciliation_state": "pending",
                        }
                    )
                else:
                    changes.append(normal_entry(path, kind, digest, disposition))
            record = {"schema_version": 1, "changes": changes}
            ACCOUNTING.validate(record, observed)
            with self.assertRaises(ValueError):
                ACCOUNTING.validate(record, observed, public_release=True)
            reconciled = [
                item for item in changes if item["disposition"] != "embargoed"
            ]
            for path, kind, digest in observed:
                if path == "security.txt":
                    entry = normal_entry(path, kind, digest)
                    entry.update(
                        {
                            "adaptation": "roots:security",
                            "risk": "high",
                            "tests": "security",
                            "provenance": "reconciled",
                            "rationale": "security reconciliation",
                        }
                    )
                    reconciled.append(entry)
            ACCOUNTING.validate(
                {"schema_version": 1, "changes": reconciled},
                observed,
                public_release=True,
            )
            for mutate in (
                lambda item: item.__setitem__("tracking_reference", "bad reference"),
                lambda item: item.__setitem__("reconcile_by", "2026-02-30"),
            ):
                invalid = [{**item} for item in changes]
                embargoed_entry = next(
                    item for item in invalid if item["disposition"] == "embargoed"
                )
                mutate(embargoed_entry)
                with self.assertRaises(ValueError):
                    ACCOUNTING.validate(
                        {"schema_version": 1, "changes": invalid},
                        observed,
                    )
            for scope in ("cohesive", "*", "ordinary.txt"):
                invalid = [{**item} for item in reconciled]
                feature_entry = next(
                    item for item in invalid if item["path"] == "feature.txt"
                )
                feature_entry["scope"] = scope
                with self.assertRaises(ValueError):
                    ACCOUNTING.validate(
                        {"schema_version": 1, "changes": invalid},
                        observed,
                    )
            (repo / "undocumented.txt").write_text("drift\n")
            git(repo, "add", ".")
            git(repo, "commit", "-m", "drift")
            head = oid(repo)
            with self.assertRaises(ValueError):
                ACCOUNTING.validate(
                    {"schema_version": 1, "changes": changes},
                    ACCOUNTING.atoms(repo, base, head),
                )

    def test_mixed_repository_atoms_are_exact_and_mutation_resistant(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory) / "repo"
            subprocess.run(["git", "init", str(repo)], check=True, capture_output=True)
            for key, value in (
                ("user.email", "test@example.invalid"),
                ("user.name", "Test"),
            ):
                git(repo, "config", key, value)
            (repo / "old.txt").write_text("old\n")
            (repo / "hunks.txt").write_text("a\nkeep\nb\nkeep\nc\n")
            (repo / "mode.sh").write_text("x\n")
            (repo / "both.sh").write_text("old\n")
            (repo / "bin.dat").write_bytes(b"\x00a")
            git(repo, "add", ".")
            git(repo, "commit", "-m", "base")
            base = oid(repo)
            (repo / "old.txt").unlink()
            (repo / "new.txt").write_text("new\n")
            (repo / "hunks.txt").write_text("A\nkeep\nb\nkeep\nC\n")
            (repo / "bin.dat").write_bytes(b"\x00b")
            (repo / "mode.sh").chmod(0o755)
            (repo / "both.sh").write_text("new\n")
            (repo / "both.sh").chmod(0o755)
            git(repo, "add", "-A")
            git(repo, "commit", "-m", "mixed")
            head = oid(repo)
            atoms = ACCOUNTING.atoms(repo, base, head)
            self.assertGreaterEqual(len(atoms), 5)
            self.assertTrue(
                {"new.txt", "old.txt", "bin.dat", "mode.sh", "both.sh", "hunks.txt"}
                <= {path for path, _, _ in atoms}
            )
            self.assertTrue(
                {"add", "delete", "binary", "mode"}
                <= {kind for _, kind, _ in atoms}
            )
            self.assertEqual(
                sum(
                    path == "hunks.txt" and kind == "modify"
                    for path, kind, _ in atoms
                ),
                2,
            )
            self.assertEqual(
                {kind for path, kind, _ in atoms if path == "mode.sh"},
                {"mode"},
            )
            self.assertEqual(
                {kind for path, kind, _ in atoms if path == "both.sh"},
                {"mode", "modify"},
            )
            self.assertEqual(
                sum(path == "both.sh" and kind == "mode" for path, kind, _ in atoms),
                1,
            )
            self.assertEqual(
                sum(path == "both.sh" and kind == "modify" for path, kind, _ in atoms),
                1,
            )
            changes = [
                normal_entry(path, kind, digest) for path, kind, digest in atoms
            ]
            ACCOUNTING.validate(
                {"schema_version": 1, "changes": changes},
                atoms,
            )
            mutations = (
                changes[:-1],
                changes + [{**changes[0], "digest": "sha256:" + "0" * 64}],
                [{**item, "digest": "sha256:" + "1" * 64} for item in changes],
            )
            for mutated in mutations:
                with self.assertRaises(ValueError):
                    ACCOUNTING.validate(
                        {"schema_version": 1, "changes": mutated},
                        atoms,
                    )
            reassigned = [{**item} for item in changes]
            reassigned[0]["path"] = "wrong.txt"
            reassigned[0]["scope"] = "wrong.txt"
            with self.assertRaises(ValueError):
                ACCOUNTING.validate(
                    {"schema_version": 1, "changes": reassigned},
                    atoms,
                )

    def test_repository_atoms_require_exact_ownership(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory) / "repo"
            subprocess.run(
                ["git", "init", str(repo)],
                check=True,
                capture_output=True,
            )
            git(repo, "config", "user.email", "test@example.invalid")
            git(repo, "config", "user.name", "Test")
            (repo / "a.txt").write_text("one\ntwo\n")
            git(repo, "add", ".")
            git(repo, "commit", "-m", "base")
            base = oid(repo)
            (repo / "a.txt").write_text("one\nchanged\nextra\n")
            git(repo, "commit", "-am", "change")
            head = oid(repo)
            atoms = ACCOUNTING.atoms(repo, base, head)
            changes = [
                normal_entry(path, kind, digest) for path, kind, digest in atoms
            ]
            ACCOUNTING.validate(
                {"schema_version": 1, "changes": changes},
                atoms,
            )
            with self.assertRaises(ValueError):
                ACCOUNTING.validate(
                    {"schema_version": 1, "changes": changes[:-1]},
                    atoms,
                )
            extra = changes + [
                {**changes[0], "digest": "sha256:" + "0" * 64}
            ]
            with self.assertRaises(ValueError):
                ACCOUNTING.validate(
                    {"schema_version": 1, "changes": extra},
                    atoms,
                )
            wildcard = [{**changes[0], "path": "*"}]
            with self.assertRaises(ValueError):
                ACCOUNTING.validate(
                    {"schema_version": 1, "changes": wildcard},
                    atoms,
                )
            embargoed = []
            for item in changes:
                embargoed.append(
                    {
                        "path": item["path"],
                        "kind": item["kind"],
                        "digest": item["digest"],
                        "disposition": "embargoed",
                        "adaptation": item["adaptation"],
                        "tracking_reference": "SEC-2026-001",
                        "reconcile_by": "2026-12-31",
                        "reconciliation_state": "pending",
                    }
                )
            ACCOUNTING.validate(
                {"schema_version": 1, "changes": embargoed},
                atoms,
            )
            with self.assertRaises(ValueError):
                ACCOUNTING.validate(
                    {"schema_version": 1, "changes": embargoed},
                    atoms,
                    public_release=True,
                )
            ACCOUNTING.validate(
                {"schema_version": 1, "changes": changes},
                atoms,
                public_release=True,
            )
            for mutate in (
                lambda item: item.pop("tracking_reference"),
                lambda item: item.__setitem__("tracking_reference", ""),
                lambda item: item.__setitem__("reconcile_by", ""),
                lambda item: item.__setitem__("reconciliation_state", "done"),
                lambda item: item.__setitem__("risk", "leaked"),
            ):
                invalid = [{**item} for item in embargoed]
                mutate(invalid[0])
                with self.assertRaises(ValueError):
                    ACCOUNTING.validate(
                        {"schema_version": 1, "changes": invalid},
                        atoms,
                    )
