#!/usr/bin/env python3
"""Focused immutable 29.3/29.4 production-accounting contract tests."""

import copy
import importlib.util
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
RECORD = ROOT / "contrib/roots/production-accounting-29.4.json"
TOOL = ROOT / "contrib/devtools/roots-production-accounting.py"
spec = importlib.util.spec_from_file_location("production_accounting", TOOL)
ACCOUNTING = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ACCOUNTING)


def build_evidence(record):
    frozen = record["releases"][1]["frozen"]
    return {
        "matrix": ["linux", "macos", "windows"],
        "release_source_commit": frozen["commit"],
        "release_source_tree": frozen["tree"],
        "status": "pass",
    }


class ProductionAccountingTest(unittest.TestCase):
    def setUp(self):
        self.record = json.loads(RECORD.read_text(encoding="utf-8"))
        self.build = build_evidence(self.record)

    def validate(self, record=None, build=None, public_release=False):
        # Exercise the public API with temporary regular JSON files so that its
        # path and symlink protections remain part of the contract.
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            record_path = directory / "production-accounting-29.4.json"
            build_path = directory / "roots-release-build-evidence.json"
            record_path.write_text(json.dumps(record or self.record), encoding="utf-8")
            build_path.write_text(json.dumps(build or self.build), encoding="utf-8")
            return ACCOUNTING.validate(record_path, ROOT, build_path, public_release)

    def test_checked_in_record_rehearses_deterministically_twice(self):
        first = self.validate()
        second = self.validate()
        self.assertEqual(first, second)
        self.assertTrue(first.startswith("sha256:"))

    def test_cross_anchor_wrong_target_unowned_commit_and_changed_atom_fail(self):
        cross_anchor = copy.deepcopy(self.record)
        cross_anchor["releases"][1]["replay_tree"] = cross_anchor["releases"][1]["frozen"]["tree"]
        wrong_target = copy.deepcopy(self.record)
        wrong_target["releases"][1]["canonical"]["commit"] = wrong_target["releases"][1]["frozen"]["commit"]
        unowned = copy.deepcopy(self.record)
        unowned["releases"][1]["production_commits"].append(copy.deepcopy(unowned["releases"][1]["production_commits"][0]))
        changed_atom = copy.deepcopy(self.record)
        changed_atom["releases"][1]["production_commits"][0]["atom_digest"] = "sha256:" + "0" * 64
        for value in (cross_anchor, wrong_target, unowned, changed_atom):
            with self.subTest(value=value["releases"][1]["id"]):
                with self.assertRaises(ACCOUNTING.AccountingError):
                    self.validate(record=value)

    def test_missing_approval_and_source_build_mismatch_fail_closed(self):
        with self.assertRaises(ACCOUNTING.AccountingError):
            self.validate(public_release=True)
        mismatch = copy.deepcopy(self.build)
        mismatch["release_source_tree"] = "sha1:" + "0" * 40
        with self.assertRaises(ACCOUNTING.AccountingError):
            self.validate(build=mismatch)

    def test_preserved_history_and_bound_paths_fail_closed(self):
        for key in ("source_commit", "source_tree"):
            altered = copy.deepcopy(self.record)
            altered["releases"][0][key] = "sha1:" + "0" * 40
            with self.subTest(history=key), self.assertRaises(ACCOUNTING.AccountingError):
                self.validate(record=altered)
        for path in ("/tmp/production-accounting.json", "../production-accounting.json"):
            altered = copy.deepcopy(self.record)
            altered["releases"][1]["topology"]["path"] = path
            with self.subTest(path=path), self.assertRaises(ACCOUNTING.AccountingError):
                self.validate(record=altered)
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            (directory / "outside.json").write_text("{}", encoding="utf-8")
            (directory / "inside").mkdir()
            (directory / "inside" / "link.json").symlink_to(directory / "outside.json")
            with self.assertRaises(ACCOUNTING.AccountingError):
                ACCOUNTING.bound_file(directory, "inside/link.json", "fixture")


if __name__ == "__main__":
    unittest.main()
