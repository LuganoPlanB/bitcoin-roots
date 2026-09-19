#!/usr/bin/env python3
"""Contract tests for the read-only scheduled/manual Roots replay workflow."""
from __future__ import annotations
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import textwrap
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github/workflows/roots-trusted-replay.yml"
ATLAS_LOCK = ROOT / "contrib/roots/atlas-input-lock-29.3.json"
LINEAGE_LEDGER = ROOT / "contrib/roots/lineage-ledger.json"
SCRIPT = ROOT / "ci/roots-trusted-replay-gate.py"
REVIEW_SCRIPT = ROOT / "ci/roots-trusted-replay-review.py"
BUILD_SCRIPT = ROOT / "ci/roots-trusted-candidate-build.py"
spec = importlib.util.spec_from_file_location("trusted_replay_gate", SCRIPT)
GATE = importlib.util.module_from_spec(spec)
spec.loader.exec_module(GATE)
review_spec = importlib.util.spec_from_file_location("trusted_replay_review", REVIEW_SCRIPT)
REVIEW = importlib.util.module_from_spec(review_spec)
review_spec.loader.exec_module(REVIEW)
build_spec = importlib.util.spec_from_file_location("trusted_candidate_build", BUILD_SCRIPT)
BUILD = importlib.util.module_from_spec(build_spec)
build_spec.loader.exec_module(BUILD)

