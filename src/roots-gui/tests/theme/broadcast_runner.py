# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""One native submission journey using only a disposable Roots regtest daemon."""

import argparse
from contextlib import closing
from datetime import datetime, timezone
from decimal import Decimal
import hashlib
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
from shutil import copytree
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time

TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))
sys.path.insert(0, str(TESTS / "platform"))
from capture_manifest import encode_capture
from integration.owned_regtest import OwnedRegtest
from protocol.signature_exchange import (
    KEYS,
    compact,
    integer,
    packed,
    sha2,
    sign,
    vector,
    wire_facts,
)
from rpc.funding_runner import READS
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)

WALLET = "gui-broadcast-watch"
GENESIS = "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"
FEE = 1000
MAX_FEE = Decimal("0.00100000")
POLICY = "testmempoolaccept"
SUBMIT = "sendrawtransaction"
MONITOR_READS = {
    "getblockhash",
    "getblockchaininfo",
    "getbestblockhash",
    "getblockheader",
    "getmempoolentry",
    "getrawtransaction",
    "gettransaction",
    "gettxspendingprevout",
}


def now():
    return datetime.now(timezone.utc).isoformat()


def native_environment(profile):
    environment = os.environ.copy()
    environment.pop("ROOTS_GUI_BROADCAST_VISUAL_PROFILE", None)
    if profile:
        environment["ROOTS_GUI_BROADCAST_VISUAL_PROFILE"] = profile
    return environment


def capture_for_profile(out, name, profile):
    capture = encode_capture(out / (name + ".ppm"))
    native = json.loads((out / (name + ".native.json")).read_text())
    if profile:
        reference = profile.startswith("reference-")
        expected = (
            (1200, 820, 1, profile.endswith("-dark"))
            if reference
            else (960, 640, 1.5, profile.endswith("-dark"))
        )
        assert (
            tuple(native[key] for key in ("width", "height", "scale", "dark"))
            == expected
        )
    capture["native"] = native
    return capture


def prepare(node, directory, openssl, recovery=False, monitor=False):
    descriptor = "sh(multi(2," + ",".join(key.hex() for key in KEYS) + "))"
    redeem = b"\x52" + b"".join(vector(key) for key in KEYS) + b"\x53\xae"
    policy = node.call("getdescriptorinfo", [descriptor])["descriptor"]
    (address,) = node.call("deriveaddresses", [policy])
    node.call(
        "createwallet",
        {"wallet_name": WALLET, "disable_private_keys": True, "blank": True},
    )
    (imported,) = node.call(
        "importdescriptors", [[dict(desc=policy, timestamp="now")]], wallet=WALLET
    )
    assert imported["success"]
    node.call("generatetoaddress", [104, address])
    coins = sorted(
        node.call("listunspent", wallet=WALLET), key=lambda coin: -coin["confirmations"]
    )
    assert len(coins) >= 2 and min(coin["confirmations"] for coin in coins[:2]) >= 101
    known = {}
    with tempfile.TemporaryDirectory(
        prefix="fixture-key-", dir=validated_scratch()
    ) as secrets:
        keys = []
        for i in (1, 2):
            key = Path(secrets) / f"{i}.der"
            key.touch(mode=0o600)
            key.write_bytes(
                bytes.fromhex("302e0201010420")
                + i.to_bytes(32, "big")
                + bytes.fromhex("a00706052b8104000a")
            )
            keys.append(key)
        cases = [
            ("locktime", coins[0], 499999, 0x51, FEE),
            ("accepted", coins[0], 0, 0x53, FEE),
            ("lost-reply", coins[1], 0, 0x55, FEE),
        ]
        if recovery:
            cases.append(("retry", coins[2], 0, 0x57, FEE))
        if monitor:
            cases.extend(
                (
                    ("replacement", coins[0], 0, 0x59, 6000),
                    ("trigger", coins[2], 0, 0x5B, FEE),
                )
            )
        for case, coin, locktime, tag, fee in cases:
            folder = directory / case
            folder.mkdir()
            previous = bytes.fromhex(
                node.call("gettransaction", [coin["txid"]], wallet=WALLET)["hex"]
            )
            previous_txid = wire_facts(previous)["txid"]
            assert previous_txid == coin["txid"]
            point = bytes.fromhex(previous_txid)[::-1] + integer(coin["vout"])
            amount = int(Decimal(str(coin["amount"])) * 100000000) - fee
            destination = b"\x00\x14" + bytes([tag]) * 20
            sequence = integer(0xFFFFFFFD)

            def transaction(script):
                return (
                    integer(2)
                    + b"\1"
                    + point
                    + vector(script)
                    + sequence
                    + b"\1"
                    + integer(amount, 8)
                    + vector(destination)
                    + integer(locktime)
                )

            digest = sha2(transaction(redeem) + integer(1))
            signatures = [sign(openssl, key, digest) for key in keys]
            script_sig = (
                b"\0"
                + b"".join(vector(sig) for sig in signatures)
                + b"\x4c"
                + bytes([len(redeem)])
                + redeem
            )
            final = transaction(script_sig)
            common = {
                b"\xfb": integer(2),
                b"\x02": integer(2),
                b"\x03": integer(locktime),
                b"\x04": compact(1),
                b"\x05": compact(1),
            }
            prevout = {
                b"\x00": previous,
                b"\x04": redeem,
                b"\x0e": point[:32],
                b"\x0f": point[32:],
                b"\x10": sequence,
            }
            output = {b"\x03": integer(amount, 8), b"\x04": destination}
            (folder / "original.psbt").write_bytes(packed([common, prevout, output]))
            signed = {b"\x02" + key: sig for key, sig in zip(KEYS, signatures)}
            (folder / "signed.psbt").write_bytes(
                packed([common, {**prevout, **signed}, output])
            )
            (folder / "expected.tx").write_bytes(final)
            (folder / "previous.hex").write_text(previous.hex())
            known[case] = dict(
                wire=final.hex(),
                sha256=hashlib.sha256(final).hexdigest(),
                facts=wire_facts(final),
                outpoint=dict(txid=coin["txid"], vout=coin["vout"]),
                fee=fee,
                locktime=locktime,
            )
    assert not Path(secrets).exists()
    (directory / "descriptor.txt").write_text(policy)
    (directory / "redeem.hex").write_text(redeem.hex())
    return known


