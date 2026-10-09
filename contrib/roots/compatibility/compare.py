#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""External paired-node regtest qualification; reports are transient run artifacts."""

import argparse
import base64
import hashlib
import http.client
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import tempfile
import threading
import time

SCHEMA_VERSION = 1
CASES = {
    "smoke": ("ordinary", "datacarrier", "invalid-block"),
    "full": ("ordinary", "datacarrier", "invalid-block", "sigop", "subdust", "lifecycle"),
}
MAX_RPC_BYTES = 4 * 1024 * 1024
MAX_DIAGNOSTIC_BYTES = 4096
MAX_TIMEOUT = 3600


class ComparisonError(Exception):
    """A bounded, public diagnostic without RPC credentials."""


class Cancelled(ComparisonError):
    pass


class RPCError(ComparisonError):
    def __init__(self, label, code):
        self.code = code
        super().__init__(label + " RPC error code " + str(code))


def kill_group(process, signum):
    try:
        os.killpg(process.pid, signum)
    except ProcessLookupError:
        pass


def digest(path):
    hasher = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def command(args, timeout=30):
    """Own the entire command process group, including on cancellation."""
    with tempfile.TemporaryFile() as output:
        process = subprocess.Popen(args, stdout=output, stderr=output, start_new_session=True)
        failure = None
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            failure = "command timed out"
        finally:
            kill_group(process, signal.SIGKILL)
            if process.poll() is None:
                process.wait(timeout=5)
        output.seek(0)
        data = output.read(MAX_RPC_BYTES + 1)
    if failure or process.returncode:
        error = ComparisonError(failure or "command failed (exit %d)" % process.returncode)
        error.diagnostic_tail = data[-MAX_DIAGNOSTIC_BYTES:].decode("utf-8", errors="replace")
        raise error
    if len(data) > MAX_RPC_BYTES:
        raise ComparisonError("command output exceeds limit")
    return data.decode("utf-8", errors="replace").strip()


def source_identity(path, commit):
    if not re.fullmatch(r"[0-9a-fA-F]{40}", commit):
        raise ComparisonError("source identity must be a full 40-hex commit")
    path = Path(path).resolve(strict=True)
    actual = command(["git", "-C", str(path), "rev-parse", "HEAD"])
    if actual != commit.lower():
        raise ComparisonError("source HEAD does not match pinned commit")
    # Include untracked files and distinguish cleanliness from immutable HEAD.
    status = command(["git", "-C", str(path), "status", "--porcelain=v1", "--untracked-files=all"])
    tree = command(["git", "-C", str(path), "rev-parse", actual + "^{tree}"])
    return {"path": str(path), "commit": actual, "tree": tree, "clean": not status, "status": status.splitlines()}


def binary_identity(path):
    path = Path(path).resolve(strict=True)
    if not path.is_file() or not os.access(path, os.X_OK):
        raise ComparisonError("binary is not an executable file")
    sha = digest(path)
    # Even -version parses startup paths in these revisions. Never let identity
    # inspection consult or create the user's default datadir.
    with tempfile.TemporaryDirectory(prefix="roots-compatibility-version-") as datadir:
        version = command([str(path), "-version", "-regtest", "-datadir=" + datadir,
                           "-conf=/dev/null", "-settings=0"], timeout=10)
    if digest(path) != sha:
        raise ComparisonError("binary changed during identity inspection")
    return {"path": str(path), "sha256": sha, "version": version[:MAX_DIAGNOSTIC_BYTES],
            "provenance": {"status": "unverified", "reason": "caller-supplied local binary"}}


def reserve_port():
    # Keep reservations until immediately before launch; startup handles bind races.
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    return sock


