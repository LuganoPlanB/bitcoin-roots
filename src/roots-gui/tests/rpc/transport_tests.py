# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Exercise the production C++ transport using exclusively owned loopback sockets."""

import sys

sys.dont_write_bytecode = True
import argparse
import base64
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import secrets
import select
import socket
import ssl
import subprocess
import tempfile
import threading
import time
import unittest
from urllib.parse import quote

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)
from fake_http import FakeHttp, Step


class Tunnel:
    """One bounded test-owned SOCKS/CONNECT tunnel; never resolves a real host."""

    def __init__(self, port, kind="socks", failure=False):
        self.target_port = port
        self.kind = kind
        self.failure = failure
        self.host = None
        self.leaked_auth = False
        self.errors = []
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.listener.settimeout(3)
        self.port = self.listener.getsockname()[1]
        self.thread = threading.Thread(target=self.run)

    @staticmethod
    def read(sock, size):
        out = b""
        while len(out) < size:
            data = sock.recv(size - len(out))
            if not data:
                raise OSError("closed fixture tunnel")
            out += data
        return out

    def run(self):
        try:
            with self.listener.accept()[0] as client:
                client.settimeout(3)
                if self.kind == "socks":
                    version, count = self.read(client, 2)
                    assert version == 5 and self.read(client, count) == b"\0"
                    client.sendall(b"\x05\x00")
                    assert self.read(client, 4) == b"\x05\x01\x00\x03"
                    self.host = self.read(client, self.read(client, 1)[0]).decode(
                        "ascii"
                    )
                    self.read(client, 2)
                    client.sendall(
                        bytes([5, 4 if self.failure else 0, 0, 1, 127, 0, 0, 1, 0, 0])
                    )
                    if self.failure:
                        return
                else:
                    header = b""
                    while not header.endswith(b"\r\n\r\n") and len(header) < 8192:
                        header += self.read(client, 1)
                    self.host = header.split(b" ")[1].decode("ascii")
                    self.leaked_auth = b"authorization:" in header.lower()
                    client.sendall(b"HTTP/1.1 200 Connection established\r\n\r\n")
                with socket.create_connection(
                    ("127.0.0.1", self.target_port), timeout=3
                ) as server:
                    while True:
                        readable, _, _ = select.select([client, server], [], [], 3)
                        if not readable:
                            return
                        for source in readable:
                            data = source.recv(8192)
                            if not data:
                                return
                            (server if source is client else client).sendall(data)
        except (OSError, AssertionError):
            self.errors.append("owned tunnel failure")

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.thread.join(4)
        self.listener.close()
        if self.thread.is_alive():
            raise RuntimeError("tunnel did not settle")


