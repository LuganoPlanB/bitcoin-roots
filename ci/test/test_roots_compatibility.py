#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import importlib.util
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "contrib/roots/compatibility/compare.py"
spec = importlib.util.spec_from_file_location("roots_compatibility", SCRIPT)
comparison = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)
fixture_spec = importlib.util.spec_from_file_location("roots_compatibility_fixtures", SCRIPT.with_name("fixtures.py"))
fixtures = importlib.util.module_from_spec(fixture_spec)
fixture_spec.loader.exec_module(fixtures)
build_spec = importlib.util.spec_from_file_location("roots_compatibility_build", SCRIPT.with_name("build.py"))
builder = importlib.util.module_from_spec(build_spec)
build_spec.loader.exec_module(builder)

# This fake owns an actual loopback HTTP server and a process, so failure tests
# exercise the production startup, cookie RPC, timeout and cleanup paths.
FAKE_NODE = r'''#!/usr/bin/env python3
import base64
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import os
from pathlib import Path
import signal
import sys
import time

MODE = {mode!r}
TRACK = {track!r}
if "-version" in sys.argv:
    if not any(arg.startswith("-datadir=") for arg in sys.argv) or "-conf=/dev/null" not in sys.argv:
        raise SystemExit(9)
    print("Fake regtest node " + MODE)
    raise SystemExit(0)
args = dict(arg[1:].split("=", 1) for arg in sys.argv[1:] if "=" in arg)
datadir = Path(args["datadir"])
with open(TRACK, "a", encoding="utf8") as stream:
    stream.write(str(os.getpid()) + "\n")
if MODE == "exit":
    raise SystemExit(7)
if MODE == "no-cookie":
    while True:
        time.sleep(1)
if MODE == "ignore-stop":
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
cookie = "__cookie__:SECRET-ONLY-IN-COOKIE"
(datadir / "regtest").mkdir(exist_ok=True)
(datadir / "regtest" / ".cookie").write_text(cookie)
stopping = False
requests = 0

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        global stopping, requests
        requests += 1
        expected = "Basic " + base64.b64encode(cookie.encode()).decode()
        if self.headers.get("Authorization") != expected:
            self.send_response(401)
            self.end_headers()
            return
        data = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        if MODE == "slow-rpc":
            time.sleep(10)
        if data["method"] == "stop":
            if MODE != "ignore-stop":
                stopping = True
            result = "stopping"
        else:
            result = {{"chain": "main" if MODE == "wrong-chain" else "regtest"}}
        body = {{"id": data["id"], "result": result, "error": None}}
        if MODE == "warmup" and requests == 1:
            body = {{"id": data["id"], "error": {{"code": -28, "message": "warming up"}}}}
        if MODE == "bad-envelope":
            body["id"] = 999
        payload = b"not json" if MODE == "malformed" else json.dumps(body).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

server = HTTPServer(("127.0.0.1", int(args["rpcport"])), Handler)
while not stopping:
    server.handle_request()
server.server_close()
'''


class CompatibilityInfrastructureTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="roots-compatibility-tests-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.repo = self.directory / "source"
        self.repo.mkdir()
        self.git("init", "-q")
        (self.repo / "README.md").write_text("Fake source for infrastructure tests only.\n")
        self.git("add", "README.md")
        self.git("-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "--allow-empty", "-qm", "test")
        self.commit = self.git("rev-parse", "HEAD").strip()
        self.track = self.directory / "pids"
        self.output = self.directory / "report.json"
        self.core = self.fake("core", "normal")
        self.roots = self.fake("roots", "normal")

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.repo), *args], text=True, encoding="utf8", stderr=subprocess.DEVNULL)

    def fake(self, label, mode):
        path = self.directory / label
        path.write_text(FAKE_NODE.format(mode=mode, track=str(self.track)) + "\n# " + label + "\n")
        path.chmod(0o700)
        return path

    def arguments(self, extra=()):
        return ["--core-bitcoind", str(self.core), "--roots-bitcoind", str(self.roots),
                "--core-source", str(self.repo), "--roots-source", str(self.repo),
                "--core-commit", self.commit, "--roots-commit", self.commit,
                "--startup-timeout", "0.3", "--rpc-timeout", "0.15", "--case-timeout", "0.4",
                "--work-dir", str(self.directory), "--output", str(self.output), *extra]

    def assert_cleaned(self):
        if self.track.exists():
            for pid in self.track.read_text().splitlines():
                with self.assertRaises(ProcessLookupError):
                    os.kill(int(pid), 0)
        self.assertEqual(list(self.directory.glob("roots-compatibility-*")), [])
        self.assertNotIn("SECRET-ONLY-IN-COOKIE", self.output.read_text())
        self.assertNotIn("Authorization", self.output.read_text())

    def run_comparison(self, extra=(), cases=None):
        args = comparison.parser().parse_args(self.arguments(extra))
        if cases is None:
            code = comparison.compare(args)
        else:
            with patch.object(comparison, "run_cases", cases):
                code = comparison.compare(args)
        report = json.loads(self.output.read_text())
        self.assert_cleaned()
        return code, report

    @staticmethod
    def successful_cases(nodes, args, report):
        for node in nodes:
            assert node.rpc("getblockchaininfo")["chain"] == "regtest"
        report["cases"] = [{"id": case, "status": "passed"} for case in comparison.CASES[args.profile]]

    def test_ready_nodes_are_not_qualification_without_serialization_framework(self):
        code, report = self.run_comparison()
        self.assertEqual(code, 1)
        self.assertFalse(report["qualified"])
        self.assertEqual(report["error"], "ValueError")
        self.assertEqual([case["id"] for case in report["cases"]], list(comparison.CASES["smoke"]))
        self.assertTrue(all(case["status"] == "not-run" for case in report["cases"]))

    def test_success_cleanup_and_honest_binary_source_identity(self):
        code, report = self.run_comparison(cases=self.successful_cases)
        self.assertEqual(code, 0)
        self.assertEqual(report["status"], "passed")
        self.assertFalse(report["qualified"])
        for label in ("core", "roots"):
            record = report["nodes"][label]
            self.assertEqual(record["binary"]["provenance"]["status"], "unverified")
            self.assertEqual(record["source"]["commit"], self.commit)
            self.assertTrue(record["source"]["clean"])
            self.assertTrue(record["cleanup"]["stopped"])
            self.assertIn("-listen=0", record["launch_flags"])
            self.assertIn("-conf=/dev/null", record["launch_flags"])
            self.assertIn("-rpcbind=127.0.0.1", record["launch_flags"])
        self.assertNotEqual(report["nodes"]["core"]["launch_flags"], report["nodes"]["roots"]["launch_flags"])

    def test_dirty_source_is_reported_without_calling_head_dirty(self):
        (self.repo / "tooling.txt").write_text("uncommitted tooling\n")
        _, report = self.run_comparison(cases=self.successful_cases)
        source = report["nodes"]["core"]["source"]
        self.assertFalse(source["clean"])
        self.assertEqual(source["status"], ["?? tooling.txt"])
        self.assertEqual(source["commit"], self.commit)

    def test_wrong_chain_cleans_partial_pair(self):
        self.roots = self.fake("roots", "wrong-chain")
        code, report = self.run_comparison()
        self.assertEqual(code, 1)
        self.assertEqual(report["error"], "roots RPC chain is not regtest")
        self.assertEqual(len(report["nodes"]), 2)
        self.assertTrue(all(node["cleanup"]["stopped"] for node in report["nodes"].values()))

    def test_startup_timeout_without_cookie(self):
        self.core = self.fake("core", "no-cookie")
        code, report = self.run_comparison()
        self.assertEqual(code, 1)
        self.assertEqual(report["error"], "core startup timed out")
        self.assertEqual(report["nodes"]["core"]["cleanup"]["method"], "terminate")

    def test_hanging_rpc_is_bounded_and_owned_node_terminated(self):
        self.core = self.fake("core", "slow-rpc")
        began = time.monotonic()
        code, report = self.run_comparison()
        self.assertEqual(code, 1)
        self.assertLess(time.monotonic() - began, 4)
        self.assertEqual(report["error"], "core startup timed out")

    def test_startup_crash(self):
        self.core = self.fake("core", "exit")
        code, report = self.run_comparison()
        self.assertEqual(code, 1)
        self.assertEqual(report["error"], "core exited during startup")

    def test_warmup_is_retried(self):
        self.core = self.fake("core", "warmup")
        code, _ = self.run_comparison(cases=self.successful_cases)
        self.assertEqual(code, 0)

    def test_malformed_rpc(self):
        for mode, expected in (("malformed", "JSON"), ("bad-envelope", "envelope")):
            with self.subTest(mode=mode):
                self.core = self.fake("core", mode)
                code, report = self.run_comparison()
                self.assertEqual(code, 1)
                self.assertEqual(report["error"], "core malformed RPC " + expected)

    def test_missing_and_nonexecutable_binary(self):
        self.core = self.directory / "absent"
        code, report = self.run_comparison()
        self.assertEqual(code, 1)
        self.assertEqual(report["error"], "FileNotFoundError")
        self.core.write_text("not executable")
        code, report = self.run_comparison()
        self.assertEqual(code, 1)
        self.assertEqual(report["error"], "binary is not an executable file")

    def test_bad_or_missing_source_identity(self):
        for value, expected in (("HEAD", "source identity must be a full 40-hex commit"),
                                ("0" * 40, "source HEAD does not match pinned commit")):
            with self.subTest(value=value):
                code, report = self.run_comparison(["--core-commit", value])
                self.assertEqual(code, 1)
                self.assertEqual(report["error"], expected)
        self.assertEqual(comparison.main(self.arguments(["--core-source", str(self.directory / "absent")])), 1)
        self.assert_cleaned()

    def test_absent_identity_has_failure_report(self):
        args = comparison.parser().parse_args(["--output", str(self.output)])
        self.assertEqual(comparison.compare(args), 1)
        report = json.loads(self.output.read_text())
        self.assertEqual(report["error"], "core input source is required")
        self.assertTrue(all(case["status"] == "not-run" for case in report["cases"]))
        self.assert_cleaned()

    def test_qualification_rejects_same_path_and_same_bytes(self):
        for binary in (self.core, self.directory / "copied-core"):
            if binary != self.core:
                binary.write_bytes(self.core.read_bytes())
                binary.chmod(0o700)
            self.roots = binary
            code, report = self.run_comparison(["--qualification"])
            self.assertEqual(code, 1)
            self.assertEqual(report["error"], "qualification requires distinct binary paths and digests")
        self.assertFalse(self.track.exists())

    def test_caller_build_config_cannot_authenticate_binary(self):
        code, report = self.run_comparison(["--qualification", "--core-build-config", "trusted=true"])
        self.assertEqual(code, 1)
        self.assertEqual(report["error"], "qualification requires verified pinned-source build provenance")
        self.assertFalse(self.track.exists())

    def simulated_build_comparison(self):
        args = comparison.parser().parse_args(self.arguments(["--build-from-source", "--qualification"]))
        args.core_bitcoind = args.roots_bitcoind = None
        original_command = comparison.command

        def simulated_cmake(argv, timeout=30):
            if argv[0] != "cmake":
                return original_command(argv, timeout)
            if "-B" in argv:
                snapshot = Path(argv[argv.index("-S") + 1])
                self.assertEqual((snapshot / "README.md").read_text(), "Fake source for infrastructure tests only.\n")
                self.assertFalse((snapshot / "untracked-input.txt").exists())
                build = Path(argv[argv.index("-B") + 1])
                self.assertFalse(build.exists())
                build.mkdir()
                (build / "CMakeCache.txt").write_text("CMAKE_CXX_COMPILER:FILEPATH=" + sys.executable
                                                     + "\nCMAKE_C_COMPILER:FILEPATH=" + sys.executable + "\n")
            else:
                build = Path(argv[argv.index("--build") + 1])
                (build / "bin").mkdir()
                label = "core" if build.parent.name == "core-build" else "roots"
                binary = build / "bin/bitcoind"
                binary.write_bytes(getattr(self, label).read_bytes())
                binary.chmod(0o700)
            return "simulated build (unit test only)"

        with patch.object(comparison, "command", simulated_cmake), patch.object(comparison, "run_cases", self.successful_cases):
            code = comparison.compare(args)
        self.assert_cleaned()
        return code, json.loads(self.output.read_text())

    def test_in_run_builds_record_source_tree_and_fresh_commands(self):
        (self.repo / "README.md").write_text("This uncommitted product edit must not enter the build.\n")
        (self.repo / "untracked-input.txt").write_text("Not an immutable input.\n")
        code, report = self.simulated_build_comparison()
        self.assertEqual(code, 0, report.get("error"))
        self.assertTrue(report["qualified"])
        for node in report["nodes"].values():
            self.assertFalse(node["source"]["clean"])
            self.assertEqual(node["binary"]["provenance"]["status"], "built-in-run")
            self.assertEqual(node["build"]["source_tree"], self.git("rev-parse", "HEAD^{tree}").strip())
            self.assertEqual(node["build"]["binary_sha256"], node["binary"]["sha256"])
            self.assertEqual(node["build"]["commands"][0]["argv"][3], "archive")
        self.assert_cleaned()

    def test_forced_shutdown_cannot_qualify_in_run_builds(self):
        self.core = self.fake("core", "ignore-stop")
        code, report = self.simulated_build_comparison()
        self.assertEqual(code, 1)
        self.assertEqual(report["status"], "failed")
        self.assertFalse(report["qualified"])
        self.assertEqual(report["error"], "owned process cleanup failed")
        self.assertEqual(report["nodes"]["core"]["cleanup"],
                         {"stopped": True, "method": "kill", "returncode": -signal.SIGKILL})
        self.assertEqual(report["nodes"]["roots"]["cleanup"]["returncode"], 0)

    def test_nonzero_cleanup_preserves_original_failure_and_cancellation(self):
        for failure, expected_code in ((comparison.ComparisonError("original fixture failure"), 1),
                                       (comparison.Cancelled(), 130)):
            def failed_cases(nodes, _args, _report):
                nodes[0].process.kill()
                nodes[0].process.wait(timeout=5)
                raise failure
            with self.subTest(failure=type(failure).__name__):
                code, report = self.run_comparison(cases=failed_cases)
                self.assertEqual(code, expected_code)
                self.assertEqual(report["error"], "cancelled" if expected_code == 130 else str(failure))
                self.assertFalse(report["qualified"])
                self.assertEqual(report["nodes"]["core"]["cleanup"]["returncode"], -signal.SIGKILL)

    def test_build_failure_and_cancellation_leave_report_and_no_artifacts(self):
        args = comparison.parser().parse_args(self.arguments(["--build-from-source", "--qualification"]))
        args.core_bitcoind = args.roots_bitcoind = None
        for failure, exit_code in ((comparison.ComparisonError("compiler unavailable"), 1), (comparison.Cancelled(), 130)):
            with self.subTest(exit_code=exit_code), patch.object(comparison, "pinned_build", side_effect=failure):
                self.assertEqual(comparison.compare(args), exit_code)
            report = json.loads(self.output.read_text())
            self.assertFalse(report["qualified"])
            self.assertEqual(report["status"], "failed")
            self.assert_cleaned()

    def test_in_run_build_rejects_caller_binary_shortcut(self):
        code, report = self.run_comparison(["--build-from-source"])
        self.assertEqual(code, 1)
        self.assertEqual(report["error"], "in-run builds cannot use caller binary paths")

    def test_mid_case_node_crash_is_detected_and_pair_cleaned(self):
        def crash(nodes, _args, _report):
            nodes[0].process.kill()
            nodes[0].process.wait(timeout=5)
            nodes[0].rpc("getblockchaininfo")
        code, report = self.run_comparison(cases=crash)
        self.assertEqual(code, 1)
        self.assertEqual(report["status"], "failed")

    def test_binary_changed_during_run_cannot_pass(self):
        def changed(nodes, args, report):
            self.successful_cases(nodes, args, report)
            self.core.write_text(self.core.read_text() + "\n# modified after execution\n")
        code, report = self.run_comparison(cases=changed)
        self.assertEqual(code, 1)
        self.assertEqual(report["error"], "binary changed during comparison")

    def test_diagnostics_redact_cookie_and_password_in_complete_lines(self):
        datadir = self.directory / "diagnostic-node"
        datadir.mkdir()
        node = comparison.Node("diagnostic", self.core, datadir, 1, 1)
        node.credentials.add("__cookie__:SECRET-ONLY-IN-COOKIE")
        (datadir / "process.log").write_text("password alone: SECRET-ONLY-IN-COOKIE\nAuthorization: Basic hidden\n")
        self.assertNotIn("SECRET-ONLY-IN-COOKIE", node.diagnostic())
        self.assertNotIn("Authorization", node.diagnostic())

    def test_missing_case_fails_closed(self):
        def incomplete(_nodes, _args, report):
            report["cases"] = [{"id": "ordinary", "status": "passed"}]
        code, report = self.run_comparison(cases=incomplete)
        self.assertEqual(code, 1)
        self.assertEqual(report["error"], "required case accounting failed")

    def test_duplicate_or_skipped_case_fails_closed(self):
        def duplicate(_nodes, _args, report):
            report["cases"] = [{"id": "ordinary", "status": "passed"}] * 3
        code, report = self.run_comparison(cases=duplicate)
        self.assertEqual(code, 1)
        self.assertEqual(report["error"], "required case accounting failed")

        def skipped(_nodes, args, report):
            self.successful_cases(_nodes, args, report)
            report["cases"][1]["status"] = "skipped"
        code, report = self.run_comparison(cases=skipped)
        self.assertEqual(code, 1)
        self.assertEqual(report["error"], "required case accounting failed")

    def test_case_deadline_failure_cleans_both_nodes(self):
        def expired(nodes, _args, _report):
            nodes[0].rpc("getblockchaininfo", deadline=time.monotonic() - 1)
        code, report = self.run_comparison(cases=expired)
        self.assertEqual(code, 1)
        self.assertEqual(report["error"], "core case deadline exceeded")

    def test_uncooperative_node_is_killed_without_touching_unrelated_process(self):
        unrelated = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        try:
            self.core = self.fake("core", "ignore-stop")
            code, report = self.run_comparison(cases=self.successful_cases)
            self.assertEqual(code, 1)
            self.assertEqual(report["nodes"]["core"]["cleanup"]["method"], "kill")
            self.assertIsNone(unrelated.poll())
        finally:
            unrelated.terminate()
            unrelated.wait(timeout=5)

    def test_sigterm_produces_partial_report_and_cleanup(self):
        self.core = self.fake("core", "no-cookie")
        process = subprocess.Popen([sys.executable, str(SCRIPT), *self.arguments(["--startup-timeout", "10"])])
        try:
            deadline = time.monotonic() + 5
            while not self.track.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertTrue(self.track.exists())
            process.send_signal(signal.SIGTERM)
            self.assertEqual(process.wait(timeout=10), 130)
            report = json.loads(self.output.read_text())
            self.assertEqual(report["error"], "cancelled")
            self.assert_cleaned()
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)

    def test_cancellation_during_cleanup_finishes_owned_cleanup(self):
        original = comparison.Node.stop
        sent = [False]

        def interrupted_stop(node):
            if not sent[0]:
                sent[0] = True
                os.kill(os.getpid(), signal.SIGTERM)
            original(node)

        with patch.object(comparison.Node, "stop", interrupted_stop):
            code, report = self.run_comparison(cases=self.successful_cases)
        self.assertEqual(code, 130)
        self.assertEqual(report["status"], "failed")
        self.assertEqual(report["error"], "cancelled")
        self.assertTrue(all(node["cleanup"]["stopped"] for node in report["nodes"].values()))

    def test_timeout_input_bounds(self):
        for value in ("nan", "inf", "0", "-1", "3601"):
            with self.subTest(value=value), self.assertRaises(Exception):
                comparison.positive_timeout(value)


