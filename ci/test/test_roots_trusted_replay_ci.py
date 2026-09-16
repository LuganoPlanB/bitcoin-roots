#!/usr/bin/env python3
"""Contract tests for the read-only scheduled/manual Roots replay workflow."""
from __future__ import annotations
import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github/workflows/roots-trusted-replay.yml"
SCRIPT = ROOT / "ci/roots-trusted-replay-gate.py"
REVIEW_SCRIPT = ROOT / "ci/roots-trusted-replay-review.py"
BUILD_SCRIPT = ROOT / "ci/roots-trusted-candidate-build.py"
spec = importlib.util.spec_from_file_location("trusted_replay_gate", SCRIPT)
GATE = importlib.util.module_from_spec(spec); spec.loader.exec_module(GATE)
review_spec = importlib.util.spec_from_file_location("trusted_replay_review", REVIEW_SCRIPT)
REVIEW = importlib.util.module_from_spec(review_spec); review_spec.loader.exec_module(REVIEW)
build_spec = importlib.util.spec_from_file_location("trusted_candidate_build", BUILD_SCRIPT)
BUILD = importlib.util.module_from_spec(build_spec); build_spec.loader.exec_module(BUILD)

class TrustedReplayCiTest(unittest.TestCase):
    def test_workflow_has_only_trusted_read_only_paths(self):
        text = WORKFLOW.read_text()
        for required in ("schedule:", "workflow_dispatch:", "contents: read", "CORE_29_3_COMMIT", "CORE_29_4_COMMIT", "CORE_29_3_TREE", "CORE_29_4_TREE", "LOCKED_INPUT_DIGEST", "fetch --no-tags https://github.com/bitcoin/bitcoin.git", "timeout-minutes: 10", "timeout-minutes: 20", "timeout-minutes: 60", "cancel-in-progress: true", "needs: [gate, fetch]", "actions/upload-artifact@65462800fd760344b1a7b4382951275a0abb4808"):
            self.assertIn(required, text)
        for forbidden in ("pull_request", "pull_request_target", "contents: write", "id-token: write", "secrets.", "actions/cache", "create tag", "git tag", "publish", "sign"):
            self.assertNotIn(forbidden, text)
        actions = re.findall(r"^\s*-\s+uses:\s*([^\s#]+)", text, re.MULTILINE)
        self.assertTrue(actions and all(re.fullmatch(r"actions/(?:checkout|upload-artifact|download-artifact)@[0-9a-f]{40}", action) for action in actions))
        for required in ("roots-trusted-candidate-build.py", "core-to-knots-29.3", "roots-29.3", "core-29.4", "timeout-minutes: 180"):
            self.assertIn(required, text)
        build_text = BUILD_SCRIPT.read_text()
        self.assertIn('"cmake", "--build"', build_text)
        self.assertIn('"ctest", "--test-dir"', build_text)
        self.assertIn('"-DENABLE_WALLET=ON"', build_text)
        self.assertIn('"-DWITH_BDB=OFF"', build_text)
        for invariant_test in ("feature_block.py", "p2p_segwit.py", "mempool_datacarrier.py"):
            self.assertIn(invariant_test, build_text)

    def test_tree_valid_candidate_still_fails_on_build_error(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            repository = root / "candidate"
            repository.mkdir()
            tree = "a" * 40
            responses = [
                mock.Mock(returncode=0, stdout=tree + "\n"),
                mock.Mock(returncode=0),
                mock.Mock(returncode=1),
            ]
            with mock.patch.object(BUILD.subprocess, "run", side_effect=responses):
                with self.assertRaisesRegex(ValueError, "candidate acceptance command failed"):
                    BUILD.build_and_test(repository.resolve(), tree, root / "new-build", 2)
    def test_decisions_locks_and_failures_are_deterministic(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "trusted-replay-report.json"
            args = ("manual", "false", "none", ROOT / "contrib/roots/adaptation-manifest-29.3.json", ROOT / "contrib/roots/methodology-v1.json", ROOT / "contrib/roots/core-29.4-migration-fixture.json")
            first = GATE.report(*args); second = GATE.report(*args)
            self.assertEqual(first, second); self.assertEqual(first["decision"], "no-op")
            self.assertEqual(GATE.decision("schedule", "false"), "replay")
            self.assertEqual(GATE.decision("manual", "false", "auto"), "no-op")
            self.assertEqual(GATE.decision("manual", "false", "force"), "replay")
            for later_base in ("core-29.4", "branch-name", "https://example.invalid/core.git", "0" * 40):
                with self.subTest(later_base=later_base), self.assertRaises(GATE.GateError):
                    GATE.report("manual", "true", later_base, *args[3:])
            fixture = json.loads((ROOT / "contrib/roots/core-29.4-migration-fixture.json").read_text()); fixture["core_inputs"]["v29.3"]["tree"] = "z" * 40
            bad = Path(directory) / "fixture.json"; bad.write_text(json.dumps(fixture))
            with self.assertRaises(GATE.GateError): GATE.report("manual", "true", "none", *args[3:5], bad)
            fixture["core_inputs"]["v29.3"]["tree"] = "0" * 40; bad.write_text(json.dumps(fixture))
            with self.assertRaises(GATE.GateError): GATE.report("manual", "true", "none", *args[3:5], bad)
            output.write_bytes(b"occupied")
            self.assertTrue(output.exists())

    def test_artifact_manifest_rejects_extra_large_and_digest_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); expected = {}
            for name in GATE.EXPECTED_ARTIFACTS:
                path = root / name; path.write_text(name); expected[name] = GATE.digest(path)
            GATE.verify_artifacts(root, expected)
            (root / "extra").write_text("x")
            with self.assertRaises(GATE.GateError): GATE.verify_artifacts(root, expected)
            (root / "extra").unlink(); (root / "replay-review.txt").write_bytes(b"x" * (GATE.MAX_REPORT_BYTES + 1))
            with self.assertRaises(GATE.GateError): GATE.verify_artifacts(root, expected)

    def test_review_uses_frozen_role_expectations_and_rejects_mutations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); artifacts = root / "artifacts"; roles = {}
            for role in ("core-to-knots-29.3", "roots-29.3", "core-29.4"):
                digests = {}
                for run in ("a", "b"):
                    target = artifacts / role / run; target.mkdir(parents=True)
                    state = {"candidate_tree": "a" * 40, "completed_units": [role]}
                    for kind, name in REVIEW.NAMES.items():
                        path = target / name
                        path.write_text(json.dumps(state, sort_keys=True) if kind == "state" else f"{role}-{kind}")
                        digests[kind] = REVIEW.sha(path)
                    digests["outcome_set"] = "sha256:" + __import__("hashlib").sha256(json.dumps(state["completed_units"], sort_keys=True, separators=(",", ":")).encode()).hexdigest()
                roles[role] = {"target_tree": "sha1:" + "a" * 40, "artifact_digests": digests}
            methodology = root / "methodology.json"; methodology.write_text(json.dumps({"reconstructions": roles}))
            REVIEW.verify(methodology, artifacts)
            for role in roles:
                for run in ("a", "b"):
                    export = artifacts / role / run / REVIEW.NAMES["generated_export"]
                    export.write_bytes(b"p" * 70_000)
                    roles[role]["artifact_digests"]["generated_export"] = REVIEW.sha(export)
            methodology.write_text(json.dumps({"reconstructions": roles}))
            REVIEW.verify(methodology, artifacts)
            export.write_bytes(b"p" * (REVIEW.CAPS[REVIEW.NAMES["generated_export"]] + 1))
            with self.assertRaises(ValueError): REVIEW.verify(methodology, artifacts)
            export.write_bytes(b"p" * 70_000)
            (artifacts / "roots-29.3" / "b" / REVIEW.NAMES["report_text"]).write_text("diverged")
            with self.assertRaises(ValueError): REVIEW.verify(methodology, artifacts)

    def test_report_bound_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "trusted-replay-report.json"
            argv = [str(SCRIPT), "--mode", "manual", "--changed", "true", "--later-base", "none", "--manifest", str(ROOT / "contrib/roots/adaptation-manifest-29.3.json"), "--methodology", str(ROOT / "contrib/roots/methodology-v1.json"), "--fixture", str(ROOT / "contrib/roots/core-29.4-migration-fixture.json"), "--output", str(output)]
            with mock.patch.object(GATE, "report", return_value={"large": "x" * 100}), mock.patch.object(GATE, "MAX_REPORT_BYTES", 1), mock.patch("sys.argv", argv):
                self.assertEqual(GATE.main(), 1)
            self.assertFalse(output.exists())

if __name__ == "__main__": unittest.main()