class TransportContracts(unittest.TestCase):
    calls = []
    transcripts = []
    settlements = []

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="rpc-", dir=validated_scratch())
        self.addCleanup(self.tmp.cleanup)
        self.cookie = Path(self.tmp.name) / "explicit-cookie"

    @contextmanager
    def fixture(self, steps, tls=False):
        server = FakeHttp(steps)
        if tls:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(ARGS.cert, ARGS.key)
            server.server.socket = context.wrap_socket(
                server.server.socket, server_side=True
            )
        self.cookie.write_text("fixture:" + server.secret)
        self.cookie.chmod(0o600)
        try:
            with server:
                yield server
        finally:
            self.transcripts.extend(server.records)
            self.settlements.append(
                dict(active=server.active, alive=server.thread.is_alive())
            )
            self.assertFalse(server.check_failures)
            self.assertEqual(server.active, 0)
            self.assertFalse(server.thread.is_alive())

    def call(
        self,
        server,
        *,
        host="127.0.0.1",
        tls="plain",
        mode="read",
        wallet="node",
        timeout=800,
        proxy=None,
        credential=None,
        origin=None,
        keylog=None,
    ):
        command = [
            str(ARGS.client),
            host,
            str(server.port),
            str(credential or self.cookie),
            str(tls),
            "read",
            wallet,
            str(timeout),
            mode,
            proxy.kind if proxy else "direct",
            str(proxy.port if proxy else 0),
        ]
        environment = {
            **os.environ,
            "http_proxy": "http://127.0.0.1:1",
            "ALL_PROXY": "socks5h://127.0.0.1:1",
            "NO_PROXY": "*",
            "ROOTS_RPC_TEST_ORIGIN": f"{'http' if tls == 'plain' else 'https'}://{host}:{server.port}",
            "ROOTS_RPC_TEST_PASSWORD": server.secret,
        }
        environment.pop("SSLKEYLOGFILE", None)
        if origin:
            environment["ROOTS_RPC_TEST_ORIGIN"] = origin
        if keylog:
            environment["SSLKEYLOGFILE"] = str(keylog)
        start = time.monotonic()
        completed = subprocess.run(
            command, env=environment, text=True, capture_output=True, timeout=4
        )
        elapsed = time.monotonic() - start
        self.assertEqual(completed.returncode, 0, "C++ client must settle normally")
        self.assertEqual(completed.stderr, "")
        for sensitive in [
            server.secret,
            server.auth,
            "response-secret",
            "cookie-secret",
            "Authorization",
            str(self.cookie),
        ]:
            self.assertNotIn(sensitive, completed.stdout + completed.stderr)
        result = json.loads(completed.stdout)
        self.calls.append(
            dict(
                test=self.id(),
                argv=command,
                cwd=os.getcwd(),
                observedAt=datetime.now(timezone.utc).isoformat(),
                exit=completed.returncode,
                elapsedSeconds=elapsed,
                result=result,
            )
        )
        return result

    def test_exact_wallet_route_and_numeric_lexemes(self):
        wallet = "漢字 / wallet?#%"

        def check(path, body, headers):
            assert path == "/wallet/" + quote(wallet, safe="")
            assert body["params"][1] == 9007199254740993
            assert str(body["params"][0]) == "1E-8"
            assert headers["Content-Type"] == "application/json"

        raw = b'{"jsonrpc":"2.0","id":1,"result":{"amount":90071992.54740993}}'
        with self.fixture([Step("read", raw=raw, request_check=check)]) as server:
            result = self.call(server, wallet=wallet)
            self.assertEqual(
                (result["error"], result["lexeme"], result["sats"]),
                ("success", "90071992.54740993", 9007199254740993),
            )
        for wallet in [".", "..", "folder/../wallet"]:

            def dot_check(path, body, headers, expected=wallet):
                assert path == "/wallet/" + quote(expected, safe="").replace(".", "%2E")

            with self.fixture([Step("read", True, request_check=dot_check)]) as server:
                self.assertEqual(self.call(server, wallet=wallet)["error"], "success")

    def test_cookie_rotation_is_next_call_without_retry(self):
        with self.fixture([Step("read", True), Step("read", True)]) as server:
            self.assertEqual(self.call(server)["error"], "success")
            server.secret = secrets.token_urlsafe(24)
            server.auth = (
                "Basic "
                + base64.b64encode(("fixture:" + server.secret).encode()).decode()
            )
            self.assertEqual(
                self.call(server, mode="mutation")["error"], "authentication"
            )
            staged = self.cookie.with_suffix(".new")
            staged.write_text("fixture:" + server.secret + "\n")
            staged.replace(self.cookie)
            self.assertEqual(self.call(server)["error"], "success")
            self.assertEqual(len(server.records), 3)

    def test_secure_store_reference_and_origin(self):
        with self.fixture([Step("read", True)]) as server:
            self.assertEqual(
                self.call(
                    server, credential="fixture-item", origin="http://unrelated.invalid"
                )["error"],
                "credentials-unavailable",
            )
            self.assertEqual(server.records, [])
            self.assertEqual(
                self.call(server, credential="fixture-item")["error"], "success"
            )
        self.cookie.write_text("missing-colon")
        with FakeHttp([]) as server:
            self.assertEqual(self.call(server)["error"], "credentials-unavailable")
            self.assertEqual(server.records, [])

    def test_redirect_never_contacts_second_endpoint(self):
        with self.fixture([]) as target:
            for code in [301, 302, 303, 307, 308]:
                with self.fixture(
                    [
                        Step(
                            "read",
                            status=code,
                            headers=(
                                ("Location", f"http://127.0.0.1:{target.port}/capture"),
                            ),
                        )
                    ]
                ) as server:
                    result = self.call(server, mode="mutation")
                    self.assertEqual(result["error"], "redirect")
                    self.assertTrue(result["uncertain"])
                    self.assertEqual(len(server.records), 1)
            self.assertEqual(target.records, [])

    def test_malformed_identity_and_shape(self):
        cases = [
            b"{",
            b'{"id":1,"id":2}',
            b'{"a":NaN}',
            b"\xff",
            b'{"jsonrpc":"2.0","id":1,"result":0} trailing',
            b'{"jsonrpc":"2.0","id":1,"error":{"code":-1,"message":"response-secret"},"result":0}',
        ]
        for raw in cases:
            with (
                self.subTest(case=hashlib.sha256(raw).hexdigest()[:8]),
                self.fixture([Step("read", raw=raw)]) as server,
            ):
                self.assertEqual(self.call(server)["error"], "json")
        with self.fixture([Step("read", wrong_id=True)]) as server:
            self.assertEqual(self.call(server)["error"], "response-identity")

    def test_body_header_depth_node_and_request_limits(self):
        for step, mode in [
            (Step("read", raw=b"x" * 1025), "read"),
            (Step("read", headers=(("X-Padding", "x" * 4096),)), "read"),
            (Step("read", result=[[[[[1]]]]]), "depth"),
            (Step("read", result=list(range(20))), "nodes"),
        ]:
            with self.fixture([step]) as server:
                self.assertEqual(
                    self.call(server, mode=mode)["error"],
                    "limit" if mode == "read" else "json",
                )
        with self.fixture([]) as server:
            self.assertEqual(
                self.call(server, mode="request-limit")["error"], "invalid-request"
            )
            self.assertEqual(server.records, [])

    def test_truncated_response_and_disconnect_never_retry(self):
        for step in [
            Step("read", raw=b"{", declared_length=100),
            Step("read", disconnect=True),
        ]:
            with self.fixture([step]) as server:
                result = self.call(server, mode="mutation")
                self.assertEqual(result["error"], "connection")
                self.assertTrue(result["uncertain"])
                self.assertTrue(step.entered.is_set())
                self.assertLessEqual(len(server.records), 1)

    def test_timeout_and_cancel_settle_held_mutation(self):
        for mode, timeout, expected in [
            ("mutation", 60, "timeout"),
            ("cancel", 800, "cancelled"),
            ("read", 60, "timeout"),
        ]:
            step = Step("read", True, gate=threading.Event())
            with self.fixture([step]) as server:
                result = self.call(server, mode=mode, timeout=timeout)
                self.assertTrue(
                    step.entered.is_set(), "real request held before cancel/timeout"
                )
                self.assertEqual(result["error"], expected)
                self.assertEqual(result["uncertain"], mode != "read")
        with self.fixture([]) as server:
            self.assertEqual(self.call(server, mode="pre-cancel")["error"], "cancelled")
            self.assertEqual(server.records, [])

    def test_error_diagnostics_redact_response(self):
        with self.fixture(
            [
                Step(
                    "read",
                    raw=b'{"jsonrpc":"2.0","id":1,"error":{"code":-32601,"message":"response-secret"}}',
                )
            ]
        ) as server:
            result = self.call(server, mode="mutation")
            self.assertEqual(
                (result["error"], result["code"], result["uncertain"]),
                ("rpc", -32601, True),
            )
        with self.fixture([Step("read", status=503, raw=b"cookie-secret")]) as server:
            self.assertEqual(self.call(server)["error"], "http")
        with self.fixture([Step("read", status=417)]) as server:
            self.assertEqual(self.call(server, mode="mutation")["error"], "http")
            self.assertEqual(len(server.records), 1)
        with self.fixture([]) as server:
            keylog = Path(self.tmp.name) / "disabled-tls-keylog"
            self.assertEqual(
                self.call(server, keylog=keylog)["error"], "transport-unavailable"
            )
            self.assertFalse(keylog.exists())
            self.assertEqual(server.records, [])

    def test_tls_trust_and_host_verification(self):
        with self.fixture([Step("read", True)], tls=True) as server:
            self.assertEqual(self.call(server, tls="tls")["error"], "tls")
            self.assertEqual(server.records, [])
            self.assertEqual(
                self.call(server, tls=ARGS.cert, host="localhost")["error"], "tls"
            )
            self.assertEqual(server.records, [])
            self.assertEqual(self.call(server, tls=ARGS.cert)["error"], "success")

    def test_proxy_remote_dns_tunnel_and_failure(self):
        with self.fixture([]) as server:
            self.assertEqual(
                self.call(server, host="fixture-wallet.OnIoN")["error"],
                "invalid-profile",
            )
            self.assertEqual(server.records, [])
        for kind in ["socks", "http"]:
            with (
                self.fixture([Step("read", True)]) as server,
                Tunnel(server.port, kind) as proxy,
            ):
                self.assertEqual(
                    self.call(server, host="fixture-wallet.onion", proxy=proxy)[
                        "error"
                    ],
                    "success",
                )
                self.assertEqual(
                    proxy.host,
                    "fixture-wallet.onion"
                    if kind == "socks"
                    else f"fixture-wallet.onion:{server.port}",
                )
                self.assertFalse(proxy.leaked_auth)
            self.assertEqual(proxy.errors, [])
        with self.fixture([]) as server, Tunnel(server.port, failure=True) as proxy:
            self.assertEqual(
                self.call(server, host="nonexistent-fixture.invalid", proxy=proxy)[
                    "error"
                ],
                "proxy",
            )
            self.assertEqual(server.records, [])
        self.assertEqual(proxy.errors, [])
        with self.fixture([]) as server:

            class UnavailableProxy:
                kind = "socks"

            with socket.socket() as reservation:
                reservation.bind(("127.0.0.1", 0))
                proxy = UnavailableProxy()
                proxy.port = reservation.getsockname()[1]
                # Bound but not listening: deterministic loopback refusal.
                self.assertEqual(self.call(server, proxy=proxy)["error"], "proxy")
            self.assertEqual(server.records, [])

    def test_invalid_endpoint_never_resolves_credentials(self):
        with self.fixture([]) as server:
            for host in [
                "user@127.0.0.1",
                "127.0.0.1/path",
                "127.0.0.1?secret",
                "127.0.0.1#secret",
            ]:
                self.assertEqual(
                    self.call(server, host=host)["error"], "invalid-profile"
                )
            self.assertEqual(server.records, [])


