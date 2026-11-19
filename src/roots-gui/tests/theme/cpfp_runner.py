# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Native child-pays-for-parent journey on an explicitly owned Roots regtest node."""

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

TESTS = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(TESTS), str(TESTS / "platform")]
from capture_manifest import encode_capture
from integration.owned_regtest import OwnedRegtest
from replacement_runner import MINER, WALLET
from rpc.funding_runner import READS
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)


def now():
    return datetime.now(timezone.utc).isoformat()


def prepare(node, source, controller):
    def call(method, params=None, wallet=None):
        start = now()
        result = node.call(method, params, wallet=wallet)
        controller.append(
            dict(
                method=method,
                params=params,
                wallet=wallet,
                result=result,
                startedUTC=start,
                completedUTC=now(),
            )
        )
        return result

    def point(tx, n):
        return dict(txid=tx["txid"], vout=n)

    def transaction(inputs, outputs, wallet=MINER, *, submit=True):
        values = [
            {address: f"{Decimal(sats).scaleb(-8):.8f}"} for address, sats in outputs
        ]
        raw = call("createrawtransaction", [inputs, values, 0, False])
        signed = call("signrawtransactionwithwallet", [raw], wallet)
        assert signed["complete"]
        result = call("decoderawtransaction", [signed["hex"]])
        if submit:
            assert (
                call("sendrawtransaction", [signed["hex"], 0.001, 0]) == result["txid"]
            )
        return dict(result, wire=signed["hex"])

    call("createwallet", {"wallet_name": MINER})
    call("createwallet", {"wallet_name": WALLET})
    miner = call("getnewaddress", ["", "bech32"], MINER)
    call("generatetoaddress", [101, miner])
    roots = call("listunspent", wallet=MINER)
    coin = next(coin for coin in roots if coin["spendable"])
    left, right = (call("getnewaddress", ["", "bech32"], MINER) for _ in range(2))
    own0, own1 = (call("getnewaddress", ["", "bech32"], WALLET) for _ in range(2))
    root = transaction(
        [point(coin, coin["vout"])],
        [
            (left, 100000),
            (right, 100000),
            (miner, int(coin["amount"] * 100_000_000) - 201000),
        ],
    )
    branch_left = transaction([point(root, 0)], [(miner, 99700)])
    branch_right = transaction([point(root, 1)], [(miner, 99600)])
    parent = transaction(
        [point(branch_left, 0), point(branch_right, 0)], [(own0, 120000), (own1, 78000)]
    )
    other_child = transaction(
        [point(parent, 0)], [(own0, 119500)], WALLET, submit=False
    )
    assert call("prioritisetransaction", [root["txid"], 0, 250])
    for role, tx in (("parent", parent), ("root", root)):
        (source / (role + ".txt")).write_text(tx["txid"])
    return miner, dict(
        root=root,
        left=branch_left,
        right=branch_right,
        parent=parent,
        otherChild=other_child,
    )


