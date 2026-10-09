# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.

"""CI gates test invalid evidence, not product consensus equivalence."""

import copy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("compatibility_ci", ROOT / "ci/compatibility.py")
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)


class CompatibilityCITest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.roots = self.directory / "roots"
        self.roots.mkdir()
        subprocess.run(["git", "init", "-q", self.roots], check=True)
        # Detached maintenance can outlive a commit and race temporary cleanup.
        self.git("config", "maintenance.auto", "false")
        self.git("config", "user.name", "Compatibility test")
        self.git("config", "user.email", "compatibility@example.invalid")
        (self.roots / "core").write_text("core")
        (self.roots / "CMakeLists.txt").write_text("set(CLIENT_VERSION_MAJOR 30)\nset(CLIENT_VERSION_MINOR 3)\n")
        self.git("add", ".")
        self.git("commit", "-qm", "core")
        self.core_sha = self.git("rev-parse", "HEAD")
        self.git("tag", "v30.3")
        self.core = self.directory / "core"
        subprocess.run(["git", "clone", "-q", "-c", "maintenance.auto=false", str(self.roots), str(self.core)], check=True)
        subprocess.run(["git", "-C", str(self.core), "remote", "set-url", "origin", ci.OFFICIAL_CORE + ".git"], check=True)
        for name in ("compare.py", "fixtures.py", "build.py"):
            destination = self.roots / "contrib/roots/compatibility" / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / "contrib/roots/compatibility" / name, destination)
        framework = self.roots / "test/functional/test_framework/messages.py"
        framework.parent.mkdir(parents=True)
        framework.write_text("# test serialization identity\n")
        self.git("add", ".")
        self.git("commit", "-qm", "roots")
        self.candidate = self.git("rev-parse", "HEAD")
        self.git("update-ref", "refs/remotes/origin/roots/30.3", self.candidate)

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.roots), *args], text=True, encoding="utf8").strip()

    def test_temporary_repositories_do_not_spawn_automatic_maintenance(self):
        trace = self.directory / "git-trace.json"
        fixture = CompatibilityCITest("test_committed_metadata_cannot_select_wrong_or_ambiguous_generation")
        result = unittest.TestResult()
        with patch.dict(os.environ, {"GIT_TRACE2_EVENT": str(trace)}):
            fixture.run(result)
            ci.git(self.core, "fetch", str(self.roots), "HEAD")
        self.assertTrue(result.wasSuccessful(), result.errors + result.failures)
        events = [json.loads(line) for line in trace.read_text().splitlines()]
        children = [event["argv"] for event in events if event["event"] == "child_start"]
        self.assertTrue(any(event["event"] == "start" and "commit" in event.get("argv", []) for event in events))
        self.assertFalse([argv for argv in children if "maintenance" in argv or "gc" in argv])

    def resolve(self, **changes):
        args = dict(roots_source=self.roots, core_source=self.core, candidate=self.candidate,
                    core_sha=self.core_sha, context_ref="topic/30.3/policy")
        return ci.resolve_sources(**{**args, **changes})

    def report(self, profile="smoke"):
        spec = importlib.util.spec_from_file_location("builder_test", ROOT / "contrib/roots/compatibility/build.py")
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        nodes = {}
        for label, source, commit, digest in (("core", self.core, self.core_sha, "a" * 64),
                                               ("roots", self.roots, self.candidate, "b" * 64)):
            tree = ci.git(source, "rev-parse", "HEAD^{tree}")
            provenance = dict(status="built-in-run", source_commit=commit, source_tree=tree, archive_sha256="c" * 64)
            build = {**provenance, "configuration": list(builder.CMAKE_OPTIONS), "binary_sha256": digest,
                     "cmake_cache_sha256": "d" * 64, "builder_sha256": ci.file_digest(self.roots / "contrib/roots/compatibility/build.py"),
                     "commands": [{"status": "passed", "argv": argv} for argv in
                                  (["git", "archive", commit], ["cmake", "configure"], ["cmake", "--build"],
                                   ["cc", "--version"], ["c++", "--version"])]}
            nodes[label] = {"source": {"commit": commit, "tree": tree}, "build": build,
                            "binary": {"sha256": digest, "provenance": provenance},
                            "build_configuration": list(builder.CMAKE_OPTIONS),
                            "cleanup": {"stopped": True, "returncode": 0}}
        cases = []
        outpoint = "e" * 64 + ":0"
        def state(number, height):
            tip = str(number) * 64
            return {"bestblockhash": tip, "height": height,
                    "utxos": {outpoint: {"bestblock": tip, "value_sats": 1, "coinbase": False,
                                         "confirmations": 1, "script_hex": "51"}}}
        snapshots = {"ordinary": [state(1, 111)], "datacarrier": [state(2, 112)],
                     "invalid-block": [state(2, 112)], "sigop": [state(3, 113), state(4, 114)],
                     "subdust": [state(5, 115)], "lifecycle": []}
        for name in ci.CASES[profile]:
            valid = name != "invalid-block"
            case = {"id": name, "status": "passed", "states": snapshots[name],
                    "admission": [{"name": title, "txid": "f" * 64, "expected_allowed": {"core": core, "roots": roots},
                                   "observed": {"core": {"allowed": core}, "roots": {"allowed": roots}}}
                                  for title, core, roots in ci.ADMISSIONS[name]],
                    "blocks": [{"hash": snapshot["bestblockhash"] if valid else "9" * 64,
                                "expected_valid": valid, "outcomes": {"core": None if valid else "invalid", "roots": None if valid else "invalid"}}
                               for snapshot in snapshots[name]]}
            if name in ("datacarrier", "subdust"):
                case["selected_block"] = case["blocks"][-1]["hash"]
            if name == "lifecycle":
                # The real harness leaves top-level states/blocks empty here;
                # observations belong to invalidate/reconsider and restart.
                transitions = []
                for fixture, parent in (("datacarrier", "ordinary"), ("subdust", "sigop")):
                    transitions.append({"fixture": fixture, "block": snapshots[fixture][-1]["bestblockhash"],
                                        "steps": [{"action": action, "state": copy.deepcopy(snapshot),
                                                   "mempools": {"core": [], "roots": []}}
                                                  for action, snapshot in (("invalidate", snapshots[parent][-1]),
                                                                           ("reconsider", snapshots["subdust"][-1]))]})
                case.update(transitions=transitions,
                            control_profile={"name": "compatible-p2tr-control-persistence", "persistmempool": True, "txid": "f" * 64},
                            restart={"control_persisted": True, "same_owned_datadirs": True, "state": copy.deepcopy(snapshots["subdust"][-1]),
                                     "mempools_before": {"core": ["f" * 64], "roots": ["f" * 64]},
                                     "mempools_after": {"core": ["f" * 64], "roots": ["f" * 64]}})
            cases.append(case)
        return {"schema_version": 1, "status": "passed", "qualified": True, "profile": profile,
                "required_cases": ci.CASES[profile], "cases": cases, "nodes": nodes,
                "harness_sha256": ci.file_digest(self.roots / "contrib/roots/compatibility/compare.py"),
                "fixture_revision": {"sha256": ci.file_digest(self.roots / "contrib/roots/compatibility/fixtures.py")},
                "serialization": {"source_commit": self.candidate, "files": [{"path": "test/functional/test_framework/messages.py",
                                  "sha256": ci.file_digest(self.roots / "test/functional/test_framework/messages.py")}]}}

    def validate(self, report, profile="smoke"):
        ci.validate_report(report, self.roots, self.core, self.candidate, self.core_sha, profile)

    def test_exact_sources_resolve(self):
        self.assertEqual(self.resolve()["generation"], "30.3")

    def prepare(self, context="main"):
        original = ci.git
        def fetch_official(source, *args):
            if args[0] == "fetch":
                self.assertEqual(args[1:3], ("--no-tags", ci.OFFICIAL_CORE + ".git"))
                ref = args[-1].split(":")[1]
                return original(source, "update-ref", ref, self.core_sha)
            return original(source, *args)
        with patch.object(ci, "git", side_effect=fetch_official):
            return ci.prepare_inputs(self.roots, self.candidate, context)

    def test_inputs_resolve_convention_and_unique_main_ancestry(self):
        for context in ("main", "roots/30.3", "topic/30.3/policy", "promote/30.3-fix"):
            with self.subTest(context=context):
                self.assertEqual(self.prepare(context), {"candidate_sha": self.candidate, "core_sha": self.core_sha,
                                                         "context_ref": "roots/30.3"})

    def test_inputs_reject_unknown_ambiguous_or_missing_generation(self):
        for context in ("roots/31.0", "unknown", "roots/30.3;bad"):
            with self.subTest(context=context), self.assertRaises(ValueError):
                self.prepare(context)
        self.git("update-ref", "refs/remotes/origin/roots/30.2", self.candidate)
        # Historical canonical branches from a different generation are not
        # silently substituted for the reviewed candidate's matching Core base.
        self.assertEqual(self.prepare()["core_sha"], self.core_sha)
        self.git("update-ref", "-d", "refs/remotes/origin/roots/30.2")
        self.git("update-ref", "-d", "refs/remotes/origin/roots/30.3")
        with self.assertRaises(ValueError):
            self.prepare()

    def test_committed_metadata_cannot_select_wrong_or_ambiguous_generation(self):
        for content in ("set(CLIENT_VERSION_MAJOR 31)\nset(CLIENT_VERSION_MINOR 0)\n",
                        "set(CLIENT_VERSION_MAJOR 30)\nset(CLIENT_VERSION_MAJOR 31)\nset(CLIENT_VERSION_MINOR 3)\n"):
            (self.roots / "CMakeLists.txt").write_text(content)
            self.git("commit", "-qam", "generation metadata fixture")
            self.candidate = self.git("rev-parse", "HEAD")
            with self.assertRaises(ValueError):
                self.prepare()
            with self.assertRaises(ValueError):
                self.prepare("roots/30.3")

    def test_invalid_sha_and_checkout(self):
        for value in ("main", "a" * 39, "0" * 40, "A" * 40, "a\n" * 20):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.resolve(candidate=value)

    def test_unknown_or_wrong_generation(self):
        for ref in ("main", "roots/31.0", "topic/30.3;bad", "roots/30.3/../other"):
            with self.subTest(ref=ref), self.assertRaises(ValueError):
                self.resolve(context_ref=ref)

    def test_official_remote_required(self):
        subprocess.run(["git", "-C", str(self.core), "remote", "set-url", "origin", "https://example.invalid/core"], check=True)
        with self.assertRaises(ValueError):
            self.resolve()

    def test_official_tag_mismatch(self):
        subprocess.run(["git", "-C", str(self.core), "tag", "-d", "v30.3"], check=True, capture_output=True)
        with self.assertRaises(ValueError):
            self.resolve()

    def test_missing_canonical_and_wrong_ancestry(self):
        self.git("update-ref", "-d", "refs/remotes/origin/roots/30.3")
        with self.assertRaises(ValueError):
            self.resolve()
        self.git("update-ref", "refs/remotes/origin/roots/30.3", self.candidate)
        self.git("checkout", "-q", "--orphan", "unrelated")
        self.git("commit", "-qm", "unrelated")
        with self.assertRaises(ValueError):
            self.resolve(candidate=self.git("rev-parse", "HEAD"))

    def test_smoke_and_full_reports(self):
        for profile in ci.CASES:
            self.validate(self.report(profile), profile)

    def test_missing_skipped_failed_and_duplicate_cases(self):
        good = self.report()
        mutations = [lambda r: r["cases"].pop(), lambda r: r["cases"].append(copy.deepcopy(r["cases"][0])),
                     lambda r: r["cases"][0].update(status="skipped"), lambda r: r["cases"][0].update(status="failed"),
                     lambda r: r.update(profile="full"), lambda r: r.update(qualified=False),
                     lambda r: r["cases"][0].update(blocks=[]), lambda r: r["cases"][0]["blocks"][0]["outcomes"].update(roots="invalid")]
        for index, mutate in enumerate(mutations):
            report = copy.deepcopy(good)
            mutate(report)
            with self.subTest(index=index), self.assertRaises(ValueError):
                self.validate(report)

    def test_stale_and_unverified_provenance(self):
        good = self.report()
        mutations = [lambda n: n["source"].update(commit="0" * 40), lambda n: n["build"].update(source_tree="0" * 40),
                     lambda n: n["binary"]["provenance"].update(status="unverified"),
                     lambda n: n["build"].update(configuration=["different"]),
                     lambda n: n["build"]["commands"][0].update(status="failed"),
                     lambda n: n["cleanup"].update(stopped=False), lambda n: n["binary"].update(sha256="0" * 64)]
        for index, mutate in enumerate(mutations):
            report = copy.deepcopy(good)
            mutate(report["nodes"]["roots"])
            with self.subTest(index=index), self.assertRaises(ValueError):
                self.validate(report)

    def test_full_restart_cannot_be_skipped(self):
        report = self.report("full")
        report["cases"][-1]["restart"]["control_persisted"] = False
        with self.assertRaises(ValueError):
            self.validate(report, "full")

    def test_real_lifecycle_shape_requires_nested_transition_and_restart_evidence(self):
        good = self.report("full")
        self.assertEqual(good["cases"][-1]["states"], [])
        self.assertEqual(good["cases"][-1]["blocks"], [])
        self.validate(good, "full")
        mutations = [lambda life: life["transitions"][0]["steps"].pop(),
                     lambda life: life["transitions"][0]["steps"][0].update(action="reconsider"),
                     lambda life: life["transitions"][0]["steps"][0]["state"].update(height=115),
                     lambda life: life["transitions"][0]["steps"][0]["state"]["utxos"]["e" * 64 + ":0"].update(value_sats=2),
                     lambda life: life["transitions"][1].update(block="0" * 64),
                     lambda life: life["restart"].update(state={}),
                     lambda life: life["restart"]["mempools_after"].update(roots=[]),
                     lambda life: life["control_profile"].update(txid="0" * 64)]
        for index, mutate in enumerate(mutations):
            report = copy.deepcopy(good)
            mutate(report["cases"][-1])
            with self.subTest(index=index), self.assertRaises(ValueError):
                self.validate(report, "full")

    def test_block_fixtures_still_require_nonempty_state_and_utxo_evidence(self):
        for field in ("states", "utxos"):
            report = self.report("full")
            if field == "states":
                report["cases"][0]["states"] = []
            else:
                report["cases"][0]["states"][0]["utxos"] = {}
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.validate(report, "full")

    def test_wrong_fixture_and_serialization_revisions(self):
        good = self.report()
        mutations = [lambda r: r.update(harness_sha256="0" * 64),
                     lambda r: r["fixture_revision"].update(sha256="0" * 64),
                     lambda r: r["serialization"].update(source_commit=self.core_sha),
                     lambda r: r["serialization"]["files"][0].update(path="../../outside"),
                     lambda r: r["serialization"]["files"][0].update(sha256="0" * 64),
                     lambda r: r["cases"][1]["admission"][1]["expected_allowed"].update(roots=True)]
        for index, mutate in enumerate(mutations):
            report = copy.deepcopy(good)
            mutate(report)
            with self.subTest(index=index), self.assertRaises(ValueError):
                self.validate(report)

    def test_report_symlinks_are_not_uploaded(self):
        secret = self.directory / "secret.json"
        secret.write_text('{"secret":"outside"}')
        report = self.directory / "report-link.json"
        report.symlink_to(secret)
        output = self.directory / "artifacts-link"
        ci.prepare_artifacts(report, output)
        self.assertFalse((output / "report.json").exists())
        self.assertTrue((output / "status.json").is_file())

    def test_release_source_gate_is_reused(self):
        # A release name without an annotated canonical-tip tag cannot qualify.
        with self.assertRaises(ValueError):
            self.resolve(release_tag="v30.3-roots.1")
        self.git("tag", "-a", "v30.3-roots.1", "-m", "release fixture")
        self.assertEqual(self.resolve(release_tag="v30.3-roots.1")["candidate_sha"], self.candidate)

    def test_core_rc_names_remain_explicitly_unsupported_by_final_release_fixtures(self):
        with self.assertRaisesRegex(ValueError, "release-candidate fixture expectations are not supported"):
            self.resolve(release_tag="v30.3rc1-roots.1")
        with self.assertRaisesRegex(ValueError, "release-candidate fixture expectations are not supported"):
            ci.generation("roots/30.3rc1")

    def test_malformed_missing_and_oversize_reports(self):
        path = self.directory / "report.json"
        with self.assertRaises(OSError):
            ci.load_report(path)
        for content in ('{"status":"passed","status":"failed"}', '{"value":NaN}', 'not json', ' ' * (ci.MAX_REPORT_BYTES + 1)):
            path.write_text(content)
            with self.assertRaises(ValueError):
                ci.load_report(path)

    def test_artifact_names_are_immutable_and_bounded(self):
        name = ci.artifact_name(self.candidate, self.core_sha, "smoke")
        self.assertIn(self.candidate, name)
        self.assertIn(self.core_sha, name)
        self.assertLess(len(name), 150)
        with self.assertRaises(ValueError):
            ci.artifact_name("../../escape", self.core_sha, "smoke")

    def test_failure_artifacts_and_sanitization(self):
        path = self.directory / "report.json"
        output = self.directory / "artifacts"
        ci.prepare_artifacts(path, output)
        self.assertEqual(json.loads((output / "status.json").read_text())["report"], "missing-or-invalid")
        path.write_text(json.dumps({"status": "failed", "diagnostic_tail": "safe error\nAuthorization: secret\nrpcpassword=secret"}))
        output = self.directory / "sanitized"
        ci.prepare_artifacts(path, output)
        result = (output / "report.json").read_text()
        self.assertIn("safe error", result)
        self.assertNotIn("secret", result)

    def test_artifacts_preserve_utxo_outpoints_without_preserving_credentials(self):
        report = self.directory / "outpoints.json"
        outpoint = "a" * 64 + ":0"
        report.write_text(json.dumps({"state": {"utxos": {outpoint: {"value_sats": 42}}},
                                      "Authorization": "Basic secret"}), encoding="utf8")
        output = self.directory / "outpoint-artifacts"
        ci.prepare_artifacts(report, output)
        sanitized = json.loads((output / "report.json").read_text())
        self.assertEqual(sanitized["state"]["utxos"][outpoint], {"value_sats": 42})
        self.assertNotIn("Authorization", sanitized)

    def test_cli_missing_report_is_failure(self):
        result = subprocess.run(["python3", str(ROOT / "ci/compatibility.py"), "report", "--candidate-sha", self.candidate,
                                 "--core-sha", self.core_sha, "--report", str(self.directory / "missing")], capture_output=True)
        self.assertEqual(result.returncode, 1)

    def test_workflow_readonly_pins_builds_and_always_uploads(self):
        workflow = (ROOT / ".github/workflows/reusable-compatibility.yml").read_text()
        self.assertIn("contents: read", workflow)
        self.assertNotIn("secrets", workflow)
        self.assertNotIn("pull_request_target", workflow)
        self.assertEqual(workflow.count("persist-credentials: false"), 2)
        self.assertIn("repository: bitcoin/bitcoin", workflow)
        self.assertIn("--build-from-source --qualification", workflow)
        self.assertIn("compatibility.py report", workflow)
        self.assertEqual(workflow.count("if: always()"), 2)
        self.assertNotIn("actions/cache", workflow)


if __name__ == "__main__":
    unittest.main()