def main():
    global ARGS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    ARGS = parser.parse_args()
    out = evidence_directory(ARGS.output)
    identity = binary_identity(ARGS.client)
    with tempfile.TemporaryDirectory(prefix="tls-", dir=validated_scratch()) as tmp:
        ARGS.cert = Path(tmp) / "fixture-cert.pem"
        ARGS.key = Path(tmp) / "fixture-key.pem"
        setup = subprocess.run(
            [
                "openssl",
                "req",
                "-x509",
                "-newkey",
                "rsa:2048",
                "-nodes",
                "-keyout",
                str(ARGS.key),
                "-out",
                str(ARGS.cert),
                "-days",
                "1",
                "-subj",
                "/CN=fixture-only",
                "-addext",
                "subjectAltName=IP:127.0.0.1",
            ],
            capture_output=True,
            timeout=10,
        )
        if setup.returncode:
            raise RuntimeError("owned TLS fixture generation failed")
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(TransportContracts)
        selected = suite.countTestCases()
        with (out / "cases.log").open("w") as log:
            result = unittest.TextTestRunner(stream=log, verbosity=2).run(suite)
    failed = sorted(
        {test.id().split(" (")[0] for test, _ in result.failures + result.errors}
    )
    report = dict(
        selected=selected,
        executed=result.testsRun,
        passed=result.testsRun - len(failed),
        failed=len(failed),
        skipped=len(result.skipped),
        failedCaseIDs=failed,
        binary=identity,
        observedAt=datetime.now(timezone.utc).isoformat(),
        calls=TransportContracts.calls,
        transcripts=TransportContracts.transcripts,
        settlements=TransportContracts.settlements,
        scope="production C++ transport, owned loopback HTTP/TLS/SOCKS/CONNECT only; direct external DNS and OS secure-store integration unexecuted",
    )
    write_json(out / "result.json", report)
    print(
        json.dumps(
            {
                k: report[k]
                for k in ["selected", "executed", "passed", "failed", "skipped"]
            }
        )
    )
    return int(
        not result.wasSuccessful()
        or selected != 12
        or result.testsRun != selected
        or bool(result.skipped)
    )


if __name__ == "__main__":
    raise SystemExit(main())
