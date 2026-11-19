# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Deterministic real loopback HTTP contract fixtures; reference transport only."""

import sys

sys.dont_write_bytecode = True
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import threading
import socket
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import evidence_directory, write_json
from fake_http import FakeHttp, Step, TransportError, RpcError, request


class Contracts(unittest.TestCase):
    transcripts = []

    def fixture(self, steps):
        server = FakeHttp(steps)
        self.addCleanup(lambda: self.transcripts.extend(server.records))
        return server

    def test_success_wallet_route(self):
        with self.fixture(
            [Step("getwalletinfo", {"private_keys_enabled": False})]
        ) as s:
            self.assertFalse(
                s.call("getwalletinfo", wallet="fixture / watch")[
                    "private_keys_enabled"
                ]
            )
            self.assertTrue(s.records[0]["wallet"])
        self.assertEqual(s.active, 0)

    def test_out_of_order_and_stale_context(self):
        gate = threading.Event()
        first = Step("slow", {"session": "old"}, gate=gate)
        second = Step("fast", {"session": "new"})
        with self.fixture([first, second]) as s:
            with ThreadPoolExecutor(max_workers=2) as pool:
                future = pool.submit(s.call, "slow", request_id=11)
                try:
                    self.assertTrue(
                        first.entered.wait(1), "first request actually held"
                    )
                    new = pool.submit(s.call, "fast", request_id=12).result(timeout=1)
                    self.assertEqual(new, {"session": "new"})
                    self.assertFalse(future.done())
                    current_session = "new"
                    gate.set()
                    old = future.result(timeout=1)
                    self.assertNotEqual(old["session"], current_session)
                    self.assertEqual(s.completed, ["fast", "slow"])
                finally:
                    gate.set()
        self.assertEqual(s.active, 0)

    def test_authentication_rejection(self):
        with self.fixture([Step("read", True)]) as s:
            with self.assertRaises(TransportError) as c:
                request(s.port, "Basic deliberately-invalid", "read")
            self.assertEqual(
                (c.exception.kind, c.exception.status), ("authentication", 401)
            )

    def test_http_error(self):
        with self.fixture([Step("read", status=503)]) as s:
            with self.assertRaises(TransportError) as c:
                s.call("read")
            self.assertEqual((c.exception.kind, c.exception.status), ("http", 503))

    def test_json_errors(self):
        for raw in [b"{", b'{"id":1,"id":2}', b'{"id":NaN}']:
            with (
                self.subTest(kind="malformed-json"),
                self.fixture([Step("read", raw=raw)]) as s,
            ):
                with self.assertRaises(TransportError) as c:
                    s.call("read")
                self.assertEqual(c.exception.kind, "json")

    def test_response_identity(self):
        with self.fixture([Step("read", wrong_id=True)]) as s:
            with self.assertRaises(TransportError) as c:
                s.call("read")
            self.assertEqual(c.exception.kind, "response-identity")

    def test_rpc_error(self):
        with self.fixture([Step("unsupported", error=-32601)]) as s:
            with self.assertRaises(RpcError) as c:
                s.call("unsupported")
            self.assertEqual(c.exception.code, -32601)

    def test_reconnect_after_disconnect(self):
        with self.fixture(
            [Step("first", disconnect=True), Step("second", {"connected": True})]
        ) as s:
            with self.assertRaises(TransportError) as c:
                s.call("first")
            self.assertEqual(c.exception.kind, "connection")
            self.assertTrue(s.call("second")["connected"])
        self.assertEqual(s.active, 0)

    def test_timeout_releases_owned_request(self):
        step = Step("held", True, gate=threading.Event())
        with self.fixture([step]) as s:
            with self.assertRaises(TransportError) as c:
                s.call("held", timeout=0.05)
            self.assertEqual(c.exception.kind, "timeout")
            self.assertTrue(step.entered.is_set())
        self.assertTrue(step.gate.is_set())
        self.assertEqual(s.active, 0)
        self.assertFalse(s.thread.is_alive())

    def test_exception_releases_request_and_server(self):
        step = Step("cancelled", True, gate=threading.Event())
        server = self.fixture([step])
        with self.assertRaisesRegex(RuntimeError, "injected fixture interruption"):
            with server as s:
                with ThreadPoolExecutor(max_workers=1) as pool:
                    future = pool.submit(s.call, "cancelled")
                    try:
                        self.assertTrue(
                            step.entered.wait(1), "owned request held before failure"
                        )
                        raise RuntimeError("injected fixture interruption")
                    finally:
                        step.gate.set()
                        future.result(timeout=1)
        self.assertEqual(server.active, 0)
        self.assertFalse(server.thread.is_alive())

    def test_incomplete_http_request_is_bounded(self):
        server = self.fixture([])
        client = None
        try:
            with server:
                client = socket.create_connection(("127.0.0.1", server.port), timeout=1)
                client.sendall(b"POST / HTTP/1.1\r\nContent-Length: 10\r\n")
                self.assertTrue(
                    server.accepted.wait(1), "incomplete request actually accepted"
                )
                # Keep the owned client open while server close joins its worker.
        finally:
            if client:
                client.close()
        self.assertEqual(server.read_timeouts, 1)
        self.assertEqual(server.active, 0)
        self.assertFalse(server.thread.is_alive())

    def test_transcript_redaction(self):
        with self.fixture([Step("sensitive", {"password": "response-sentinel"})]) as s:
            s.call(
                "sensitive",
                {"passphrase": "request-sentinel", "cookie": "cookie-sentinel"},
            )
            encoded = json.dumps(s.records)
            self.assertTrue(
                all(
                    value not in encoded
                    for value in [
                        s.auth,
                        s.secret,
                        "request-sentinel",
                        "response-sentinel",
                        "cookie-sentinel",
                        "Authorization",
                    ]
                )
            )
            self.assertEqual(
                set(s.records[0]), {"method", "request_id", "wallet", "status"}
            )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", required=True, type=Path)
    a = p.parse_args()
    out = evidence_directory(a.output)
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(Contracts)
    with (out / "cases.log").open("w") as log:
        r = unittest.TextTestRunner(stream=log, verbosity=2).run(suite)
    failed_ids = sorted(
        {test.id().split(" (", 1)[0] for test, _ in r.failures + r.errors}
    )
    executed = r.testsRun - len(r.skipped)
    report = dict(
        selected=12,
        executed=executed,
        passed=executed - len(failed_ids),
        failed=len(failed_ids),
        skipped=len(r.skipped),
        failedCaseIDs=failed_ids,
        failedSubcaseRecords=len(r.failures) + len(r.errors),
        jsonMalformedSubcases=3,
        transcript=Contracts.transcripts,
        scope="test-only HTTP reference client; production connection service is later work",
    )
    write_json(out / "result.json", report)
    write_json(out / "transcript.json", Contracts.transcripts)
    print(
        json.dumps(
            {
                k: report[k]
                for k in ["selected", "executed", "passed", "failed", "skipped"]
            }
        )
    )
    return int(not r.wasSuccessful() or executed != 12)


if __name__ == "__main__":
    raise SystemExit(main())