class BroadcastProxy:
    def __init__(self, node, known, store, recovery=False, monitor=False):
        self.events = []
        self.writes = set()
        self.crash_release = threading.Event()
        proxy = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def do_POST(self):
                body = self.rfile.read(int(self.headers["Content-Length"]))
                request = json.loads(body, parse_float=Decimal)
                method, params = request["method"], request["params"]
                event = dict(method=method, route=self.path, receivedUTC=now())
                proxy.events.append(event)
                if (
                    method not in READS | {POLICY, SUBMIT} | MONITOR_READS
                    or method == "listdescriptors"
                    and params != {"private": False}
                ):
                    event["blocked"] = "method or scope"
                    self.send_error(403)
                    return
                if method in (POLICY, SUBMIT):
                    # Keep exact public params but never request headers or a wallet authentication value.
                    event["requestBody"] = body.decode()
                    try:
                        raw = params[0][0] if method == POLICY else params[0]
                        (name,) = [
                            key for key, value in known.items() if value["wire"] == raw
                        ]
                        expected = known[name]
                        event.update(case=name, wireSHA256=expected["sha256"])
                        assert self.path == "/" and params[1] == MAX_FEE
                        if method == POLICY:
                            assert params == [[raw], MAX_FEE]
                        else:
                            assert params == [raw, MAX_FEE, 0] and name in (
                                "accepted",
                                "lost-reply",
                                "retry",
                            )
                            with closing(
                                sqlite3.connect(
                                    store.as_uri() + "?mode=ro", uri=True, timeout=0.1
                                )
                            ) as reader:
                                row = reader.execute(
                                    "SELECT genesis,txid,wtxid,wire_digest,observed_ms FROM broadcast_attempts WHERE genesis=? AND txid=?",
                                    (GENESIS, expected["facts"]["txid"]),
                                ).fetchone()
                                detail = reader.execute(
                                    "SELECT wire,serial,attempted_ms,outcome FROM broadcast_details WHERE genesis=? AND txid=?",
                                    (GENESIS, expected["facts"]["txid"]),
                                ).fetchone()
                            assert (
                                row
                                and row[:4]
                                == (
                                    GENESIS,
                                    expected["facts"]["txid"],
                                    expected["facts"]["wtxid"],
                                    expected["sha256"],
                                )
                                and row[4] > 0
                            )
                            assert (
                                detail
                                and detail[0] == raw
                                and detail[3] == 0
                                and (name, detail[1]) not in proxy.writes
                            )
                            event["durablyObservedBeforeForwarding"] = list(row)
                            event["attemptBeforeForwarding"] = dict(
                                serial=detail[1], attemptedMs=detail[2], pending=True
                            )
                            proxy.writes.add((name, detail[1]))
                            if recovery and name == "retry" and detail[1] == 1:
                                event["connectionDroppedBeforeForwarding"] = True
                                self.close_connection = True
                                return
                    except (
                        AssertionError,
                        ValueError,
                        IndexError,
                        KeyError,
                        sqlite3.Error,
                    ):
                        event["blocked"] = (
                            "wrong wire, fee cap, duplicate, or missing pre-send durable record"
                        )
                        self.send_error(403)
                        return
                status_call = method in (
                    "getmempoolentry",
                    "getrawtransaction",
                    "gettransaction",
                ) and (
                    isinstance(params, list)
                    and bool(params)
                    and isinstance(params[0], str)
                    or isinstance(params, dict)
                    and "txid" in params
                )
                if recovery and status_call:
                    txid = params[0] if isinstance(params, list) else params["txid"]
                    events = [
                        key
                        for key, value in known.items()
                        if value["facts"]["txid"] == txid
                    ]
                    if events:
                        event["statusCase"] = events[0]
                    marker = store.parent / "status-mode"
                    mode = marker.read_text().strip() if marker.exists() else ""
                    if (
                        mode == "unavailable"
                        and method == "getrawtransaction"
                        and event.get("statusCase") == "retry"
                    ):
                        event["statusFixture"] = "unavailable"
                        self.send_error(503)
                        return
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
                    event["upstreamStatus"] = response.status
                    if method in (POLICY, SUBMIT):
                        event["responseBody"] = raw.decode()
                    if monitor and method in MONITOR_READS:
                        event["requestBody"] = body.decode()
                        event["responseBody"] = raw.decode()
                    if method == SUBMIT and event["case"] == "lost-reply":
                        event["responseDroppedAfterUpstream"] = True
                        if recovery:
                            (store.parent / "accepted-before-exit").touch()
                            proxy.crash_release.wait(timeout=9)
                        self.close_connection = True
                        return
                    if recovery and method == SUBMIT and event["case"] == "retry":
                        event["statusFixture"] = (
                            "already-known RPC response after actual upstream acceptance"
                        )
                        raw = json.dumps(
                            dict(
                                jsonrpc="2.0",
                                id=request["id"],
                                error=dict(code=-27, message="already known"),
                            )
                        ).encode()
                    if recovery and status_call and event.get("statusCase"):
                        event["requestBody"] = body.decode()
                        event["upstreamBody"] = raw.decode()
                        if mode == "mismatch" and method == "getmempoolentry":
                            reply = json.loads(raw)
                            reply["result"]["wtxid"] = "f" * 64
                            raw = json.dumps(reply).encode()
                            event["statusFixture"] = "mismatched witness hash"
                    if recovery and (
                        method == SUBMIT or status_call and event.get("statusCase")
                    ):
                        event["deliveredBody"] = raw.decode()
                    try:
                        self.send_response(response.status)
                        self.send_header("Content-Type", "application/json")
                        self.send_header("Content-Length", str(len(raw)))
                        self.end_headers()
                        self.wfile.write(raw)
                    except (BrokenPipeError, ConnectionResetError):
                        event["localWaitRetired"] = True

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = False
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()

    @property
    def port(self):
        return self.server.server_address[1]

    def close(self):
        self.crash_release.set()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)
        assert not self.thread.is_alive()


