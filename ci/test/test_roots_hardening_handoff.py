#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Tests for the 29.4 hardening handoff contract and change gate."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "contrib/devtools/roots-hardening-handoff.py"
ACCOUNTING_TOOL = ROOT / "contrib/devtools/roots-continuous-accounting.py"
PLAN = ROOT / ".gestalt/cpp-hardening-and-backports.org"
CONTRACT = ROOT / "contrib/roots/hardening-handoff-v1.json"
CONSTRUCTOR = ROOT / "contrib/roots/replay-29.4-proposal/canonical-lineage.bash"
VALIDATION_REF = "refs/remotes/origin/ci/l7-validation/39a5e302"
ROOT_COMMIT = "cbc88cff9b35b95a549c0313e424e13093fcd6a1"
ROOT_TREE = "39a5e30207a09962e78ae81c24cc65b1e478ef90"


def run(command, *, cwd=None, check=True, env=None):
    result = subprocess.run(
        [str(value) for value in command],
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    )
    if check and result.returncode:
        raise AssertionError(
            f"command failed ({result.returncode}): {' '.join(str(value) for value in command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def git(repository, *arguments):
    return run(["git", "-C", repository, *arguments]).stdout.strip()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def sha256(path):
    return "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()


def load_accounting():
    spec = importlib.util.spec_from_file_location("handoff_test_accounting", ACCOUNTING_TOOL)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ACCOUNTING = load_accounting()


class HardeningHandoffTest(unittest.TestCase):
    maxDiff = None

    def test_plan_contract_is_complete_and_pinned(self):
        result = run([sys.executable, TOOL, "validate", "--plan", PLAN, "--contract", CONTRACT])
        report = json.loads(result.stdout)
        self.assertEqual(report["decision"], "accepted")
        self.assertEqual(report["original_milestones"], 59)
        self.assertEqual(report["original_l1"], 11)
        self.assertEqual(report["original_l2"], 48)
        self.assertEqual(report["dispositions"], {"merged": 6, "obsolete": 0, "retained": 46, "superseded": 7})
        self.assertEqual(
            report["implementation_milestones"],
            ["implement-ci-identity-seam", "implement-qt-presentation-descriptor"],
        )
        self.assertEqual(report["baseline"]["core_tag"], "v29.4")
        self.assertEqual(report["baseline"]["candidate_tree"], "sha1:" + ROOT_TREE)

    def assert_stable_failure(self, plan, contract, code):
        diagnostics = []
        safe_stops = []
        for index in range(2):
            output = plan.parent / f"report-{index}.json"
            result = run(
                [sys.executable, TOOL, "validate", "--plan", plan, "--contract", contract, "--output", output],
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(result.stderr.startswith(code + ":"), result.stderr)
            diagnostics.append(result.stderr)
            safe_path = plan.parent / "hardening-safe-stop.json"
            safe_stops.append(safe_path.read_bytes())
            self.assertEqual(json.loads(safe_path.read_text())["code"], code)
        self.assertEqual(*diagnostics)
        self.assertEqual(*safe_stops)

    def test_plan_mutations_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            original_plan = PLAN.read_text(encoding="utf-8")
            original_contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
            canonical_plan = work / "plan.org"
            canonical_plan.write_text(original_plan, encoding="utf-8")

            missing_property = work / "missing-property.org"
            missing_property.write_text(original_plan.replace(":REPLAY_ACCEPTANCE: required\n", "", 1), encoding="utf-8")
            contract = work / "contract.json"
            contract.write_text(canonical(original_contract) + "\n", encoding="utf-8")
            self.assert_stable_failure(missing_property, contract, "E_L1_ACCEPTANCE")

            stale = work / "stale.org"
            stale.write_text(original_plan + "\nStart from commit =c0febdd25d036a6188259fd66e0f9ed37eebc1b1=\n", encoding="utf-8")
            self.assert_stable_failure(stale, contract, "E_STALE_29_3")

            incomplete_value = json.loads(CONTRACT.read_text(encoding="utf-8"))
            incomplete_value["disposition_groups"][-1]["milestone_ids"].remove("terminal-hardening-review")
            incomplete = work / "incomplete.json"
            incomplete.write_text(canonical(incomplete_value) + "\n", encoding="utf-8")
            self.assert_stable_failure(canonical_plan, incomplete, "E_MAPPING_INCOMPLETE")

            duplicate_value = json.loads(CONTRACT.read_text(encoding="utf-8"))
            duplicate_value["disposition_groups"][0]["milestone_ids"].append("risk-baseline")
            duplicate = work / "duplicate.json"
            duplicate.write_text(canonical(duplicate_value) + "\n", encoding="utf-8")
            self.assert_stable_failure(canonical_plan, duplicate, "E_MAPPING_DUPLICATE")

            duplicate_authority = work / "duplicate-authority.org"
            duplicate_authority.write_text(
                original_plan + "\nAdd a versioned release checklist/report schema\n",
                encoding="utf-8",
            )
            self.assert_stable_failure(duplicate_authority, contract, "E_DUPLICATE_AUTHORITY")

    def prepare_canonical(self, work):
        source = work / "source"
        canonical_repo = work / "canonical"
        run(["git", "clone", "--quiet", "--no-local", ROOT, source])
        git(source, "fetch", "--quiet", "--no-tags", ROOT, f"{VALIDATION_REF}:{VALIDATION_REF}")
        result = run([CONSTRUCTOR, source, canonical_repo])
        self.assertIn("target_commit=" + ROOT_COMMIT, result.stdout)
        self.assertEqual(git(canonical_repo, "rev-parse", "HEAD^{tree}"), ROOT_TREE)
        return canonical_repo

    def configure(self, repository):
        git(repository, "config", "user.email", "hardening-rehearsal@example.invalid")
        git(repository, "config", "user.name", "Hardening rehearsal")

    def commit(self, repository, message):
        environment = {
            **os.environ,
            "GIT_AUTHOR_DATE": "2001-01-01T00:00:00Z",
            "GIT_COMMITTER_DATE": "2001-01-01T00:00:00Z",
        }
        run(["git", "-C", repository, "commit", "--quiet", "-m", message], env=environment)
        return git(repository, "rev-parse", "HEAD")

    def accounting_record(self, repository, base, head, adaptation, tests, path):
        changes = []
        for changed_path, kind, digest in ACCOUNTING.atoms(repository, base, head):
            changes.append(
                {
                    "path": changed_path,
                    "kind": kind,
                    "digest": digest,
                    "disposition": "update",
                    "adaptation": adaptation,
                    "risk": "medium",
                    "tests": tests,
                    "dependencies": "accepted-29.4-candidate",
                    "provenance": "L10.6 disposable hardening rehearsal",
                    "replay_impact": "replay",
                    "scope": changed_path,
                    "rationale": "exercise the hardening handoff gate",
                }
            )
        path.write_text(canonical({"schema_version": 1, "changes": changes}) + "\n", encoding="utf-8")
        return len(changes)

    def gate(self, repository, base, head, change_class, action, adaptation, tests, work):
        record = work / "accounting.json"
        atom_count = self.accounting_record(repository, base, head, adaptation, tests, record)
        evidence = {
            "schema_version": 1,
            "change_class": change_class,
            "adaptation_action": action,
            "adaptation_ids": [adaptation],
            "tests": [tests],
            "invariants": {
                "core_consensus_compatible": "pass",
                "local_policy_outside_block_validity": "pass",
                "rdts_bip110_enforcement_absent": "pass",
            },
            "boundary_scope": {
                "consensus_paths": [],
                "policy_paths": [],
                "build_registration_paths": [],
                "rdts_bip110_diff": False,
            },
            "replay": {
                "base_tag": "v29.4",
                "root_commit": "sha1:" + ROOT_COMMIT,
                "root_tree": "sha1:" + ROOT_TREE,
                "base_commit": "sha1:" + base,
                "head_commit": "sha1:" + head,
                "head_tree": "sha1:" + git(repository, "rev-parse", "HEAD^{tree}"),
                "first_parent": True,
                "merge_commits": 0,
            },
            "accounting_record_sha256": sha256(record),
        }
        evidence_path = work / "evidence.json"
        evidence_path.write_text(canonical(evidence) + "\n", encoding="utf-8")
        output = work / "report.json"
        result = run(
            [
                sys.executable,
                TOOL,
                "gate",
                "--repository",
                repository,
                "--base",
                base,
                "--head",
                head,
                "--evidence",
                evidence_path,
                "--record",
                record,
                "--contract",
                CONTRACT,
                "--output",
                output,
            ]
        )
        self.assertEqual(json.loads(result.stdout), json.loads(output.read_text()))
        report = json.loads(output.read_text())
        self.assertEqual(report["accounted_atoms"], atom_count)
        return report, evidence_path, record

    def test_documentation_and_cpp_fix_rehearse_through_actual_gate(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            canonical_repo = self.prepare_canonical(work)
            summaries = {}

            docs = work / "docs"
            run(["git", "clone", "--quiet", canonical_repo, docs])
            self.configure(docs)
            tags_before = git(docs, "for-each-ref", "--format=%(refname) %(objectname)", "refs/tags")
            base = git(docs, "rev-parse", "HEAD")
            doc_path = docs / "doc/hardening-handoff-rehearsal.md"
            doc_path.write_text("Documentation-only rehearsal; runtime behavior is unchanged.\n", encoding="utf-8")
            git(docs, "add", doc_path.relative_to(docs))
            head = self.commit(docs, "rehearsal: preserve behavior for documentation")
            docs_evidence = work / "docs-evidence"
            docs_evidence.mkdir()
            docs_report, _, _ = self.gate(
                docs, base, head, "documentation-noop", "preserve",
                "roots-roots-documentation-documentation", "documentation-noop fixture", docs_evidence,
            )
            self.assertEqual(docs_report["decision"], "accepted")
            self.assertEqual(docs_report["invariants"]["rdts_bip110_enforcement_absent"], "pass")
            self.assertEqual(tags_before, git(docs, "for-each-ref", "--format=%(refname) %(objectname)", "refs/tags"))
            summaries["documentation-noop"] = docs_report

            cpp = work / "cpp"
            run(["git", "clone", "--quiet", canonical_repo, cpp])
            self.configure(cpp)
            tags_before = git(cpp, "for-each-ref", "--format=%(refname) %(objectname)", "refs/tags")
            base = git(cpp, "rev-parse", "HEAD")
            fixture_path = cpp / "src/hardening_handoff_rehearsal.cpp"
            fixture_path.write_text(
                "// Disposable fix fixture: the documented maximum is inclusive.\n"
                "bool IsWithinMaximum(unsigned value, unsigned maximum) { return value <= maximum; }\n",
                encoding="utf-8",
            )
            git(cpp, "add", fixture_path.relative_to(cpp))
            head = self.commit(cpp, "rehearsal: fix inclusive C++ boundary")
            cpp_evidence = work / "cpp-evidence"
            cpp_evidence.mkdir()
            cpp_report, evidence_path, record = self.gate(
                cpp, base, head, "cpp-fix", "update",
                "roots-roots-common-maintenance", "inclusive boundary fixture assertion", cpp_evidence,
            )
            self.assertEqual(cpp_report["decision"], "accepted")
            self.assertIn("src/hardening_handoff_rehearsal.cpp", cpp_report["changed_paths"])
            self.assertEqual(cpp_report["replay_root"]["tree"], "sha1:" + ROOT_TREE)
            self.assertEqual(cpp_report["boundary_scope"]["consensus_paths"], [])
            self.assertEqual(
                cpp_report["invariant_baseline_sha256"],
                "sha256:7133b96f479a0929ca319a7ac07374dd84a86169681ba84904f537040bc15346",
            )
            self.assertEqual(tags_before, git(cpp, "for-each-ref", "--format=%(refname) %(objectname)", "refs/tags"))
            summaries["cpp-fix"] = cpp_report

            second_path = cpp / "doc/hardening-handoff-second-commit.md"
            second_path.write_text("A second commit must be gated separately.\n", encoding="utf-8")
            git(cpp, "add", second_path.relative_to(cpp))
            second_head = self.commit(cpp, "rehearsal: second cohesive change")
            range_failure = run(
                [
                    sys.executable, TOOL, "gate", "--repository", cpp,
                    "--base", base, "--head", second_head, "--evidence", evidence_path,
                    "--record", record, "--contract", CONTRACT,
                    "--output", cpp_evidence / "failed-report.json",
                ],
                check=False,
            )
            self.assertTrue(range_failure.stderr.startswith("E_COHESIVE_COMMIT:"), range_failure.stderr)

            failed = json.loads(evidence_path.read_text())
            failed["boundary_scope"]["consensus_paths"] = ["src/consensus/fake.cpp"]
            evidence_path.write_text(canonical(failed) + "\n", encoding="utf-8")
            boundary_failure = run(
                [
                    sys.executable, TOOL, "gate", "--repository", cpp,
                    "--base", base, "--head", head, "--evidence", evidence_path,
                    "--record", record, "--contract", CONTRACT,
                    "--output", cpp_evidence / "failed-report.json",
                ],
                check=False,
            )
            self.assertTrue(boundary_failure.stderr.startswith("E_BOUNDARY_SCOPE:"), boundary_failure.stderr)

            failed = json.loads(evidence_path.read_text())
            failed["boundary_scope"]["consensus_paths"] = []
            failed["invariants"]["rdts_bip110_enforcement_absent"] = "fail"
            evidence_path.write_text(canonical(failed) + "\n", encoding="utf-8")
            diagnostics = []
            safe_stops = []
            for _ in range(2):
                result = run(
                    [
                        sys.executable, TOOL, "gate", "--repository", cpp,
                        "--base", base, "--head", head, "--evidence", evidence_path,
                        "--record", record, "--contract", CONTRACT,
                        "--output", cpp_evidence / "failed-report.json",
                    ],
                    check=False,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(result.stderr.startswith("E_INVARIANTS:"), result.stderr)
                diagnostics.append(result.stderr)
                safe_stops.append((cpp_evidence / "hardening-safe-stop.json").read_bytes())
            self.assertEqual(*diagnostics)
            self.assertEqual(*safe_stops)

            summary = {
                "schema_version": 1,
                "baseline": {"tag": "v29.4", "commit": "sha1:" + ROOT_COMMIT, "tree": "sha1:" + ROOT_TREE},
                "rehearsals": summaries,
                "safe_stop": json.loads(safe_stops[0]),
            }
            summary_path = work / "hardening-rehearsal-summary.json"
            summary_path.write_text(canonical(summary) + "\n", encoding="utf-8")
            self.assertEqual(json.loads(summary_path.read_text())["safe_stop"]["decision"], "safe-stop")


if __name__ == "__main__":
    unittest.main()
