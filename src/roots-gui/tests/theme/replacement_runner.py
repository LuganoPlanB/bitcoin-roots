# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""One native fee increase journey on an explicitly owned Roots regtest node."""

import argparse
import base64
from contextlib import closing
from datetime import datetime, timezone
from decimal import Decimal
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

TESTS = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(TESTS), str(TESTS / "platform")]
from capture_manifest import encode_capture
from integration.owned_regtest import OwnedRegtest
from rpc.funding_runner import READS
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)

WALLET = "fee-wallet"
MINER = "fee-fixture-miner"
BUMP = "psbtbumpfee"
RATE = Decimal("12.345")


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

    call("createwallet", {"wallet_name": MINER})
    call("createwallet", {"wallet_name": WALLET})
    miner = call("getnewaddress", ["", "bech32"], MINER)
    call("generatetoaddress", [101, miner])
    roles = ("eligible", "plain", "replaced", "poor", "race")
    addresses = {role: call("getnewaddress", ["", "bech32"], WALLET) for role in roles}
    funding = call(
        "sendmany",
        {"amounts": {address: 0.002 for address in addresses.values()}, "fee_rate": 2},
        MINER,
    )
    funding_wire = call("gettransaction", [funding], MINER)["hex"]
    assert call("sendrawtransaction", [funding_wire, 0.001, 0]) == funding
    call("generatetoaddress", [1, miner])
    coins = {coin["address"]: coin for coin in call("listunspent", wallet=WALLET)}
    fixtures = {}
    for role in roles:
        coin = coins[addresses[role]]
        destination = call("getnewaddress", ["", "bech32"], MINER)
        change = call("getrawchangeaddress", ["bech32"], WALLET)
        point = {key: coin[key] for key in ("txid", "vout")}
        sequence = 0xFFFFFFFE if role == "plain" else 0xFFFFFFFD
        unsigned = call(
            "walletcreatefundedpsbt",
            {
                "inputs": [dict(point, sequence=sequence)],
                "outputs": [{destination: 0.00199750 if role == "poor" else 0.001}],
                "locktime": 0,
                "options": {
                    "add_inputs": False,
                    "changeAddress": change,
                    "changePosition": 1,
                    "fee_rate": 2,
                    "replaceable": role != "plain",
                },
                "psbt_version": 2,
            },
            WALLET,
        )
        signed = call(
            "walletprocesspsbt",
            {"psbt": unsigned["psbt"], "sign": True, "finalize": False},
            WALLET,
        )
        final = call("finalizepsbt", [signed["psbt"]])
        assert final["complete"]
        txid = call("sendrawtransaction", [final["hex"], 0.001, 0])
        decoded = call("decoderawtransaction", [final["hex"]])
        assert decoded["txid"] == txid and decoded["vin"][0]["sequence"] == sequence
        fixtures[role] = dict(
            txid=txid,
            wtxid=decoded["hash"],
            outpoint=point,
            wire=final["hex"],
            outputs=decoded["vout"],
            fee=unsigned["fee"],
        )
        (source / (role + ".txt")).write_text(txid)
    # Fixture-only wallet replacement to populate the original's actual replaced_by_txid.
    replaced = call("bumpfee", [fixtures["replaced"]["txid"], {"fee_rate": 12}], WALLET)
    assert (
        call("gettransaction", [fixtures["replaced"]["txid"]], WALLET)[
            "replaced_by_txid"
        ]
        == replaced["txid"]
    )
    replaced_wire = call("gettransaction", [replaced["txid"]], WALLET)["hex"]
    assert call("sendrawtransaction", [replaced_wire, 0.001, 0]) == replaced["txid"]
    return miner, fixtures