def recovery_journey(args, out):
    start = now()
    processes = []
    try:
        with tempfile.TemporaryDirectory(
            prefix="broadcast-recovery-", dir=validated_scratch()
        ) as temporary:
            source = Path(temporary) / "public"
            source.mkdir(mode=0o700)
            store = source / "metadata.sqlite"
            with OwnedRegtest(args.daemon, out / "node") as node:
                node.ready()
                known = prepare(node, source, args.openssl, recovery=True)
                proxy = BroadcastProxy(node, known, store, recovery=True)
                stop_mining = threading.Event()
                mining_result = {}

                def mine_after_status():
                    while not stop_mining.wait(0.01):
                        if not (source / "mine-after-observation").exists():
                            continue
                        try:
                            observed = node.call("getrawmempool")
                            assert set(observed) == {
                                known[name]["facts"]["txid"]
                                for name in ("retry", "lost-reply")
                            }
                            write_json(
                                out / "mempool-observation.json",
                                dict(observedUTC=now(), txids=observed),
                            )
                            (address,) = node.call(
                                "deriveaddresses",
                                [(source / "descriptor.txt").read_text()],
                            )
                            (block,) = node.call("generatetoaddress", [1, address])
                            exact = node.call("getblock", [block])
                            assert (
                                set(observed) <= set(exact["tx"])
                                and exact["confirmations"] == 1
                            )
                            mining_result.update(
                                block=block, txids=observed, observedUTC=now()
                            )
                            (source / "mined-block").write_text(block)
                        except Exception as error:
                            mining_result.update(error=str(error))
                        return

                miner = threading.Thread(target=mine_after_status)
                miner.start()
                try:
                    for role, wanted in (("interrupt", 71), ("restart", 0)):
                        role_dir = out / role
                        role_dir.mkdir()
                        argv = [
                            sys.executable,
                            "-B",
                            str(TESTS / "platform/owned_display.py"),
                            "--output",
                            str(role_dir / "display"),
                            "--timeout",
                            "40",
                            "--",
                            str(args.native),
                            str(role_dir),
                            str(args.fonts),
                            str(source),
                            str(proxy.port),
                            str(node.data / "regtest/.cookie"),
                            role,
                        ]
                        started = now()
                        child = subprocess.run(
                            argv, text=True, capture_output=True, timeout=45
                        )
                        processes.append(
                            dict(
                                role=role,
                                argv=argv,
                                cwd=str(Path.cwd()),
                                exit=child.returncode,
                                expected=wanted,
                                startedUTC=started,
                                completedUTC=now(),
                            )
                        )
                        (role_dir / "native.log").write_text(
                            child.stdout + child.stderr
                        )
                        if role == "interrupt":
                            proxy.crash_release.set()
                        if child.returncode != wanted:
                            raise RuntimeError(
                                f"{role} process exited {child.returncode}"
                            )
                finally:
                    stop_mining.set()
                    proxy.close()
                    miner.join(timeout=12)
                    assert not miner.is_alive()
                    write_json(out / "mined-block.json", mining_result)
                    write_json(out / "transcript.json", proxy.events)
                    write_json(out / "fixtures.json", known)
                    copytree(
                        source,
                        out / "public-fixtures",
                        ignore=lambda _, names: [
                            name for name in names if name.startswith("metadata.sqlite")
                        ],
                    )
                with closing(
                    sqlite3.connect(store.as_uri() + "?mode=ro", uri=True)
                ) as db:
                    rows = db.execute(
                        "SELECT genesis,txid,serial,attempted_ms,outcome,outcome_ms FROM broadcast_details ORDER BY txid"
                    ).fetchall()
                write_json(out / "journal.json", rows)
                mutating = [e for e in proxy.events if e["method"] == SUBMIT]
                assert all(not e.get("blocked") for e in proxy.events)
                assert [
                    (e["case"], e["attemptBeforeForwarding"]["serial"])
                    for e in mutating
                ] == [("retry", 1), ("lost-reply", 1), ("retry", 2)]
                assert (
                    mutating[0]["connectionDroppedBeforeForwarding"]
                    and mutating[1]["responseDroppedAfterUpstream"]
                )
                assert json.loads(mutating[2]["deliveredBody"])["error"]["code"] == -27
                assert mining_result["block"] and node.call("getrawmempool") == []
                assert {(r[1], r[2], r[4]) for r in rows} == {
                    (known["lost-reply"]["facts"]["txid"], 1, 0),
                    (known["retry"]["facts"]["txid"], 2, 4),
                }
        daemon = json.loads((out / "node/process.json").read_text())
        assert (
            daemon["settled"]
            and daemon["datadirRemoved"]
            and daemon["authMaterialAbsentFromRetainedLogs"]
        )
        for process in processes:
            display = json.loads(
                (out / process["role"] / "display/processes.json").read_text()
            )
            assert (
                display["child"]["settled"]
                and display["child"]["exit"] == process["expected"]
            )
            assert all(
                x["settled"] and not x.get("remainingLiveGroupMembers")
                for x in display["processes"]
            )
        captures = {
            name: encode_capture(out / role / (name + ".ppm"))
            for role, name in (
                ("interrupt", "submission-uncertain"),
                ("restart", "submission-recovered"),
            )
        }
        write_json(
            out / "report.json",
            dict(
                startedUTC=start,
                completedUTC=now(),
                processes=processes,
                binary=binary_identity(args.native),
                scratchRemoved=not Path(temporary).exists(),
                proxySettled=not proxy.thread.is_alive(),
                roots=daemon["backend"],
                interruption=json.loads(
                    (out / "interrupt/interruption.json").read_text()
                ),
                restart=json.loads((out / "restart/native-result.json").read_text()),
                captures=captures,
                selected=1,
                executed=1,
                passed=1,
                failed=0,
                skipped=0,
            ),
        )
        print("Broadcast recovery evidence:", out)
        return 0
    except Exception as error:
        write_json(
            out / "failure.json",
            dict(
                startedUTC=start,
                completedUTC=now(),
                message=str(error),
                processes=processes,
            ),
        )
        print("Broadcast recovery evidence:", out, file=sys.stderr)
        raise


