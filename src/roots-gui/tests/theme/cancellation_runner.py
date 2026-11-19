# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Native competing self-payment journey on an explicitly owned Roots regtest node."""

import argparse
from contextlib import closing
from datetime import datetime, timezone
from decimal import Decimal
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time

TESTS = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(TESTS), str(TESTS / "platform")]
from capture_manifest import encode_capture
from integration.owned_regtest import OwnedRegtest
from replacement_runner import WALLET, prepare
from rpc.funding_runner import READS
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)


def now():
    return datetime.now(timezone.utc).isoformat()


class CancellationProxy:
    def __init__(self, node, source, miner, fixtures, controller):
        self.events = []
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.mined = False
        self.errors = []
        owner = self
        original = fixtures["eligible"]["txid"]
        wallet_route = "/wallet/" + WALLET

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def do_POST(self):
                body = self.rfile.read(int(self.headers["Content-Length"]))
                request = json.loads(body, parse_float=Decimal)
                method, params = request["method"], request["params"]
                mode = (
                    (source / "mode").read_text() if (source / "mode").exists() else ""
                )
                event = dict(
                    method=method,
                    route=self.path,
                    mode=mode,
                    receivedUTC=now(),
                    requestBody=body.decode(),
                )
                owner.events.append(event)
                try:
                    if (
                        self.path not in ("/", wallet_route)
                        or method
                        not in READS
                        | {
                            "getaddressinfo",
                            "getmempoolentry",
                            "walletprocesspsbt",
                            "testmempoolaccept",
                        }
                        or (
                            method == "listdescriptors" and params != {"private": False}
                        )
                    ):
                        event["blocked"] = "unexpected RPC or route"
                        self.send_error(403)
                        return
                    if method == "walletprocesspsbt":
                        assert mode == "explicit-sign" and self.path == wallet_route
                        assert params["sign"] is True and params["finalize"] is False
                    if method == "testmempoolaccept":
                        assert (
                            mode in {"preflight", "broadcast-race"} and self.path == "/"
                        )
                        assert params[1] == Decimal("0.001")
                    if (
                        method == "gettransaction"
                        and params == [original]
                        and mode == "broadcast-race"
                    ):
                        # Controller-only race after the application commits
                        # the exact Pending attempt and just before it could send.
                        txid = (source / "final-txid.txt").read_text()
                        journal_uri = (source / "metadata.sqlite").as_uri() + "?mode=ro"
                        with (
                            owner.lock,
                            closing(sqlite3.connect(journal_uri, uri=True)) as db,
                        ):
                            row = db.execute(
                                "SELECT wire,serial,outcome FROM broadcast_details WHERE txid=?",
                                (txid,),
                            ).fetchone()
                            if row and row[2] == 0 and not owner.mined:
                                started = now()
                                (block,) = node.call("generatetoaddress", [1, miner])
                                decoded = node.call("getblock", [block])
                                assert (
                                    original in decoded["tx"]
                                    and txid not in decoded["tx"]
                                )
                                event["pendingBeforeRace"] = dict(
                                    wire=row[0],
                                    serial=row[1],
                                    outcome=row[2],
                                    txid=txid,
                                )
                                event["confirmedBeforePossibleSend"] = block
                                controller.append(
                                    dict(
                                        method="generatetoaddress + getblock",
                                        params=[1, miner],
                                        result=decoded,
                                        startedUTC=started,
                                        completedUTC=now(),
                                    )
                                )
                                owner.mined = True
                                (source / "race-mined").touch()
                    with closing(
                        http.client.HTTPConnection("127.0.0.1", node.port, timeout=8)
                    ) as remote:
                        remote.request(
                            "POST",
                            self.path,
                            body,
                            {
                                "Authorization": self.headers.get("Authorization", ""),
                                "Content-Type": "application/json",
                            },
                        )
                        response = remote.getresponse()
                        raw = response.read()
                        status = response.status
                    event.update(responseBody=raw.decode(), upstreamStatus=status)
                    if (
                        method == "getwalletinfo"
                        and request["id"] == 3
                        and mode == "wrong-wallet"
                    ):
                        wrong = json.loads(raw)
                        wrong["result"]["walletname"] = "different-wallet"
                        raw = json.dumps(wrong).encode()
                        event["responseBodyDelivered"] = raw.decode()
                    if (
                        method == "getmempoolentry"
                        and params == [original]
                        and mode == "stale-witness"
                    ):
                        wrong = json.loads(raw)
                        wrong["result"]["wtxid"] = "f" * 64
                        raw = json.dumps(wrong).encode()
                        event["responseBodyDelivered"] = raw.decode()
                    if method == "listdescriptors" and mode == "stale-owner-policy":
                        changed = json.loads(raw)
                        changed["result"]["descriptors"] = [
                            descriptor
                            for descriptor in changed["result"]["descriptors"]
                            if not descriptor.get("internal")
                        ]
                        raw = json.dumps(changed).encode()
                        event["responseBodyDelivered"] = raw.decode()
                    if (
                        method == "getmempoolentry"
                        and params == [original]
                        and mode == "held"
                    ):
                        (source / "read-held").touch()
                        until = time.monotonic() + 6
                        while (
                            not (source / "release-read").exists()
                            and time.monotonic() < until
                            and not owner.stop.wait(0.02)
                        ):
                            pass
                        event["readHeldForCancellation"] = True
                        time.sleep(0.10)
                    if (
                        method == "getmempoolentry"
                        and params == [original]
                        and mode == "approval-held"
                    ):
                        assert not (source / "approval-held").exists()
                        (source / "approval-held").touch()
                        until = time.monotonic() + 6
                        while (
                            not (source / "approval-release").exists()
                            and time.monotonic() < until
                            and not owner.stop.wait(0.02)
                        ):
                            pass
                        assert (source / "approval-release").exists(), (
                            "owned approval gate deadline"
                        )
                        event["approvalReplyReleasedAfterReviewClosed"] = True
                    self.send_response(status)
                    self.send_header("Content-Length", str(len(raw)))
                    self.end_headers()
                    self.wfile.write(raw)
                except (BrokenPipeError, ConnectionResetError):
                    event["clientRetired"] = True
                except (
                    AssertionError,
                    KeyError,
                    ValueError,
                    OSError,
                    sqlite3.Error,
                ) as error:
                    owner.errors.append(type(error).__name__)
                    self.send_error(500)

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = False
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()

    def close(self):
        self.stop.set()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)
        assert not self.thread.is_alive()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("native", "fonts", "daemon", "output"):
        parser.add_argument("--" + key, required=True, type=Path)
    args = parser.parse_args()
    output = Path(
        tempfile.mkdtemp(prefix="attempt-", dir=evidence_directory(args.output))
    )
    started = now()
    controller, argv, child = [], None, None
    try:
        with tempfile.TemporaryDirectory(
            prefix="fee-cancellation-", dir=validated_scratch()
        ) as name:
            source = Path(name)
            with OwnedRegtest(
                args.daemon, output / "node", public_descriptors=True
            ) as node:
                node.ready()
                fixtures = {}
                proxy = None
                try:
                    miner, fixtures = prepare(node, source, controller)
                    proxy = CancellationProxy(node, source, miner, fixtures, controller)
                    argv = [
                        sys.executable,
                        "-B",
                        str(TESTS / "platform/owned_display.py"),
                        "--timeout",
                        "65",
                        "--output",
                        str(output / "display"),
                        "--",
                        str(args.native),
                        str(output),
                        str(args.fonts),
                        str(source),
                        str(proxy.port),
                        str(node.data / "regtest/.cookie"),
                    ]
                    native_started = now()
                    child = subprocess.run(
                        argv, text=True, capture_output=True, timeout=75
                    )
                    native_completed = now()
                    (output / "native.log").write_text(child.stdout + child.stderr)
                finally:
                    if proxy:
                        proxy.close()
                        write_json(output / "transcript.json", proxy.events)
                        write_json(output / "proxy-errors.json", proxy.errors)
                    (output / "controller.json").write_text(
                        json.dumps(controller, indent=2, default=str) + "\n"
                    )
                    (output / "fixtures.json").write_text(
                        json.dumps(fixtures, indent=2, default=str) + "\n"
                    )
                if child.returncode:
                    raise RuntimeError(
                        "native cancellation journey exited " + str(child.returncode)
                    )
                assert not proxy.errors and not any(
                    event.get("blocked") for event in proxy.events
                )
                mutations = [
                    event
                    for event in proxy.events
                    if event["method"]
                    in {
                        "walletprocesspsbt",
                        "sendrawtransaction",
                        "walletpassphrase",
                        "finalizepsbt",
                        "bumpfee",
                        "psbtbumpfee",
                        "getnewaddress",
                        "getrawchangeaddress",
                    }
                ]
                (only,) = mutations
                assert (
                    only["method"] == "walletprocesspsbt"
                    and only["mode"] == "explicit-sign"
                )
                preflight = [
                    event
                    for event in proxy.events
                    if event["method"] == "testmempoolaccept"
                ]
                assert [event["mode"] for event in preflight] == [
                    "preflight",
                    "broadcast-race",
                ]
                final_hex = (output / "candidate-final.tx").read_bytes().hex()
                for event in preflight:
                    assert json.loads(event["requestBody"])["params"][0] == [final_hex]
                    assert json.loads(event["responseBody"])["result"][0]["allowed"]
                (race,) = [
                    event
                    for event in proxy.events
                    if "confirmedBeforePossibleSend" in event
                ]
                txid = (source / "final-txid.txt").read_text()
                assert race["pendingBeforeRace"] == dict(
                    wire=final_hex, serial=1, outcome=0, txid=txid
                )
                assert json.loads(race["responseBody"])["result"]["confirmations"] == 1
                assert any(row.get("readHeldForCancellation") for row in proxy.events)
                (approval,) = [
                    row
                    for row in proxy.events
                    if row.get("approvalReplyReleasedAfterReviewClosed")
                ]
                assert (
                    approval["mode"] == "approval-held"
                    and approval["upstreamStatus"] == 200
                )
                with closing(
                    sqlite3.connect(
                        (source / "metadata.sqlite").as_uri() + "?mode=ro", uri=True
                    )
                ) as db:
                    (final_record,) = db.execute(
                        "SELECT txid,wire,serial,outcome FROM broadcast_details"
                    ).fetchall()
                assert final_record == (txid, final_hex, 1, 1)  # Unsent
                write_json(output / "journal.json", final_record)
        process = json.loads((output / "node/process.json").read_text())
        display = json.loads((output / "display/processes.json").read_text())
        assert (
            process["settled"]
            and process["datadirRemoved"]
            and process["authMaterialAbsentFromRetainedLogs"]
        )
        assert display["child"]["settled"] and all(
            row["settled"] and not row.get("remainingLiveGroupMembers")
            for row in display["processes"]
        )
        captures = {
            key: encode_capture(output / (key + ".ppm"))
            for key in (
                "attempt-cancellation",
                "competing-self-payment",
                "original-confirmed-first",
            )
        }
        write_json(
            output / "report.json",
            dict(
                startedUTC=started,
                completedUTC=now(),
                nativeStartedUTC=native_started,
                nativeCompletedUTC=native_completed,
                argv=argv,
                cwd=str(Path.cwd()),
                exit=child.returncode,
                binary=binary_identity(args.native),
                scratchRemoved=not Path(name).exists(),
                proxySettled=not proxy.thread.is_alive(),
                roots=process["backend"],
                native=json.loads((output / "native-result.json").read_text()),
                captures=captures,
                distinctExplicitSignRequests=1,
                productSubmissionRequests=0,
                selected=1,
                executed=1,
                passed=1,
                failed=0,
                skipped=0,
            ),
        )
        print("Native cancellation evidence", output)
    except Exception as error:
        write_json(
            output / "failure.json",
            dict(
                startedUTC=started,
                completedUTC=now(),
                argv=argv,
                cwd=str(Path.cwd()),
                exit=child.returncode if child else None,
                message=str(error),
            ),
        )
        print("Native cancellation evidence", output, file=sys.stderr)
        raise


if __name__ == "__main__":
    main()