class ReplacementProxy:
    def __init__(self, node, source, miner, fixtures, controller):
        self.events = []
        self.stop = threading.Event()
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def do_POST(self):
                body = self.rfile.read(int(self.headers["Content-Length"]))
                request = json.loads(body, parse_float=Decimal)
                method, params = request["method"], request["params"]
                event = dict(method=method, route=self.path, receivedUTC=now())
                owner.events.append(event)
                if (
                    method not in READS | {BUMP, "getaddressinfo", "getmempoolentry"}
                    or (method == "listdescriptors" and params != {"private": False})
                    or self.path not in ("/", "/wallet/" + WALLET)
                ):
                    event["blocked"] = "unexpected method or scope"
                    self.send_error(403)
                    return
                mode = (
                    (source / "mode").read_text() if (source / "mode").exists() else ""
                )
                include = method in {
                    BUMP,
                    "getmempoolentry",
                    "gettransaction",
                    "getaddressinfo",
                    "getbestblockhash",
                    "getblockhash",
                    "listdescriptors",
                    "gettxout",
                    "getopenrpcinfo",
                }
                if include:
                    event["requestBody"] = body.decode()
                if method == BUMP:
                    role = (
                        "poor"
                        if mode == "insufficient"
                        else "race"
                        if mode == "race"
                        else "eligible"
                    )
                    event["case"] = mode
                    expected = {"fee_rate": RATE, "psbt_version": 2}
                    if len(fixtures[role]["outputs"]) == 2:
                        expected["original_change_index"] = 1
                    if self.path != "/wallet/" + WALLET or params != [
                        fixtures[role]["txid"],
                        expected,
                    ]:
                        event["blocked"] = "replacement request identity or fee units"
                        self.send_error(403)
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
                    status = response.status
                if include:
                    event.update(upstreamStatus=status, responseBody=raw.decode())
                if method == BUMP and mode in {
                    "malformed",
                    "recipient",
                    "change",
                    "fee",
                }:
                    substitute = json.loads(raw)
                    assert substitute["result"] and substitute["result"]["psbt"]
                    result = substitute["result"]
                    if mode == "malformed":
                        result["psbt"] = "not a PSBT"
                    elif mode == "fee":
                        result["fee"] = 0.00012345
                    else:
                        index = 0 if mode == "recipient" else 1
                        script = bytes.fromhex(
                            fixtures["eligible"]["outputs"][index]["scriptPubKey"][
                                "hex"
                            ]
                        )
                        wire = base64.b64decode(result["psbt"])
                        assert wire.count(script) == 1
                        altered = script[:-1] + bytes([script[-1] ^ 1])
                        result["psbt"] = base64.b64encode(
                            wire.replace(script, altered)
                        ).decode()
                    raw = json.dumps(substitute).encode()
                    event["responseBodyDelivered"] = raw.decode()
                if method == BUMP and mode == "drop":
                    assert json.loads(raw).get("result")
                    event["responseDroppedAfterUpstream"] = True
                    self.close_connection = True
                    return
                if method == BUMP and mode == "cancel":
                    (source / "wallet-prepared").touch()
                    until = time.monotonic() + 7
                    while (
                        not (source / "release-reply").exists()
                        and time.monotonic() < until
                        and not owner.stop.wait(0.02)
                    ):
                        pass
                    event["heldForCancellation"] = True
                    time.sleep(0.1)
                if method == BUMP and mode == "race":
                    begin = now()
                    (block,) = node.call("generatetoaddress", [1, miner])
                    actual = node.call("getblock", [block])
                    assert fixtures["race"]["txid"] in actual["tx"] and json.loads(
                        raw
                    ).get("result")
                    controller.append(
                        dict(
                            method="generatetoaddress + getblock",
                            params=[1, miner],
                            result=actual,
                            startedUTC=begin,
                            completedUTC=now(),
                        )
                    )
                    (source / "race-mined").touch()
                    event["confirmedBeforeReply"] = block
                self.send_response(status)
                self.send_header("Content-Length", str(len(raw)))
                self.end_headers()
                try:
                    self.wfile.write(raw)
                except (BrokenPipeError, ConnectionResetError):
                    event["clientRetired"] = True

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
            prefix="fee-replacement-", dir=validated_scratch()
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
                    proxy = ReplacementProxy(node, source, miner, fixtures, controller)
                    argv = [
                        sys.executable,
                        "-B",
                        str(TESTS / "platform/owned_display.py"),
                        "--timeout",
                        "75",
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
                        argv, text=True, capture_output=True, timeout=85
                    )
                    native_completed = now()
                    (output / "native.log").write_text(child.stdout + child.stderr)
                finally:
                    if proxy:
                        proxy.close()
                        write_json(output / "transcript.json", proxy.events)
                    (output / "controller.json").write_text(
                        json.dumps(controller, indent=2, default=str) + "\n"
                    )
                    (output / "fixtures.json").write_text(
                        json.dumps(fixtures, indent=2, default=str) + "\n"
                    )
                if child.returncode:
                    raise RuntimeError(
                        "native fee increase journey exited " + str(child.returncode)
                    )
                assert not any(event.get("blocked") for event in proxy.events)
                writes = [event for event in proxy.events if event["method"] == BUMP]
                assert [event["case"] for event in writes] == [
                    "malformed",
                    "recipient",
                    "change",
                    "fee",
                    "insufficient",
                    "drop",
                    "cancel",
                    "eligible",
                    "race",
                ]
                poor = writes[4]
                refused = json.loads(poor["responseBody"])["error"]
                assert refused["code"] in {-4, -6} and (
                    "Insufficient funds" in refused["message"]
                    or "exceeds your balance" in refused["message"]
                )
                assert (
                    writes[5]["responseDroppedAfterUpstream"]
                    and writes[6]["heldForCancellation"]
                    and writes[8]["confirmedBeforeReply"]
                )
                actual = json.loads(writes[7]["responseBody"], parse_float=Decimal)[
                    "result"
                ]
                assert actual["psbt"] == (output / "returned.psbt.txt").read_text()
                original_fee, fee, old_change, new_change = map(
                    int, (output / "fee-facts.txt").read_text().split()
                )
                assert original_fee == int(
                    actual["origfee"] * 100000000
                ) and fee == int(actual["fee"] * 100000000)
                assert old_change - new_change == fee - original_fee
                assert [
                    event["method"]
                    for event in proxy.events
                    if event["method"]
                    not in READS | {"getaddressinfo", "getmempoolentry"}
                ] == [BUMP] * len(writes)
                assert fixtures["eligible"]["txid"] not in node.call("getrawmempool")
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
            for key in ("increase-fee", "replacement-draft")
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
                exactFees=[original_fee, fee],
                exactChange=[old_change, new_change],
                productMethods={BUMP: len(writes)},
                selected=1,
                executed=1,
                passed=1,
                failed=0,
                skipped=0,
            ),
        )
        print("Native fee increase evidence", output)
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
        print("Native fee increase evidence", output, file=sys.stderr)
        raise


if __name__ == "__main__":
    main()