def monitor_journey(args, out):
    start = now()
    controller = []
    started_native = None
    child = None
    try:
        with tempfile.TemporaryDirectory(
            prefix="broadcast-monitor-", dir=validated_scratch()
        ) as temporary:
            source = Path(temporary) / "public"
            source.mkdir(mode=0o700)
            store = source / "metadata.sqlite"
            with OwnedRegtest(args.daemon, out / "node", monitor_fixture=True) as node:
                node.ready()
                known = prepare(node, source, args.openssl, monitor=True)
                (address,) = node.call(
                    "deriveaddresses", [(source / "descriptor.txt").read_text()]
                )
                proxy = BroadcastProxy(node, known, store, monitor=True)
                cancelled = threading.Event()
                accepted = known["accepted"]["facts"]["txid"]
                replacement = known["replacement"]["facts"]["txid"]
                trigger = known["trigger"]["facts"]["txid"]
                faults = []

                def act(method, params=None):
                    begin = now()
                    reply = node.call(method, params)
                    controller.append(
                        dict(
                            method=method,
                            params=params,
                            startedUTC=begin,
                            completedUTC=now(),
                            result=reply,
                        )
                    )
                    return reply

                def send(case):
                    reply = act("sendrawtransaction", [known[case]["wire"], 0.001, 0])
                    assert reply == known[case]["facts"]["txid"]

                def pool(expected):
                    assert set(act("getrawmempool")) == expected

                def dispatch():
                    original_block = None
                    try:
                        for step in (
                            "mine",
                            "invalidate",
                            "reconsider",
                            "offline",
                            "remove-again",
                            "expire",
                            "refill",
                            "replace",
                            "conflict",
                        ):
                            while not (source / ("request-" + step)).exists():
                                if cancelled.wait(0.015):
                                    return
                            if step == "mine":
                                pool({accepted})
                                (original_block,) = act(
                                    "generatetoaddress", [1, address]
                                )
                                assert (
                                    accepted in act("getblock", [original_block])["tx"]
                                )
                                value = original_block
                            elif step in ("invalidate", "remove-again"):
                                act("invalidateblock", [original_block])
                                pool({accepted})
                                value = act("getbestblockhash")
                                assert value != original_block
                            elif step == "reconsider":
                                act("reconsiderblock", [original_block])
                                value = act("getbestblockhash")
                                assert value == original_block
                                pool(set())
                            elif step == "offline":

                                def count():
                                    return sum(
                                        item["method"]
                                        in {
                                            "getmempoolentry",
                                            "gettxspendingprevout",
                                            "getrawtransaction",
                                        }
                                        for item in proxy.events
                                    )

                                before = count()
                                begin = now()
                                assert not cancelled.wait(1.25) and count() == before
                                controller.append(
                                    dict(
                                        method="offline observation window",
                                        startedUTC=begin,
                                        completedUTC=now(),
                                        matchingReadsBefore=before,
                                        matchingReadsAfter=count(),
                                    )
                                )
                                value = "paused"
                            elif step == "expire":
                                act("setmocktime", [int(time.time()) + 7200])
                                send("trigger")
                                pool({trigger})
                                value = "expired in owned fixture"
                            elif step == "refill":
                                send("accepted")
                                pool({accepted, trigger})
                                value = "owned fixture reintroduced exact transaction"
                            elif step == "replace":
                                send("replacement")
                                pool({replacement, trigger})
                                value = replacement
                            else:
                                (conflict,) = act("generatetoaddress", [1, address])
                                assert replacement in act("getblock", [conflict])["tx"]
                                assert accepted not in controller[-1]["result"]["tx"]
                                pool(set())
                                value = conflict
                            (source / ("result-" + step)).write_text(value)
                    except Exception as error:
                        faults.append(str(error))

                controller_thread = threading.Thread(target=dispatch)
                controller_thread.start()
                argv = [
                    sys.executable,
                    "-B",
                    str(TESTS / "platform/owned_display.py"),
                    "--output",
                    str(out / "display"),
                    "--timeout",
                    "95",
                    "--",
                    str(args.native),
                    str(out),
                    str(args.fonts),
                    str(source),
                    str(proxy.port),
                    str(node.data / "regtest/.cookie"),
                    "monitor",
                ]
                try:
                    started_native = now()
                    child = subprocess.run(
                        argv,
                        text=True,
                        capture_output=True,
                        timeout=105,
                        env=native_environment(args.profile),
                    )
                    completed_native = now()
                    (out / "native.log").write_text(child.stdout + child.stderr)
                finally:
                    cancelled.set()
                    controller_thread.join(timeout=8)
                    proxy.close()
                    assert not controller_thread.is_alive()
                    write_json(out / "transcript.json", proxy.events)
                    (out / "controller.json").write_text(
                        json.dumps(
                            dict(calls=controller, failed=faults), indent=2, default=str
                        )
                        + "\n"
                    )
                    write_json(out / "fixtures.json", known)
                    copytree(
                        source,
                        out / "public-fixtures",
                        ignore=lambda _, names: [
                            name for name in names if name.startswith("metadata.sqlite")
                        ],
                    )
                if child.returncode or faults:
                    raise RuntimeError(
                        f"native exit {child.returncode}; controller failures {faults}"
                    )
                assert all(not event.get("blocked") for event in proxy.events)
                (only,) = [event for event in proxy.events if event["method"] == SUBMIT]
                assert (
                    only["case"] == "accepted"
                    and only["attemptBeforeForwarding"]["serial"] == 1
                )
                with closing(
                    sqlite3.connect(store.as_uri() + "?mode=ro", uri=True)
                ) as db:
                    rows = db.execute(
                        "SELECT genesis,txid,wire,serial,outcome FROM broadcast_details"
                    ).fetchall()
                assert rows == [(GENESIS, accepted, known["accepted"]["wire"], 1, 3)]
                write_json(out / "journal.json", rows)
        process = json.loads((out / "node/process.json").read_text())
        display = json.loads((out / "display/processes.json").read_text())
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
            name: capture_for_profile(out, name, args.profile)
            for name in ("in-mempool", "confirmed", "reorged")
        }
        write_json(
            out / "report.json",
            dict(
                startedUTC=start,
                completedUTC=now(),
                nativeStartedUTC=started_native,
                nativeCompletedUTC=completed_native,
                argv=argv,
                cwd=str(Path.cwd()),
                exit=child.returncode,
                binary=binary_identity(args.native),
                scratchRemoved=not Path(temporary).exists(),
                proxySettled=not proxy.thread.is_alive(),
                controllerSettled=not controller_thread.is_alive(),
                roots=process["backend"],
                native=json.loads((out / "native-result.json").read_text()),
                captures=captures,
                profile=args.profile,
                selected=1,
                executed=1,
                passed=1,
                failed=0,
                skipped=0,
            ),
        )
        print("Broadcast monitor evidence:", out)
        return 0
    except Exception as error:
        write_json(
            out / "failure.json",
            dict(
                startedUTC=start,
                completedUTC=now(),
                nativeStartedUTC=started_native,
                message=str(error),
                argv=argv if "argv" in locals() else None,
                cwd=str(Path.cwd()),
                exit=child.returncode if child else None,
            ),
        )
        print("Broadcast monitor evidence:", out, file=sys.stderr)
        raise