class ChildPackageProxy:
    def __init__(self, node, source, miner, fixtures, controller):
        self.events = []
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.mined = False
        self.spent = False
        self.adjusted = False
        self.lock_trial_started = False
        self.fixture_locked = False
        self.errors = []
        owner = self
        parent = fixtures["parent"]["txid"]
        ancestor = fixtures["root"]["txid"]
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
                            "getmempoolancestors",
                            "getmempoolcluster",
                            "getrawtransaction",
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

                    def actor(method, params, wallet=None):
                        start = now()
                        result = node.call(method, params, wallet=wallet)
                        controller.append(
                            dict(
                                method=method,
                                params=params,
                                wallet=wallet,
                                result=result,
                                startedUTC=start,
                                completedUTC=now(),
                            )
                        )
                        return result

                    selected = [dict(txid=parent, vout=1)]
                    if method == "getaddressinfo" and mode == "locked":
                        with owner.lock:
                            if not owner.lock_trial_started:
                                assert actor("lockunspent", [False, selected], WALLET)
                                owner.lock_trial_started = owner.fixture_locked = True
                                event["actorLockedSelectedOutput"] = selected[0]
                    if (
                        method == "gettxout"
                        and params == [parent, 0, True]
                        and mode == "spent"
                    ):
                        with owner.lock:
                            if not owner.spent:
                                spent = fixtures["otherChild"]
                                assert (
                                    actor(
                                        "sendrawtransaction", [spent["wire"], 0.001, 0]
                                    )
                                    == spent["txid"]
                                )
                                event["actorSpentSelectedOutput"] = spent["txid"]
                                owner.spent = True
                    if method == "getmempoolentry" and params == [parent]:
                        with owner.lock:
                            if mode == "stale-fee" and not owner.adjusted:
                                assert actor(
                                    "prioritisetransaction", [ancestor, 0, 500]
                                )
                                event["actorModifiedBaseFeeDeltaSat"] = 500
                                owner.adjusted = True
                            elif mode == "approve" and owner.adjusted:
                                assert actor(
                                    "prioritisetransaction", [ancestor, 0, -500]
                                )
                                event["actorModifiedBaseFeeDeltaSat"] = -500
                                owner.adjusted = False
                    if (
                        method == "gettxout"
                        and params == [parent, 1, True]
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
                                (block,) = actor("generatetoaddress", [1, miner])
                                decoded = actor("getblock", [block, 1])
                                assert (
                                    parent in decoded["tx"]
                                    and txid not in decoded["tx"]
                                )
                                event["pendingBeforeRace"] = dict(
                                    wire=row[0],
                                    serial=row[1],
                                    outcome=row[2],
                                    txid=txid,
                                )
                                event["confirmedBeforePossibleSend"] = block
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
                        method == "listlockunspent"
                        and params == []
                        and mode == "locked"
                    ):
                        with owner.lock:
                            if owner.fixture_locked:
                                assert selected[0] in json.loads(raw)["result"]
                                assert actor("lockunspent", [True, selected], WALLET)
                                owner.fixture_locked = False
                                event["actorReleasedSelectedOutput"] = selected[0]
                    if (
                        method == "getmempoolancestors"
                        and params == [parent, True]
                        and mode == "missing"
                    ):
                        changed = json.loads(raw)
                        assert changed["result"].pop(ancestor)
                        raw = json.dumps(changed).encode()
                        event["responseBodyDelivered"] = raw.decode()
                    if method == "getmempoolinfo" and mode == "policy-limit":
                        changed = json.loads(raw)
                        changed["result"]["limitclustercount"] = 5
                        raw = json.dumps(changed).encode()
                        event["responseBodyDelivered"] = raw.decode()
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
            prefix="fee-cpfp-", dir=validated_scratch()
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
                    proxy = ChildPackageProxy(node, source, miner, fixtures, controller)
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
                        "native CPFP journey exited " + str(child.returncode)
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
                        "lockunspent",
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
                assert any(
                    row.get("actorSpentSelectedOutput")
                    and json.loads(row["responseBody"])["result"] is None
                    for row in proxy.events
                )
                assert any(
                    row.get("actorReleasedSelectedOutput")
                    in json.loads(row["responseBody"])["result"]
                    for row in proxy.events
                    if row.get("actorReleasedSelectedOutput")
                )
                assert [
                    row["actorModifiedBaseFeeDeltaSat"]
                    for row in proxy.events
                    if "actorModifiedBaseFeeDeltaSat" in row
                ] == [500, -500]
                (output / "decode-final.json").write_text(
                    json.dumps(
                        node.call("decoderawtransaction", [final_hex]),
                        indent=2,
                        default=str,
                    )
                    + "\n"
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
                "unknown-package",
                "unsigned-child-estimate",
                "final-child-actual",
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
        print("Native CPFP evidence", output)
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
        print("Native CPFP evidence", output, file=sys.stderr)
        raise


if __name__ == "__main__":
    main()