class FixtureComparatorTest(unittest.TestCase):
    def test_acceptance_divergence_is_failure(self):
        with self.assertRaisesRegex(comparison.ComparisonError, "acceptance diverged"):
            fixtures.assert_block_outcomes({"core": None, "roots": "rejected"}, True, comparison.ComparisonError)
        with self.assertRaisesRegex(comparison.ComparisonError, "acceptance diverged"):
            fixtures.assert_block_outcomes({"core": None, "roots": None}, False, comparison.ComparisonError)
        fixtures.assert_block_outcomes({"core": "invalid core", "roots": "invalid roots"}, False, comparison.ComparisonError)

    def test_chain_and_utxo_divergence_is_failure(self):
        core = {"height": 10, "bestblockhash": "a", "utxos": {"out": {"value_sats": 330}}}
        for roots in ({**core, "height": 11}, {**core, "bestblockhash": "b"},
                      {**core, "utxos": {"out": {"value_sats": 329}}}):
            with self.subTest(roots=roots), self.assertRaisesRegex(comparison.ComparisonError, "state diverged"):
                fixtures.compare_snapshots({"core": core, "roots": roots}, comparison.ComparisonError)
        self.assertEqual(fixtures.compare_snapshots({"core": core, "roots": core}, comparison.ComparisonError), core)

    def test_utxo_normalization_preserves_monetary_precision(self):
        raw = {"bestblock": "a", "confirmations": 2, "value": 0.00000330,
               "scriptPubKey": {"hex": "51", "desc": "incidental descriptor"}, "coinbase": False}
        self.assertEqual(fixtures.normalized_utxo(raw), {"bestblock": "a", "confirmations": 2,
                         "value_sats": 330, "script_hex": "51", "coinbase": False})
        self.assertIsNone(fixtures.normalized_utxo(None))


class SourceArchiveTest(unittest.TestCase):
    def test_archive_rejects_traversal_and_external_symlink(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for index, (name, target) in enumerate((("../escape", None), ("link", "../../escape"))):
                archive = directory / (str(index) + ".tar")
                with tarfile.open(archive, "w") as stream:
                    member = tarfile.TarInfo(name)
                    if target:
                        member.type = tarfile.SYMTYPE
                        member.linkname = target
                        stream.addfile(member)
                    else:
                        member.size = 1
                        stream.addfile(member, io.BytesIO(b"x"))
                with self.assertRaisesRegex(comparison.ComparisonError, "escapes snapshot"):
                    builder.extract_source(archive, directory / ("snapshot" + str(index)), comparison.ComparisonError)


if __name__ == "__main__":
    unittest.main()