def reader_journey(args, out):
    started = now()
    profile = args.profile or "reference-light"
    env = native_environment(profile)
    # Keep the reader itself in the installed environment; only Roots adopts
    # the GNU-style message, date, and numeric categories under test.
    env["ROOTS_GUI_READER_LOCALE"] = "de_DE.UTF-8"
    with tempfile.TemporaryDirectory(
        prefix="broadcast-reader-", dir=validated_scratch()
    ) as temporary:
        source = Path(temporary) / "public"
        source.mkdir(mode=0o700)
        store = source / "metadata.sqlite"
        with OwnedRegtest(args.daemon, out / "node", monitor_fixture=True) as node:
            node.ready()
            known = prepare(node, source, args.openssl, monitor=True)
            proxy = BroadcastProxy(node, known, store, monitor=True)
            argv = [
                sys.executable,
                "-B",
                str(TESTS / "platform/owned_display.py"),
                "--output",
                str(out / "display"),
                "--timeout",
                "213",
                "--",
                "/usr/bin/python3",
                "-B",
                str(TESTS / "platform/a11y_session.py"),
                "--binary",
                str(args.native),
                "--fonts",
                str(args.fonts),
                "--output",
                str(out / "journey"),
                "--orca",
                "--orca-output",
                "braille-monitor",
                "--send-fixture",
                str(source),
                str(proxy.port),
                str(node.data / "regtest/.cookie"),
            ]
            try:
                begin = now()
                with (out / "native.log").open("w") as log:
                    child = subprocess.run(
                        argv, stdout=log, stderr=subprocess.STDOUT, timeout=227, env=env
                    )
                end = now()
            finally:
                proxy.close()
                write_json(out / "transcript.json", proxy.events)
                write_json(out / "fixtures.json", known)
                copytree(
                    source,
                    out / "public-fixtures",
                    ignore=lambda _, names: [
                        name for name in names if name.startswith("metadata.sqlite")
                    ],
                )
            write_json(
                out / "command.json",
                dict(
                    argv=argv,
                    cwd=str(Path.cwd()),
                    exit=child.returncode,
                    startedUTC=begin,
                    completedUTC=end,
                ),
            )
            if child.returncode:
                raise RuntimeError(
                    f"native Orca journey exited {child.returncode}; see {out}"
                )
            accepted = known["accepted"]
            (received,) = [event for event in proxy.events if event["method"] == SUBMIT]
            assert (
                received["case"] == "accepted"
                and received["attemptBeforeForwarding"]["serial"] == 1
            )
            assert received["attemptBeforeForwarding"]["pending"]
            assert not any(event.get("blocked") for event in proxy.events)
            with closing(sqlite3.connect(store.as_uri() + "?mode=ro", uri=True)) as db:
                rows = db.execute(
                    "SELECT genesis,txid,wire,serial,outcome FROM broadcast_details"
                ).fetchall()
            assert rows == [
                (GENESIS, accepted["facts"]["txid"], accepted["wire"], 1, 3)
            ]
            write_json(out / "journal.json", rows)
    process = json.loads((out / "node/process.json").read_text())
    display = json.loads((out / "display/processes.json").read_text())
    reader = json.loads((out / "journey/result.json").read_text())
    assert reader["readerOutputVerified"] and reader["selected"] == reader["passed"]
    assert (
        process["settled"]
        and process["datadirRemoved"]
        and process["authMaterialAbsentFromRetainedLogs"]
    )
    assert display["child"]["settled"] and all(
        row["settled"] and not row.get("remainingLiveGroupMembers")
        for row in display["processes"]
    )
    assert not proxy.thread.is_alive() and not Path(temporary).exists()
    reader_processes = json.loads((out / "journey/processes.json").read_text())
    assert all(
        row["settled"] and not row["remainingLiveGroupMembers"]
        for row in reader_processes
    )
    native = out / "journey/fixture"
    captures = {
        view: capture_for_profile(native, "reader-" + view, profile)
        for view in ("send", "review", "signed", "final", "monitor")
    }
    write_json(
        out / "report.json",
        dict(
            startedUTC=started,
            completedUTC=now(),
            command="command.json",
            binary=binary_identity(args.native),
            profile=profile,
            locale=env["ROOTS_GUI_READER_LOCALE"],
            reader=reader,
            captures=captures,
            native=json.loads((native / "native-result.json").read_text()),
            roots=process["backend"],
            scratchRemoved=True,
            ownedProcessesSettled=True,
            selected=1,
            executed=1,
            passed=1,
            failed=0,
            skipped=0,
        ),
    )
    print("Broadcast reader evidence:", out)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("native", "openssl", "fonts", "daemon", "output"):
        parser.add_argument("--" + key, required=True, type=Path)
    journey = parser.add_mutually_exclusive_group()
    journey.add_argument("--recovery", action="store_true")
    journey.add_argument("--monitor", action="store_true")
    journey.add_argument("--reader", action="store_true")
    parser.add_argument(
        "--profile",
        choices=("reference-light", "reference-dark", "compact-light", "compact-dark"),
    )
    args = parser.parse_args()
    if args.recovery and args.profile:
        parser.error("--profile applies to the broadcast and monitor journeys")
    out = Path(tempfile.mkdtemp(prefix="attempt-", dir=evidence_directory(args.output)))
    if args.recovery:
        return recovery_journey(args, out)
    if args.monitor:
        return monitor_journey(args, out)
    if args.reader:
        return reader_journey(args, out)
    start = now()
    outcome = None
    try:
        with tempfile.TemporaryDirectory(
            prefix="broadcast-public-", dir=validated_scratch()
        ) as temporary:
            work = Path(temporary)
            source = work / "public"
            source.mkdir(mode=0o700)
            store = source / "metadata.sqlite"
            with OwnedRegtest(args.daemon, out / "node") as node:
                node.ready()
                known = prepare(node, source, args.openssl)
                proxy = BroadcastProxy(node, known, store)
                argv = [
                    sys.executable,
                    "-B",
                    str(TESTS / "platform/owned_display.py"),
                    "--output",
                    str(out / "display"),
                    "--timeout",
                    "40",
                    "--",
                    str(args.native),
                    str(out),
                    str(args.fonts),
                    str(source),
                    str(proxy.port),
                    str(node.data / "regtest/.cookie"),
                ]
                try:
                    outcome = subprocess.run(
                        argv,
                        text=True,
                        capture_output=True,
                        timeout=50,
                        env=native_environment(args.profile),
                    )
                    (out / "native.log").write_text(outcome.stdout + outcome.stderr)
                finally:
                    proxy.close()
                    write_json(out / "transcript.json", proxy.events)
                    write_json(out / "fixtures.json", known)
                    copytree(
                        source,
                        out / "public-fixtures",
                        ignore=lambda _, names: [
                            name for name in names if name.startswith("metadata.sqlite")
                        ],
                    )
                if outcome.returncode:
                    raise RuntimeError(f"native process exited {outcome.returncode}")
                actions = [
                    event
                    for event in proxy.events
                    if event["method"] in (POLICY, SUBMIT)
                ]
                assert all(not event.get("blocked") for event in proxy.events)
                writes = [event for event in actions if event["method"] == SUBMIT]
                assert [entry["case"] for entry in writes] == ["accepted", "lost-reply"]
                assert (
                    not writes[0].get("responseDroppedAfterUpstream")
                    and writes[1]["responseDroppedAfterUpstream"]
                )
                rejections = [
                    json.loads(event["responseBody"])["result"][0]
                    for event in actions
                    if event["case"] == "locktime"
                ]
                assert (
                    len(rejections) == 1
                    and rejections[0]["allowed"] is False
                    and rejections[0]["reject-reason"] == "non-final"
                )
                for name in ("accepted", "lost-reply"):
                    policy = [
                        event
                        for event in actions
                        if event["case"] == name
                        and event["method"] == POLICY
                        and "responseBody" in event
                    ]
                    assert len(policy) >= 2 and all(
                        json.loads(event["responseBody"])["result"][0]["allowed"]
                        for event in policy
                    )
                assert all(
                    json.loads(event["responseBody"])["result"]
                    == known[event["case"]]["facts"]["txid"]
                    for event in writes
                )
                mempool = node.call("getrawmempool")
                assert set(mempool) == {
                    known[name]["facts"]["txid"] for name in ("accepted", "lost-reply")
                }
                write_json(
                    out / "mempool-observation.json",
                    dict(observedUTC=now(), txids=mempool, confirmationClaimed=False),
                )
                with closing(
                    sqlite3.connect(store.as_uri() + "?mode=ro", uri=True)
                ) as db:
                    records = db.execute(
                        "SELECT genesis,txid,wtxid,wire_digest,observed_ms FROM broadcast_attempts ORDER BY txid"
                    ).fetchall()
                assert len(records) == 2
                write_json(out / "durable-attempts.json", records)
        process = json.loads((out / "node/process.json").read_text())
        display = json.loads((out / "display/processes.json").read_text())
        assert (
            process["settled"]
            and process["datadirRemoved"]
            and process["authMaterialAbsentFromRetainedLogs"]
        )
        assert display["child"]["settled"] and all(
            r["settled"] and not r.get("remainingLiveGroupMembers")
            for r in display["processes"]
        )
        metadata = dict(
            startedUTC=start,
            completedUTC=now(),
            argv=argv,
            cwd=str(Path.cwd()),
            exit=outcome.returncode,
            scratchRemoved=not Path(temporary).exists(),
            fixturesPrivateKeysRetained=False,
            proxySettled=not proxy.thread.is_alive(),
            binary=binary_identity(args.native),
            native=json.loads((out / "native-result.json").read_text()),
            roots=process["backend"],
            captures={
                name: capture_for_profile(out, name, args.profile)
                for name in ("broadcast-ready", "broadcast-rejected")
            },
            profile=args.profile,
            selected=1,
            executed=1,
            passed=1,
            failed=0,
            skipped=0,
        )
        write_json(out / "report.json", metadata)
        print("Broadcast evidence:", out)
        return 0
    except Exception as error:
        write_json(
            out / "failure.json",
            dict(
                startedUTC=start,
                completedUTC=now(),
                message=str(error),
                exit=outcome.returncode if outcome else None,
                argv=argv if "argv" in locals() else None,
                cwd=str(Path.cwd()),
            ),
        )
        print("Broadcast evidence:", out, file=sys.stderr)
        raise


if __name__ == "__main__":
    raise SystemExit(main())