class TrustedReplayCiTest(unittest.TestCase):
    @staticmethod
    def workflow_env(text, name):
        match = re.search(rf"(?m)^  {re.escape(name)}: (.+)$", text)
        if match is None:
            raise AssertionError(f"workflow environment value {name!r} is missing")
        return match.group(1).strip("'\"")

    @staticmethod
    def replay_shell(text):
        start = text.index("  replay:\n")
        end = text.index("  review:\n", start)
        section = text[start:end]
        match = re.search(r"(?ms)^      - run: \|\n(?P<body>.*?)(?=^      - uses:)", section)
        if match is None:
            raise AssertionError("replay shell step is missing")
        return textwrap.dedent(match.group("body"))

    @staticmethod
    def shell_function(shell, name):
        match = re.search(rf"(?ms)^{re.escape(name)}\(\) \{{\n.*?^\}}$", shell)
        if match is None:
            raise AssertionError(f"replay shell function {name!r} is missing")
        return match.group(0)

    def run_replay_helpers(self, body, runner_temp):
        shell = self.replay_shell(WORKFLOW.read_text(encoding="utf-8"))
        helpers = "\n".join(
            self.shell_function(shell, name)
            for name in ("prepare_replay_paths", "find_replay_state", "verify_replay_outputs")
        )
        return subprocess.run(
            ["bash", "-c", "set -Eeuo pipefail\n" + helpers + "\n" + body],
            check=False,
            text=True,
            capture_output=True,
            env={**os.environ, "RUNNER_TEMP": str(runner_temp)},
        )

    def test_replay_paths_are_fresh_paired_and_isolated(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = self.run_replay_helpers(
                """
                prepare_replay_paths calibration a state_a report_a
                prepare_replay_paths calibration b state_b report_b
                test "$state_a" != "$state_b"
                test "$report_a" != "$report_b"
                test "$(dirname "$state_a")" = "$(dirname "$report_a")"
                test "$(dirname "$state_b")" = "$(dirname "$report_b")"
                test -d "$state_a" && test -d "$report_a"
                test -d "$state_b" && test -d "$report_b"
                test -z "$(find "$state_a" "$report_a" "$state_b" "$report_b" -mindepth 1 -print -quit)"
                """,
                root,
            )
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_replay_paths_reject_reuse_and_symlink_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            duplicate = self.run_replay_helpers(
                "prepare_replay_paths calibration a state report\n"
                "prepare_replay_paths calibration a duplicate_state duplicate_report\n",
                root,
            )
            self.assertNotEqual(duplicate.returncode, 0)

        for occupied in ("state", "report"):
            with self.subTest(occupied=occupied), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                destination = root / "calibration-a-replay"
                (destination / occupied).mkdir(parents=True)
                marker = destination / occupied / "preserve"
                marker.write_text("caller-owned", encoding="utf-8")
                result = self.run_replay_helpers(
                    "prepare_replay_paths calibration a state report\n",
                    root,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(marker.read_text(encoding="utf-8"), "caller-owned")

        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as escape:
            root = Path(directory)
            (root / "calibration-a-replay").symlink_to(escape, target_is_directory=True)
            result = self.run_replay_helpers(
                "prepare_replay_paths calibration a state report\n",
                root,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(list(Path(escape).iterdir()), [])

    def test_replay_outputs_reject_missing_or_empty_state_and_reports(self):
        expected = (
            "replay-state.json",
            "replay-review.json",
            "replay-review.txt",
            "replay-generated-series.patch",
        )
        cases = (("missing-state", "missing-state"), ("empty-state", "state")) + tuple(
            ("missing-" + name, name) for name in expected
        ) + tuple(("empty-" + name, "empty:" + name) for name in expected)
        for label, mutation in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                setup = ["prepare_replay_paths calibration a state report"]
                if mutation != "missing-state":
                    setup.append("printf '{}\\n' > \"$state/replay-state-0001.json\"")
                if mutation == "state":
                    setup[-1] = "touch \"$state/replay-state-0001.json\""
                for name in expected:
                    if mutation == name:
                        continue
                    if mutation == "empty:" + name:
                        setup.append(f'touch "$report/{name}"')
                    else:
                        setup.append(f'printf "evidence\\n" > "$report/{name}"')
                setup.append("verify_replay_outputs \"$state\" \"$report\" final_state")
                result = self.run_replay_helpers("\n".join(setup) + "\n", root)
                self.assertNotEqual(result.returncode, 0)

    def test_replay_invokes_each_reconstruction_once(self):
        shell = self.replay_shell(WORKFLOW.read_text(encoding="utf-8"))
        calls = re.findall(r"(?m)^reconstruct (calibration|roots-29\.3|locked-29\.4) .* ([ab])$", shell)
        self.assertEqual(
            calls,
            [
                ("calibration", "a"),
                ("calibration", "b"),
                ("roots-29.3", "a"),
                ("roots-29.3", "b"),
                ("locked-29.4", "a"),
                ("locked-29.4", "b"),
            ],
        )
        self.assertEqual(len(calls), len(set(calls)))

    def test_replay_exports_from_the_owned_candidate(self):
        shell = self.replay_shell(WORKFLOW.read_text(encoding="utf-8"))
        self.assertIn(
            'export-patches --repository "$state_directory/owned-candidate"',
            shell,
        )
        self.assertNotIn('export-patches --repository "$work"', shell)

    def test_workflow_has_only_trusted_read_only_paths(self):
        text = WORKFLOW.read_text()
        for required in ("schedule:", "workflow_dispatch:", "contents: read", "CORE_29_3_COMMIT", "CORE_29_4_COMMIT", "CORE_29_3_TREE", "CORE_29_4_TREE", "LOCKED_INPUT_DIGEST", "fetch --no-tags https://github.com/bitcoin/bitcoin.git", "timeout-minutes: 10", "timeout-minutes: 20", "timeout-minutes: 60", "cancel-in-progress: true", "needs: [gate, fetch]", "actions/upload-artifact@65462800fd760344b1a7b4382951275a0abb4808"):
            self.assertIn(required, text)
        for forbidden in ("pull_request", "pull_request_target", "contents: write", "id-token: write", "secrets.", "actions/cache", "create tag", "git tag", "publish", "sign"):
            self.assertNotIn(forbidden, text)
        actions = re.findall(r"^\s*-\s+uses:\s*([^\s#]+)", text, re.MULTILINE)
        self.assertTrue(actions and all(re.fullmatch(r"actions/(?:checkout|upload-artifact|download-artifact)@[0-9a-f]{40}", action) for action in actions))
        checkout_steps = re.findall(
            r"(?ms)^      - uses: actions/checkout@[^\n]+\n(.*?)(?=^      - |\Z)",
            text,
        )
        self.assertEqual(len(checkout_steps), text.count("uses: actions/checkout@"))
        self.assertTrue(all("persist-credentials: false" in step for step in checkout_steps))
        for required in ("roots-trusted-candidate-build.py", "core-to-knots-29.3", "roots-29.3", "core-29.4", "timeout-minutes: 180"):
            self.assertIn(required, text)
        for required in ("promotion-inputs:", "operation:", "options: [replay, promote]", "candidate_commit:", "candidate_tree:", "control_sha:", "test \"$CONTROL_SHA\" = \"$EVENT_SHA\"", "refs/heads/integration/roots-29.4", "Validate and build the immutable candidate once"):
            self.assertIn(required, text)
        self.assertIn("test \"$CANDIDATE_COMMIT\" = 990732b942778c7c96bc9f647f290846eec71c12", text)
        self.assertIn("test \"$CANDIDATE_TREE\" = 5e0fe225597174052ed927becf666019b1b9799e", text)
        self.assertNotIn("test \"$CANDIDATE_COMMIT\" = c8dc2e70bc145930855cf615ba9caffaccdcdcb9", text)
        self.assertNotIn("test \"$CANDIDATE_TREE\" = 70cd94d5f0ca21ac61720f33ecb86ba115a3b7a9", text)
        self.assertNotIn("test \"$CANDIDATE_COMMIT\" = dfc74d403585f7c23815ef80d2e206b85c33919a", text)
        self.assertNotIn("test \"$CANDIDATE_TREE\" = 477eb9b3f50098b0b8548a9ff35efaa29fd7ecde", text)
        self.assertFalse((ROOT / ".github/workflows/roots-trusted-promotion.yml").exists())
        build_text = BUILD_SCRIPT.read_text()
        self.assertIn('"cmake", "--build"', build_text)
        self.assertIn('"ctest", "--test-dir"', build_text)
        self.assertIn('"-DCMAKE_C_COMPILER_LAUNCHER=ccache"', build_text)
        self.assertIn('"-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"', build_text)
        self.assertIn('"-DENABLE_WALLET=ON"', build_text)
        self.assertIn('"-DWITH_BDB=OFF"', build_text)
        self.assertIn('"CCACHE_DIR"', build_text)
        for invariant_test in ("feature_block.py", "p2p_segwit.py", "mempool_datacarrier.py"):
            self.assertIn(invariant_test, build_text)

    def test_public_knots_tag_and_historical_roots_parent_are_distinct_locks(self):
        text = WORKFLOW.read_text(encoding="utf-8")
        atlas = json.loads(ATLAS_LOCK.read_text(encoding="utf-8"))["git_inputs"]["knots-29.3.knots20260507"]
        ledger = json.loads(LINEAGE_LEDGER.read_text(encoding="utf-8"))
        knots = next(release for release in ledger["releases"] if release["id"] == "knots-29.3.knots20260507")
        historical_parent = ledger["fork_starts"][0]["parent"].removeprefix("sha1:")

        self.assertEqual(self.workflow_env(text, "KNOTS_29_3_TAG_REF"), knots["tag_ref"])
        self.assertEqual(self.workflow_env(text, "KNOTS_29_3_TAG_OBJECT"), knots["tag_object"].removeprefix("sha1:"))
        self.assertEqual(self.workflow_env(text, "KNOTS_29_3_COMMIT"), atlas["peeled_commit"].removeprefix("sha1:"))
        self.assertEqual(self.workflow_env(text, "KNOTS_29_3_TREE"), atlas["tree"].removeprefix("sha1:"))
        self.assertEqual(self.workflow_env(text, "ROOTS_29_3_PARENT_COMMIT"), historical_parent)
        self.assertEqual(self.workflow_env(text, "ROOTS_29_3_PARENT_TREE"), "56f97d3a9199c1fb191e1b8a21f2caae3901b7f6")
        self.assertEqual(
            self.workflow_env(text, "HEADLESS_BUILD_PACKAGES"),
            "build-essential cmake pkgconf python3 libevent-dev libboost-dev libsqlite3-dev ccache",
        )
        self.assertNotEqual(self.workflow_env(text, "KNOTS_29_3_COMMIT"), historical_parent)
        self.assertIn('"$KNOTS_29_3_TAG_REF:$KNOTS_29_3_TAG_REF"', text)
        self.assertIn('cat-file -t "$KNOTS_29_3_TAG_REF"', text)
        self.assertIn('rev-parse "$KNOTS_29_3_TAG_REF^{commit}"', text)
        self.assertIn('rev-parse "$KNOTS_29_3_TAG_REF^{commit}^{tree}"', text)
        self.assertNotIn('https://github.com/bitcoinknots/bitcoin.git "$ROOTS_29_3_PARENT_COMMIT"', text)
        self.assertIn('reconstruct roots-29.3 "$ROOTS_29_3_PARENT_COMMIT" "$ROOTS_29_3_PARENT_TREE"', text)

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
            with mock.patch.object(BUILD.subprocess, "run", side_effect=responses), mock.patch.object(BUILD, "verify_launchers"):
                with self.assertRaisesRegex(ValueError, "candidate acceptance command failed"):
                    BUILD.build_and_test(repository.resolve(), tree, root / "new-build", 2)

    def test_candidate_cache_environment_is_isolated_from_host(self):
        build = Path("/tmp/roots-candidate-build")
        with mock.patch.dict("os.environ", {"CCACHE_DIR": "/host/cache", "GITHUB_TOKEN": "token", "AWS_SECRET_ACCESS_KEY": "secret"}, clear=False):
            environment = BUILD.candidate_environment(build)
        self.assertEqual(environment["CCACHE_DIR"], "/tmp/roots-candidate-build/.ccache")
        self.assertNotIn("GITHUB_TOKEN", environment)
        self.assertNotIn("AWS_SECRET_ACCESS_KEY", environment)
    def test_decisions_locks_and_failures_are_deterministic(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "trusted-replay-report.json"
            args = ("manual", "false", "none", ROOT / "contrib/roots/adaptation-manifest-29.3.json", ROOT / "contrib/roots/methodology-v1.json", ROOT / "contrib/roots/core-29.4-migration-fixture.json")
            first = GATE.report(*args)
            second = GATE.report(*args)
            self.assertEqual(first, second)
            self.assertEqual(first["decision"], "no-op")
            self.assertEqual(GATE.decision("schedule", "false"), "replay")
            self.assertEqual(GATE.decision("manual", "false", "auto"), "no-op")
            self.assertEqual(GATE.decision("manual", "false", "force"), "replay")
            for later_base in ("core-29.4", "branch-name", "https://example.invalid/core.git", "0" * 40):
                with self.subTest(later_base=later_base), self.assertRaises(GATE.GateError):
                    GATE.report("manual", "true", later_base, *args[3:])
            fixture = json.loads((ROOT / "contrib/roots/core-29.4-migration-fixture.json").read_text())
            fixture["core_inputs"]["v29.3"]["tree"] = "z" * 40
            bad = Path(directory) / "fixture.json"
            bad.write_text(json.dumps(fixture))
            with self.assertRaises(GATE.GateError): GATE.report("manual", "true", "none", *args[3:5], bad)
            fixture["core_inputs"]["v29.3"]["tree"] = "0" * 40
            bad.write_text(json.dumps(fixture))
            with self.assertRaises(GATE.GateError): GATE.report("manual", "true", "none", *args[3:5], bad)
            output.write_bytes(b"occupied")
            self.assertTrue(output.exists())

    def test_artifact_manifest_rejects_extra_large_and_digest_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            expected = {}
            for name in GATE.EXPECTED_ARTIFACTS:
                path = root / name
                path.write_text(name)
                expected[name] = GATE.digest(path)
            GATE.verify_artifacts(root, expected)
            (root / "extra").write_text("x")
            with self.assertRaises(GATE.GateError): GATE.verify_artifacts(root, expected)
            (root / "extra").unlink()
            (root / "replay-review.txt").write_bytes(b"x" * (GATE.MAX_REPORT_BYTES + 1))
            with self.assertRaises(GATE.GateError): GATE.verify_artifacts(root, expected)

    def test_review_uses_frozen_role_expectations_and_rejects_mutations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifacts = root / "artifacts"
            roles = {}
            for role in ("core-to-knots-29.3", "roots-29.3", "core-29.4"):
                digests = {}
                for run in ("a", "b"):
                    target = artifacts / role / run
                    target.mkdir(parents=True)
                    state = {"candidate_tree": "a" * 40, "completed_units": [role]}
                    for kind, name in REVIEW.NAMES.items():
                        path = target / name
                        path.write_text(json.dumps(state, sort_keys=True) if kind == "state" else f"{role}-{kind}")
                        digests[kind] = REVIEW.sha(path)
                    digests["outcome_set"] = "sha256:" + __import__("hashlib").sha256(json.dumps(state["completed_units"], sort_keys=True, separators=(",", ":")).encode()).hexdigest()
                roles[role] = {"target_tree": "sha1:" + "a" * 40, "artifact_digests": digests}
            methodology = root / "methodology.json"
            methodology.write_text(json.dumps({
                "contracts": {"canonical_export": REVIEW.CANONICAL_EXPORT_CONTRACT},
                "reconstructions": roles,
            }))
            REVIEW.verify(methodology, artifacts)
            methodology.write_text(json.dumps({
                "contracts": {"canonical_export": "arbitrary-export"},
                "reconstructions": roles,
            }))
            with self.assertRaises(ValueError): REVIEW.verify(methodology, artifacts)
            methodology.write_text(json.dumps({
                "contracts": {"canonical_export": REVIEW.CANONICAL_EXPORT_CONTRACT},
                "reconstructions": roles,
            }))
            export = artifacts / "roots-29.3" / "a" / REVIEW.NAMES["generated_export"]
            original = export.read_bytes()
            export.write_bytes(original + b"mutated signature/header/date/path/order/binary/rename/config")
            with self.assertRaises(ValueError): REVIEW.verify(methodology, artifacts)
            export.write_bytes(original)
            for role in roles:
                for run in ("a", "b"):
                    export = artifacts / role / run / REVIEW.NAMES["generated_export"]
                    export.write_bytes(b"p" * 70_000)
                    roles[role]["artifact_digests"]["generated_export"] = REVIEW.sha(export)
            methodology.write_text(json.dumps({
                "contracts": {"canonical_export": REVIEW.CANONICAL_EXPORT_CONTRACT},
                "reconstructions": roles,
            }))
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