class Node:
    def __init__(self, label, binary, datadir, startup_timeout, rpc_timeout, extra_flags=()):
        self.label = label
        self.binary = Path(binary)
        self.datadir = Path(datadir)
        self.startup_timeout = startup_timeout
        self.rpc_timeout = rpc_timeout
        self.extra_flags = list(extra_flags)
        self.process = None
        self.log = None
        self.port = None
        self.flags = []
        self.rpc_id = 0
        self.cookie = None
        self.credentials = set()
        self.cleanup_result = None

    def start(self, deadline=None):
        if self.process is not None and self.process.poll() is None:
            raise ComparisonError("node is already running")
        self.datadir.mkdir(parents=True, exist_ok=True)
        reservation = reserve_port()
        self.port = reservation.getsockname()[1]
        self.flags = ["-regtest", "-server=1", "-daemon=0", "-conf=/dev/null", "-settings=0",
                      "-datadir=" + str(self.datadir), "-rpcbind=127.0.0.1", "-rpcallowip=127.0.0.1",
                      "-rpcport=" + str(self.port), "-listen=0", "-discover=0", "-dnsseed=0",
                      "-fixedseeds=0", "-connect=0", "-networkactive=0", "-disablewallet=1",
                      "-printtoconsole=0", *self.extra_flags]
        self.cookie = None
        self.log = (self.datadir / "process.log").open("ab")
        try:
            reservation.close()
            self.process = subprocess.Popen([str(self.binary), *self.flags], stdout=self.log,
                                            stderr=self.log, start_new_session=True)
            startup_deadline = time.monotonic() + self.startup_timeout
            deadline = startup_deadline if deadline is None else min(deadline, startup_deadline)
            while time.monotonic() < deadline:
                if self.process.poll() is not None:
                    raise ComparisonError(self.label + " exited during startup")
                cookie_path = self.datadir / "regtest" / ".cookie"
                try:
                    self.cookie = cookie_path.read_text().strip()
                    if not re.fullmatch(r"[^:\s]+:[^\s]+", self.cookie):
                        raise ComparisonError(self.label + " malformed RPC cookie")
                    self.credentials.add(self.cookie)
                except FileNotFoundError:
                    time.sleep(min(0.05, max(0, deadline - time.monotonic())))
                    continue
                try:
                    info = self.rpc("getblockchaininfo", deadline=deadline)
                except RPCError as error:
                    if error.code != -28:
                        raise
                    time.sleep(min(0.05, max(0, deadline - time.monotonic())))
                    continue
                except (ConnectionError, TimeoutError, OSError):
                    time.sleep(min(0.05, max(0, deadline - time.monotonic())))
                    continue
                if not isinstance(info, dict) or info.get("chain") != "regtest":
                    raise ComparisonError(self.label + " RPC chain is not regtest")
                return
            raise ComparisonError(self.label + " startup timed out")
        finally:
            reservation.close()

    def rpc(self, method, *params, deadline=None):
        remaining = self.rpc_timeout if deadline is None else min(self.rpc_timeout, deadline - time.monotonic())
        if remaining <= 0:
            raise ComparisonError(self.label + " case deadline exceeded")
        if not self.cookie:
            raise ComparisonError(self.label + " RPC cookie unavailable")
        self.rpc_id += 1
        body = json.dumps({"jsonrpc": "2.0", "id": self.rpc_id, "method": method, "params": list(params)})
        authorization = base64.b64encode(self.cookie.encode()).decode()
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=remaining)
        timer = None
        rpc_deadline = time.monotonic() + remaining
        try:
            connection.connect()
            # Socket inactivity timeouts alone allow a peer to drip bytes forever.
            # Shutdown the owned socket at the absolute RPC deadline as well.
            rpc_socket = connection.sock

            def expire():
                try:
                    rpc_socket.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass

            timer = threading.Timer(max(0, rpc_deadline - time.monotonic()), expire)
            timer.daemon = True
            timer.start()
            connection.request("POST", "/", body, {"Authorization": "Basic " + authorization,
                                                   "Content-Type": "application/json"})
            response = connection.getresponse()
            payload = response.read(MAX_RPC_BYTES + 1)
            if response.status != 200:
                raise ComparisonError(self.label + " RPC HTTP failure (%d)" % response.status)
            if len(payload) > MAX_RPC_BYTES:
                raise ComparisonError(self.label + " RPC response exceeds limit")
            try:
                parsed = json.loads(payload)
            except (ValueError, UnicodeError):
                raise ComparisonError(self.label + " malformed RPC JSON") from None
            if not isinstance(parsed, dict) or parsed.get("id") != self.rpc_id:
                raise ComparisonError(self.label + " malformed RPC envelope")
            if parsed.get("error") is not None:
                error = parsed["error"]
                code = error.get("code") if isinstance(error, dict) else None
                if type(code) is not int:
                    raise ComparisonError(self.label + " malformed RPC error")
                # Server text is untrusted and may include authentication or raw input.
                raise RPCError(self.label, code)
            if "result" not in parsed:
                raise ComparisonError(self.label + " malformed RPC envelope")
            return parsed["result"]
        finally:
            if timer:
                timer.cancel()
                timer.join()
            connection.close()

    def stop(self):
        if self.process is None:
            if self.log:
                self.log.close()
            self.cleanup_result = {"stopped": True, "method": "not-started"}
            return
        method = "already-exited"
        try:
            if self.process.poll() is None:
                method = "rpc"
                try:
                    self.rpc("stop", deadline=time.monotonic() + min(self.rpc_timeout, 2))
                    self.process.wait(timeout=2)
                except (ComparisonError, OSError, http.client.HTTPException, subprocess.TimeoutExpired):
                    method = "terminate"
                    kill_group(self.process, signal.SIGTERM)
                    try:
                        self.process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        method = "kill"
                        kill_group(self.process, signal.SIGKILL)
                        self.process.wait(timeout=2)
            self.cleanup_result = {"stopped": self.process.poll() is not None, "method": method,
                                   "returncode": self.process.returncode}
        finally:
            kill_group(self.process, signal.SIGKILL)
            if self.log:
                self.log.close()
            self.cookie = None

    def diagnostic(self):
        # Preserve bounded complete log lines, never a truncated credential or
        # the cookie file itself. Startup and validation logs are separate.
        parts = []
        for path in (self.datadir / "process.log", self.datadir / "regtest" / "debug.log"):
            if not path.exists():
                continue
            with path.open("rb") as stream:
                offset = max(0, path.stat().st_size - MAX_DIAGNOSTIC_BYTES // 2)
                stream.seek(offset)
                if offset:
                    stream.readline(MAX_DIAGNOSTIC_BYTES // 2)
                parts.append(stream.read(MAX_DIAGNOSTIC_BYTES // 2).decode("utf-8", errors="replace"))
        text = "\n".join(parts)
        for cookie in self.credentials:
            for secret in (cookie, cookie.split(":", 1)[1], base64.b64encode(cookie.encode()).decode()):
                text = text.replace(secret, "[redacted]")
        text = re.sub(r"(?i)(authorization|cookie|rpcpassword|rpcuser)[^\n]*", "[redacted]", text)
        return text


def run_cases(nodes, args, report):
    spec = importlib.util.spec_from_file_location("roots_compatibility_fixtures", Path(__file__).with_name("fixtures.py"))
    fixtures = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixtures)
    fixtures.FixtureRunner(nodes, args, report, ComparisonError).run()


def pinned_build(source, directory, args, record):
    spec = importlib.util.spec_from_file_location("roots_compatibility_build", Path(__file__).with_name("build.py"))
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    return builder.build_source(source, directory, args.build_timeout, args.build_jobs, command, ComparisonError, record)


def write_report(path, report):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, delete=False, encoding="utf-8") as stream:
            temporary = Path(stream.name)
            json.dump(report, stream, indent=2, sort_keys=True, allow_nan=False)
            stream.write("\n")
        os.replace(temporary, path)
    finally:
        if temporary and temporary.exists():
            temporary.unlink()


def positive_timeout(value):
    number = float(value)
    if not math.isfinite(number) or not 0 < number <= MAX_TIMEOUT:
        raise argparse.ArgumentTypeError("timeout must be finite and between 0 and 3600 seconds")
    return number


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    for label in ("core", "roots"):
        # Validate inside compare so absent identity still leaves a failure report.
        result.add_argument("--" + label + "-bitcoind")
        result.add_argument("--" + label + "-source")
        result.add_argument("--" + label + "-commit")
        result.add_argument("--" + label + "-build-config", default="unspecified",
                            help="caller description; does not authenticate a local binary")
    result.add_argument("--profile", choices=CASES, default="smoke")
    result.add_argument("--output", required=True)
    result.add_argument("--qualification", action="store_true", help="require verified build provenance")
    result.add_argument("--build-from-source", action="store_true",
                        help="build fresh binaries from pinned Git exports during this run")
    result.add_argument("--build-timeout", type=positive_timeout, default=1800,
                        help="total seconds per source export/configure/build")
    result.add_argument("--build-jobs", type=int, choices=range(1, 5), default=2)
    result.add_argument("--startup-timeout", type=positive_timeout, default=30)
    result.add_argument("--rpc-timeout", type=positive_timeout, default=10)
    result.add_argument("--case-timeout", type=positive_timeout, default=120)
    result.add_argument("--work-dir", help="parent for owned temporary regtest directories")
    return result


def compare(args):
    started = time.monotonic()
    report = {"schema_version": SCHEMA_VERSION, "profile": args.profile, "status": "failed",
              "harness_sha256": digest(__file__),
              "qualified": False, "required_cases": list(CASES[args.profile]),
              "cases": [{"id": case, "status": "not-run"} for case in CASES[args.profile]], "nodes": {},
              "timeouts": {key: getattr(args, key + "_timeout") for key in ("startup", "rpc", "case", "build")}}
    nodes = []
    temporary = None
    exit_code = 1
    try:
        for label in ("core", "roots"):
            for field in ("source", "commit"):
                if not getattr(args, label + "_" + field):
                    raise ComparisonError(label + " input " + field + " is required")
            record = report["nodes"][label] = {}
            record["source"] = source_identity(getattr(args, label + "_source"), getattr(args, label + "_commit"))
            record["build_configuration"] = getattr(args, label + "_build_config")
        temporary = tempfile.TemporaryDirectory(prefix="roots-compatibility-", dir=args.work_dir)
        fixture_args = argparse.Namespace(**vars(args))
        fixture_args.roots_commit = report["nodes"]["roots"]["source"]["commit"]
        for label in ("core", "roots"):
            record = report["nodes"][label]
            path = getattr(args, label + "_bitcoind")
            if args.build_from_source:
                if path:
                    raise ComparisonError("in-run builds cannot use caller binary paths")
                path, evidence = pinned_build(record["source"], Path(temporary.name) / (label + "-build"), args, record)
                record["binary"] = binary_identity(path)
                record["binary"]["provenance"] = {"status": "built-in-run", "source_commit": record["source"]["commit"],
                                                  "source_tree": record["source"]["tree"],
                                                  "archive_sha256": evidence["archive_sha256"]}
                record["build_configuration"] = evidence["configuration"]
                evidence["binary_sha256"] = record["binary"]["sha256"]
                if label == "roots":
                    fixture_args.roots_source = evidence["snapshot_path"]
            else:
                if not path:
                    raise ComparisonError(label + " input bitcoind is required")
                record["binary"] = binary_identity(path)
        core, roots = (report["nodes"][label]["binary"] for label in ("core", "roots"))
        if args.qualification or args.build_from_source:
            if core["path"] == roots["path"] or core["sha256"] == roots["sha256"]:
                raise ComparisonError("qualification requires distinct binary paths and digests")
            if not args.build_from_source:
                raise ComparisonError("qualification requires verified pinned-source build provenance")
        for label in ("core", "roots"):
            record = report["nodes"][label]
            node = Node(label, record["binary"]["path"], Path(temporary.name) / label,
                        args.startup_timeout, args.rpc_timeout, extra_flags=("-acceptnonstdtxn=0", "-persistmempool=1"))
            nodes.append(node)  # Own cleanup before startup can fail.
            node.start()
            record["launch_flags"] = node.flags
            record["launch_history"] = [list(node.flags)]
        run_cases(nodes, fixture_args, report)
        actual = [case["id"] for case in report["cases"]]
        if actual != list(CASES[args.profile]) or any(case["status"] != "passed" for case in report["cases"]):
            raise ComparisonError("required case accounting failed")
        report["status"] = "passed"
        report["qualified"] = args.build_from_source
        exit_code = 0
    except (Cancelled, KeyboardInterrupt):
        report["error"] = "cancelled"
        exit_code = 130
    except (ComparisonError, OSError, ValueError, http.client.HTTPException) as error:
        # OS messages may contain arbitrary filenames. Keep reports deterministic and bounded.
        report["error"] = str(error) if isinstance(error, ComparisonError) else type(error).__name__
    except Exception as error:
        report["error"] = "internal harness failure: " + type(error).__name__
    finally:
        # Cancellation during ordinary shutdown must not strand the second node
        # or interrupt publication of a partial report.
        cleanup_cancelled = [False]

        def defer_cancel(_signum, _frame):
            cleanup_cancelled[0] = True

        cleanup_handlers = {signum: signal.signal(signum, defer_cancel)
                            for signum in (signal.SIGTERM, signal.SIGINT)}
        try:
            for node in reversed(nodes):
                record = report["nodes"][node.label]
                record["launch_flags"] = node.flags
                try:
                    node.stop()
                    record["cleanup"] = node.cleanup_result
                    if exit_code:
                        record["diagnostic"] = node.diagnostic()
                except (OSError, subprocess.TimeoutExpired) as error:
                    record["cleanup"] = {"stopped": False, "error": type(error).__name__}
                    report["status"] = "failed"
                    report["error"] = "owned process cleanup failed"
                    exit_code = 1
            for record in report["nodes"].values():
                if "binary" in record:
                    try:
                        unchanged = digest(record["binary"]["path"]) == record["binary"]["sha256"]
                    except OSError:
                        unchanged = False
                    if not unchanged:
                        report["status"] = "failed"
                        report["error"] = "binary changed during comparison"
                        exit_code = 1
            if temporary:
                temporary.cleanup()
            if cleanup_cancelled[0] and exit_code != 1:
                report["status"] = "failed"
                report["error"] = "cancelled"
                exit_code = 130
            if exit_code:
                report["qualified"] = False
            report["duration_seconds"] = round(time.monotonic() - started, 6)
            write_report(args.output, report)
        finally:
            for signum, handler in cleanup_handlers.items():
                signal.signal(signum, handler)
    return exit_code


def main(argv=None):
    args = parser().parse_args(argv)
    previous = {}

    def cancel(_signum, _frame):
        # A second signal must not interrupt bounded cleanup and report publication.
        for signum in previous:
            signal.signal(signum, signal.SIG_IGN)
        raise Cancelled()

    try:
        for signum in (signal.SIGTERM, signal.SIGINT):
            previous[signum] = signal.signal(signum, cancel)
        return compare(args)
    finally:
        for signum, handler in previous.items():
            signal.signal(signum, handler)


if __name__ == "__main__":
    raise SystemExit(main())
