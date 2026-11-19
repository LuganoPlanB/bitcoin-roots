# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Foreground test-owned daemon only: explicit binary, private scratch and cookie."""

import base64
import ctypes
import os
import signal
import sys
import http.client
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time
from urllib.parse import quote
from support.runtime import binary_identity, validated_scratch, write_json
from rpc.fake_http import strict_json, RpcError


class OwnedRegtest:
    def __init__(
        self,
        binary,
        output,
        *,
        port=None,
        public_descriptors=False,
        monitor_fixture=False,
    ):
        self.output = Path(output)
        self.output.mkdir(parents=True, exist_ok=True)
        self.identity = binary_identity(binary)
        self.scratch = tempfile.TemporaryDirectory(
            prefix="roots-gui-regtest-", dir=validated_scratch()
        )
        self.data = Path(self.scratch.name)
        self.data.chmod(0o700)
        if port is None:
            with socket.socket() as allocator:
                allocator.bind(("127.0.0.1", 0))
                port = allocator.getsockname()[1]
        self.port = port
        self.auth = None
        self.secrets = []
        self.sequence = 0
        self.transcript = []
        self.closed = False
        self.argv = [
            self.identity["path"],
            "-regtest",
            "-server=1",
            "-daemon=0",
            "-listen=0",
            "-discover=0",
            "-dnsseed=0",
            "-connect=0",
            "-listenonion=0",
            "-natpmp=0",
            "-prune=0",
            "-walletbroadcast=0",
            "-persistmempool=0",
            "-printtoconsole=1",
            "-rpcbind=127.0.0.1",
            "-rpcallowip=127.0.0.1",
            f"-rpcport={port}",
            f"-datadir={self.data}",
        ]
        # Transient opt-in for this disposable fixture only. GUI traffic remains
        # behind the separate read-only proxy; no installed service is changed.
        if public_descriptors:
            self.argv.append("-allow-dangerous-rpc=1")
        if monitor_fixture:
            self.argv.extend(
                (
                    "-txindex=1",
                    "-txospenderindex=1",
                    "-mempoolexpiry=1",
                    "-debug=mempool",
                )
            )
        self.raw = self.data / "console.log"
        self.log = self.raw.open("wb")
        parent_pid = os.getpid()

        def parent_death_guard():
            signal.signal(signal.SIGTERM, signal.SIG_DFL)
            if ctypes.CDLL(None).prctl(1, signal.SIGTERM, 0, 0, 0) != 0:
                os._exit(125)
            if os.getppid() != parent_pid:
                os.kill(os.getpid(), signal.SIGTERM)

        try:
            self.process = subprocess.Popen(
                self.argv,
                stdout=self.log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
                preexec_fn=parent_death_guard if sys.platform == "linux" else None,
            )
        except BaseException:
            self.log.close()
            self.scratch.cleanup()
            raise
        self.record = dict(
            binary=self.identity,
            argv=self.argv,
            pid=self.process.pid,
            network="regtest",
            datadir=str(self.data),
            port=port,
            credentials="ephemeral owned cookie; never retained",
            parentDeathSignal="SIGTERM" if sys.platform == "linux" else "unavailable",
            settled=False,
        )

    def cookie(self):
        raw = (self.data / "regtest/.cookie").read_bytes().strip()
        self.auth = "Basic " + base64.b64encode(raw).decode()
        self.secrets = [raw.decode(), self.auth, base64.b64encode(raw).decode()]

    def ready(self, timeout=30, *, hold=False):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError("owned daemon exited during startup")
            try:
                self.cookie()
                if not hold:
                    info = self.call("getblockchaininfo")
                    if info["chain"] != "regtest":
                        raise RuntimeError("unexpected fixture chain")
                    self.record["backend"] = self.call("getnetworkinfo")["subversion"]
                    return info
            except (OSError, RpcError, ValueError, http.client.HTTPException):
                pass
            time.sleep(0.02)
        raise TimeoutError("owned daemon readiness deadline")

    def call(self, method, params=None, *, wallet=None, timeout=10):
        if self.process.poll() is not None:
            raise RuntimeError("owned daemon is not live")
        self.sequence += 1
        identifier = self.sequence
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=timeout)
        path = "/wallet/" + quote(wallet, safe="") if wallet is not None else "/"
        try:
            conn.request(
                "POST",
                path,
                json.dumps(
                    dict(
                        jsonrpc="2.0",
                        id=identifier,
                        method=method,
                        params={} if params is None else params,
                    )
                ),
                {"Authorization": self.auth, "Content-Type": "application/json"},
            )
            response = conn.getresponse()
            raw = response.read()
            status = response.status
            entry = dict(
                id=identifier, method=method, wallet=wallet is not None, status=status
            )
            if method == "walletprocesspsbt":
                entry["policy"] = {k: params.get(k) for k in ["sign", "finalize"]}
            self.transcript.append(entry)
            result = strict_json(raw)
            if result.get("id") != identifier:
                raise ValueError("RPC response identity mismatch")
            if result.get("error"):
                raise RpcError(result["error"]["code"])
            if status != 200 or "result" not in result:
                raise ValueError("RPC result unavailable")
            return result["result"]
        finally:
            conn.close()

    def close(self):
        if self.closed:
            return self.record
        self.closed = True
        try:
            if self.process.poll() is None:
                try:
                    if not self.auth:
                        self.cookie()
                    self.call("stop", timeout=2)
                except (
                    OSError,
                    ValueError,
                    RpcError,
                    RuntimeError,
                    http.client.HTTPException,
                ):
                    pass
                try:
                    self.process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    self.process.terminate()
                    try:
                        self.process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        self.process.kill()
                        self.process.wait(timeout=3)
        finally:
            self.log.close()
            for name, source in [
                ("console.log", self.raw),
                ("debug.log", self.data / "regtest/debug.log"),
            ]:
                if source.is_file():
                    text = source.read_text(errors="replace")
                    for secret in self.secrets:
                        text = text.replace(secret, "[REDACTED]")
                    (self.output / name).write_text(text)
            self.record["authMaterialAbsentFromRetainedLogs"] = all(
                all(secret not in p.read_text() for secret in self.secrets)
                for p in self.output.glob("*.log")
            )
            self.record.update(
                exit=self.process.returncode,
                settled=self.process.poll() is not None,
                transcript=self.transcript,
            )
            self.scratch.cleanup()
            self.record["datadirRemoved"] = not self.data.exists()
            write_json(self.output / "process.json", self.record)
        if not self.record["settled"]:
            raise RuntimeError("owned daemon did not settle")
        return self.record

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
