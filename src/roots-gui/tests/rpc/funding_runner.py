# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Exercise unsigned production funding against an exact owned Roots fixture."""

import argparse
import base64
import csv
from contextlib import closing
from decimal import Decimal
import hashlib
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import threading

sys.dont_write_bytecode = True
TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))
sys.path.insert(0, str(TESTS / "platform"))
from integration.owned_regtest import OwnedRegtest
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)
from capture_manifest import encode_capture


READS = frozenset(
    {
        "getnetworkinfo",
        "getblockchaininfo",
        "getblockhash",
        "getbestblockhash",
        "getwalletinfo",
        "getopenrpcinfo",
        "help",
        "listdescriptors",
        "listlockunspent",
        "gettxout",
        "getbalances",
        "listsinceblock",
        "gettransaction",
        "listunspent",
        "estimatesmartfee",
        "getmempoolinfo",
        "getrawmempool",
        "getblockstats",
        "getblockheader",
    }
)
FEE_READS = frozenset(
    {
        "estimatesmartfee",
        "getmempoolinfo",
        "getrawmempool",
        "getblockstats",
        "getblockheader",
    }
)
FUND = "walletcreatefundedpsbt"
SIGN = "walletprocesspsbt"
UNLOCK = "walletpassphrase"
FIXTURE_PASSPHRASE = "signnativefixture"


def maps(source):
    offset = 5
    assert source[:offset] == b"psbt\xff"
    records = []

    def size():
        nonlocal offset
        value = source[offset]
        offset += 1
        if value >= 253:
            width = 2 ** (value - 252)
            value = int.from_bytes(source[offset : offset + width], "little")
            offset += width
        return value

    while offset < len(source):
        row = {}
        while key_size := size():
            key = source[offset : offset + key_size]
            offset += key_size
            value_size = size()
            row[key] = source[offset : offset + value_size]
            offset += value_size
        records.append(row)
    return records


def encode_maps(records):
    def size(n):
        return bytes([n]) if n < 253 else b"\xfd" + n.to_bytes(2, "little")

    wire = b"psbt\xff" + b"".join(
        b"".join(size(len(k)) + k + size(len(v)) + v for k, v in row.items()) + b"\x00"
        for row in records
    )
    return base64.b64encode(wire).decode()


class FundingProxy:
    def __init__(self, node, capture_flow=False, signing=False):
        self.events = []
        self.mutation = None
        proxy = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def do_POST(self):
                body = self.rfile.read(int(self.headers["Content-Length"]))
                request = json.loads(body, parse_float=Decimal)
                method = request["method"]
                params = request["params"]
                event = dict(method=method, path=self.path)
                if method in FEE_READS:
                    event["params"] = json.loads(body)["params"]
                proxy.events.append(event)
                allowed = READS | {FUND, SIGN, UNLOCK} if signing else READS | {FUND}
                if method not in allowed or (
                    method == "listdescriptors" and params != {"private": False}
                ):
                    event["blocked"] = True
                    self.send_error(403)
                    return
                if method == UNLOCK:
                    event["timeout"] = params.get("timeout")
                    if (
                        set(params) != {"passphrase", "timeout"}
                        or event["timeout"] != 30
                    ):
                        event["blocked"] = True
                        self.send_error(403)
                        return
                if method == SIGN:
                    event["policy"] = {
                        k: params.get(k) for k in ("sign", "finalize", "bip32derivs")
                    }
                    event["psbt"] = params.get("psbt")
                    if (
                        params.get("finalize") is not False
                        or type(params.get("sign")) is not bool
                        or not event["psbt"]
                    ):
                        event["blocked"] = True
                        self.send_error(403)
                        return
                if method == FUND:
                    options = params["options"]
                    event["options"] = json.loads(body)["params"]
                    if (
                        not options.get("changeAddress")
                        or options.get("lockUnspents") is not False
                        or params["bip32derivs"] is not False
                    ):
                        event["blocked"] = True
                        self.send_error(403)
                        return
                with closing(
                    http.client.HTTPConnection("127.0.0.1", node.port, timeout=12)
                ) as upstream:
                    upstream.request(
                        "POST",
                        self.path,
                        body,
                        {
                            "Authorization": self.headers.get("Authorization", ""),
                            "Content-Type": "application/json",
                        },
                    )
                    response = upstream.getresponse()
                    raw = response.read()
                    event["status"] = response.status
                    if method in FEE_READS:
                        event["responseBody"] = raw.decode("utf-8")
                    if capture_flow and method == FUND:
                        event["requestBody"] = body.decode("utf-8")
                        event["responseBody"] = raw.decode("utf-8")
                    if method == SIGN:
                        result = json.loads(raw).get("result")
                        if result is not None:
                            event["returnedPsbt"] = result["psbt"]
                            event["daemonReturnedHex"] = "hex" in result
                    if method == "listlockunspent" and proxy.mutation == "lock":
                        value = json.loads(raw)
                        value["result"].append(proxy.new_lock)
                        raw = json.dumps(value).encode()
                    if method == FUND and proxy.mutation and proxy.mutation != "lock":
                        value = json.loads(raw)
                        result = value.get("result")
                        assert result and result.get("psbt"), value.get("error")
                        records = maps(base64.b64decode(result["psbt"]))
                        if proxy.mutation == "input":
                            records[1][b"\x0e"] = b"\x22" * 32
                        elif proxy.mutation == "recipient":
                            output = 1 + records[0][b"\x04"][0]
                            script = records[output][b"\x04"]
                            records[output][b"\x04"] = script[:-1] + bytes(
                                [script[-1] ^ 1]
                            )
                        elif proxy.mutation == "change":
                            output = 1 + records[0][b"\x04"][0] + result["changepos"]
                            script = records[output][b"\x04"]
                            records[output][b"\x04"] = script[:-1] + bytes(
                                [script[-1] ^ 1]
                            )
                        elif proxy.mutation == "fee":
                            result["fee"] = str(
                                Decimal(str(result["fee"])) + Decimal("0.00000001")
                            )
                        elif proxy.mutation == "signature":
                            records[1][b"\x08"] = b"\x00"
                        else:
                            raise AssertionError(proxy.mutation)
                        result["psbt"] = encode_maps(records)
                        raw = json.dumps(value).encode()
                    self.send_response(response.status)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(raw)))
                    self.end_headers()
                    self.wfile.write(raw)

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()

    @property
    def port(self):
        return self.server.server_address[1]

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=4)
        assert not self.thread.is_alive()


def sat(value):
    sats = Decimal(value) * 100000000
    assert sats == sats.to_integral_value()
    return int(sats)


def native_preview(args, out, node, proxy, base, destination):
    frames = out / "captures"
    native = out / "native"
    batch_files = {}
    with tempfile.TemporaryDirectory(
        prefix="funding-preview-", dir=validated_scratch()
    ) as private:
        if args.batch:
            familiar, other = destination.split(",")
            source = Path(private) / "batch-in.csv"
            with source.open("x", encoding="utf-8", newline="") as stream:
                csv.writer(stream).writerows(
                    [
                        ("Address", "Amount", "Label", "Unit"),
                        (familiar, "0.60000000", "München, 支付", "BTC"),
                        (other, "2000000", "  =1+2", "sat"),
                    ]
                )
            destination = familiar
        argv = [
            sys.executable,
            "-B",
            str(TESTS / "platform/owned_display.py"),
            "--output",
            str(native),
            "--timeout",
            "23",
            "--",
            str(args.native),
            str(frames),
            str(args.fonts),
            "--sign-live"
            if args.sign
            else "--edit-live"
            if args.edit
            else "--review-live"
            if args.review
            else "--batch-live"
            if args.batch
            else "--flow-live"
            if args.flow
            else "--fees-live"
            if args.fees
            else "--create-live"
            if args.create
            else "--funding-live",
            str(proxy.port),
            str(node.data / "regtest/.cookie"),
            base["genesis"],
            base["wallet"],
            str(Path(private) / "metadata.sqlite"),
            destination,
        ]
        completed = subprocess.run(argv, capture_output=True, text=True, timeout=35)
        (out / "native-run.log").write_text(completed.stdout + completed.stderr)
        assert completed.returncode == 0, (native / "command.log").read_text()
        if args.batch:
            exported = Path(private) / "batch-out.csv"
            assert exported.stat().st_mode & 0o777 == 0o600
            with exported.open(encoding="utf-8", newline="") as stream:
                assert list(csv.reader(stream)) == [
                    ["Address", "Amount", "Label (Roots text v1)", "Unit"],
                    [familiar, "0.60000000", "München, 支付", "BTC"],
                    [other, "2000000", "'  =1+2", "sat"],
                ]
            retained = out / "batch-file-evidence"
            retained.mkdir()
            for path in (source, exported):
                saved = retained / path.name
                contents = path.read_bytes()
                saved.write_bytes(contents)
                batch_files[path.name] = dict(
                    path=str(saved),
                    bytes=len(contents),
                    sha256=hashlib.sha256(contents).hexdigest(),
                )
    result = json.loads((frames / "result.json").read_text())
    process = json.loads((native / "processes.json").read_text())
    assert (
        result["connected"]
        and result["teardown"]
        and result["externalHostCommands"] == 0
    )
    assert process["child"]["settled"] and len(process["processes"]) == 2
    assert all(
        p["settled"] and not p.get("remainingLiveGroupMembers")
        for p in process["processes"]
    )
    names = (
        ("send-created", "send-clear-confirm") if args.create else ("funding-preview",)
    )
    if args.fees:
        names = ("fees-target", "fees-mempool", "fees-recent")
    if args.flow:
        names = ("transaction-flow-small", "transaction-flow-large", "flow-hidden")
    if args.batch:
        names = ("send-to-many", "transaction-analysis")
    if args.review:
        names = ("transaction-review", "advanced-sighash")
    if args.edit:
        names = ("review-invalidated",)
    if args.sign:
        names = ("daemon-unlock",)
    assert result["frames"] == len(names)
    captures = {name: encode_capture(frames / (name + ".ppm")) for name in names}
    assert all((frames / (name + ".semantics.txt")).is_file() for name in names)
    assert not Path(private).exists()
    record = dict(
        argv=argv,
        exit=completed.returncode,
        checks=result["checks"],
        displaySettled=True,
        binary=binary_identity(args.native),
        metadataRemoved=True,
    )
    if (
        args.create
        or args.fees
        or args.flow
        or args.batch
        or args.review
        or args.edit
        or args.sign
    ):
        record["captures"] = captures
    else:
        record["capture"] = captures["funding-preview"]
    if args.batch:
        record.update(
            files=batch_files, ownedLocalFilesRemoved=True, physicalOsChooser=False
        )
    if args.review or args.edit or args.sign:
        record["approvalProvenance"] = json.loads(
            (frames / "approval-provenance.json").read_bytes()
        )
    if args.edit:
        record["editProvenance"] = json.loads(
            (frames / "edit-provenance.json").read_bytes()
        )
    if args.sign:
        record["signingProvenance"] = json.loads(
            (frames / "signing-provenance.json").read_bytes()
        )
    return record


def main():
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("daemon", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--binary", type=Path)
    modes.add_argument("--native", type=Path)
    journeys = parser.add_mutually_exclusive_group()
    journeys.add_argument("--create", action="store_true")
    journeys.add_argument("--fees", action="store_true")
    journeys.add_argument("--flow", action="store_true")
    journeys.add_argument("--batch", action="store_true")
    journeys.add_argument("--review", action="store_true")
    journeys.add_argument("--edit", action="store_true")
    journeys.add_argument("--sign", action="store_true")
    parser.add_argument("--fonts", type=Path)
    args = parser.parse_args()
    if args.native and not args.fonts:
        parser.error("native evidence requires bundled fonts")
    if (
        args.create
        or args.fees
        or args.flow
        or args.batch
        or args.review
        or args.edit
        or args.sign
    ) and not args.native:
        parser.error("Selected journey requires the native executable")
    out = Path(tempfile.mkdtemp(prefix="attempt-", dir=evidence_directory(args.output)))
    selected = (
        [
            (
                "connected-approve-unlock-sign"
                if args.sign
                else "connected-edit-invalidates-approval"
                if args.edit
                else "connected-review-then-approve"
                if args.review
                else "connected-send-to-many"
                if args.batch
                else "connected-transaction-flow"
                if args.flow
                else "connected-fee-observations"
                if args.fees
                else "connected-create-clear-recovery"
                if args.create
                else "connected-preview"
            )
        ]
        if args.native
        else [
            "pinned",
            "automatic",
            "restricted",
            "mixed-script",
            "maximum",
            "dust",
            "insufficient",
            "ancestor",
            "unexpected-input",
            "unexpected-recipient",
            "unexpected-change",
            "mismatched-fee",
            "final-witness",
            "new-lock",
        ]
    )
    record = dict(
        selected=len(selected),
        executed=0,
        passed=0,
        failed=0,
        skipped=0,
        results=[],
        binary=binary_identity(args.native or args.binary),
        daemon=binary_identity(args.daemon),
    )
    try:
        with OwnedRegtest(args.daemon, out / "daemon", public_descriptors=True) as node:
            node.ready()
            wallet = "funding & exact wallet"
            bank = "fixture controller"
            for name in (wallet, bank):
                options = {"wallet_name": name}
                if args.sign and name == wallet:
                    options["passphrase"] = FIXTURE_PASSPHRASE
                node.call("createwallet", options)
            if args.sign:
                node.secrets.append(FIXTURE_PASSPHRASE)
            miner = node.call("getnewaddress", wallet=bank)
            node.call("generatetoaddress", [101, miner])
            addresses = [
                node.call("getnewaddress", {"address_type": script}, wallet=wallet)
                for script in ("bech32", "legacy", "bech32")
            ]

            def fund(address, amount):
                txid = node.call(
                    "sendtoaddress",
                    {"address": address, "amount": amount, "fee_rate": 2},
                    wallet=bank,
                )
                raw = node.call("gettransaction", [txid], wallet=bank)["hex"]
                assert node.call("sendrawtransaction", [raw]) == txid

            for address, amount in zip(addresses[:2], ("0.30000000", "0.50000000")):
                fund(address, amount)
            node.call("generatetoaddress", [2, miner])
            fund(addresses[2], "0.07000000")
            coins = node.call("listunspent", {"minconf": 0}, wallet=wallet)
            exact = []
            for address in addresses:
                (coin,) = (c for c in coins if c["address"] == address)
                coin = dict(
                    coin,
                    amount=str(coin["amount"]),
                    previous=node.call("gettransaction", [coin["txid"]], wallet=wallet)[
                        "hex"
                    ],
                )
                exact.append(coin)
            native, legacy, unconfirmed = exact

            def point(coin):
                return {"txid": coin["txid"], "vout": coin["vout"]}

            if not args.fees:
                assert node.call(
                    "lockunspent",
                    {"unlock": False, "transactions": [point(unconfirmed)]},
                    wallet=wallet,
                )
            destinations = [
                node.call("getnewaddress", wallet=bank)
                for _ in range(8 if args.flow else 2)
            ]
            policy_before = node.call(
                "listdescriptors", {"private": False}, wallet=wallet
            )
            locks_before = node.call("listlockunspent", wallet=wallet)
            balance_before = node.call("getbalances", wallet=wallet)
            base = dict(
                genesis=node.call("getblockhash", [0]),
                tip=node.call("getbestblockhash"),
                wallet=wallet,
                rate=2125,
            )
            proxy = FundingProxy(
                node,
                args.flow or args.batch or args.review or args.edit or args.sign,
                args.sign,
            )
            proxy.new_lock = point(native)
            try:
                if args.native:
                    record["executed"] = 1
                    target = (
                        ",".join([addresses[0], destinations[0]])
                        if args.batch
                        else ",".join(destinations)
                        if args.flow
                        else f"{unconfirmed['txid']}:{unconfirmed['vout']}"
                        if args.fees
                        else destinations[0]
                    )
                    record["native"] = native_preview(
                        args, out, node, proxy, base, target
                    )
                    funded = [
                        event for event in proxy.events if event["method"] == FUND
                    ]
                    if args.fees:
                        assert not funded and all(
                            event["method"] in READS for event in proxy.events
                        )
                        fee_calls = [
                            event
                            for event in proxy.events
                            if event["method"] in FEE_READS
                        ]
                        assert {event["method"] for event in fee_calls} == FEE_READS
                        assert all(
                            event["path"] == "/" and event["status"] == 200
                            for event in fee_calls
                        )
                        (estimate,) = (
                            event
                            for event in fee_calls
                            if event["method"] == "estimatesmartfee"
                        )
                        assert estimate["params"] == [3]
                        (mempool,) = (
                            event
                            for event in fee_calls
                            if event["method"] == "getrawmempool"
                        )
                        assert mempool["params"] == [True]
                        stats = [
                            event
                            for event in fee_calls
                            if event["method"] == "getblockstats"
                        ]
                        assert len(stats) == 6 and stats[0]["params"][0] == base["tip"]
                        assert (
                            len(
                                [
                                    event
                                    for event in fee_calls
                                    if event["method"] == "getblockheader"
                                ]
                            )
                            == 5
                        )
                        record["feeCalls"] = len(fee_calls)
                    elif args.batch:
                        assert len(funded) == 1 and funded[0]["status"] == 200
                        construction = json.loads(
                            funded[0]["requestBody"], parse_float=Decimal
                        )["params"]
                        assert construction["options"]["lockUnspents"] is False
                        assert [list(row) for row in construction["outputs"]] == [
                            [addresses[0]],
                            [destinations[0]],
                        ]
                        assert [
                            sat(next(iter(row.values())))
                            for row in construction["outputs"]
                        ] == [60000000, 2000000]
                        assert all(
                            label not in funded[0]["requestBody"]
                            for label in ("München", "1+2")
                        )
                        assert all(
                            event["method"] in READS
                            for event in proxy.events
                            if event is not funded[0]
                        )
                        record["batch"] = dict(
                            constructions=1,
                            exactRecipients=2,
                            fixedSatoshis=62000000,
                            localLabels=True,
                        )
                    elif args.flow:
                        assert len(funded) == 2 and all(
                            event["status"] == 200 for event in funded
                        )
                        first, second = [event["options"] for event in funded]
                        assert (
                            len(first["outputs"]) == 1 and len(second["outputs"]) == 8
                        )
                        assert [
                            item for row in second["outputs"] for item in row
                        ] == destinations
                        change1, change2 = [
                            event["options"]["changeAddress"]
                            for event in (first, second)
                        ]
                        assert change1 != change2
                        assert all(
                            event["options"]["lockUnspents"] is False
                            for event in (first, second)
                        )
                        record["flow"] = dict(
                            constructions=2,
                            explicitChange=change2,
                            firstChange=change1,
                            fixtureRecipients=destinations,
                            nativeClipboardOwnership=True,
                        )
                    else:
                        assert len(funded) == 1 and funded[0]["status"] == 200
                        assert "Local invoice" not in json.dumps(funded[0])
                    if args.create or args.review or args.edit:
                        after_construction = proxy.events[
                            proxy.events.index(funded[0]) + 1 :
                        ]
                        assert (
                            sum(
                                call["method"] == "gettxout"
                                for call in after_construction
                            )
                            >= 2
                        )
                        assert all(
                            call["method"] in READS for call in after_construction
                        )
                    if args.review or args.edit or args.sign:
                        approval = record["native"]["approvalProvenance"]
                        unsigned = json.loads(
                            funded[0]["responseBody"], parse_float=Decimal
                        )["result"]
                        assert (
                            approval["wallet"] == base["wallet"]
                            and approval["genesis"] == base["genesis"]
                        )
                        assert (
                            approval["originalSourceSha256"]
                            == hashlib.sha256(
                                unsigned["psbt"].encode("ascii")
                            ).hexdigest()
                        )
                        assert (
                            approval["feeSats"] == sat(unsigned["fee"])
                            and approval["recipientSats"] == 12345678
                        )
                        assert (
                            approval["sourceDocument"] != approval["reviewDocument"]
                            and approval["intendedChangeProved"]
                        )
                    if args.sign:
                        enrich, unlock, sign = [
                            event
                            for event in proxy.events
                            if event["method"] in {SIGN, UNLOCK}
                        ]
                        assert [
                            event["method"] for event in (enrich, unlock, sign)
                        ] == [SIGN, UNLOCK, SIGN]
                        assert all(
                            event["path"] == funded[0]["path"]
                            and event["status"] == 200
                            for event in (enrich, unlock, sign)
                        )
                        assert enrich["policy"] == dict(
                            sign=False, finalize=False, bip32derivs=True
                        )
                        assert (
                            sign["policy"]
                            == dict(sign=True, finalize=False, bip32derivs=False)
                            and unlock["timeout"] == 30
                        )
                        exact = record["native"]["signingProvenance"]
                        assert (
                            enrich["psbt"]
                            == sign["psbt"]
                            == exact["source"]
                            == unsigned["psbt"]
                        )
                        assert exact["commitment"] == approval["commitment"]
                        input_map = maps(base64.b64decode(exact["signedPsbt"]))[1]
                        sign_map = maps(base64.b64decode(sign["returnedPsbt"]))[1]
                        assert any(
                            k[0] == 2 and sign_map.get(k) == v
                            for k, v in input_map.items()
                        )
                        assert not {b"\x07", b"\x08"} & input_map.keys()
                        assert (
                            node.call("getwalletinfo", wallet=wallet)["unlocked_until"]
                            > 0
                        )
                        assert FIXTURE_PASSPHRASE not in json.dumps(proxy.events)
                        for path in (out / "captures").glob("*.txt"):
                            assert FIXTURE_PASSPHRASE not in path.read_text()
                    if args.edit:
                        edited = record["native"]["editProvenance"]
                        assert (
                            edited["originalSource"] == unsigned["psbt"]
                            and edited["workingSource"] != unsigned["psbt"]
                        )
                        assert (
                            edited["oldCommitment"] == approval["commitment"]
                            and edited["newCommitment"] != approval["commitment"]
                        )
                        assert (
                            edited["locktime"] == 900000 and edited["changeIndex"] == 0
                        )
                    record["results"].append(dict(id=selected[0], calls=proxy.events))
                    record["passed"] = 1
                scenarios = {
                    "pinned": (2, [native], ["0.10000000"], True, None),
                    "automatic": (0, [native, legacy], ["0.60000000"], True, None),
                    "restricted": (1, [native], ["0.10000000"], True, None),
                    "mixed-script": (
                        2,
                        [native, legacy],
                        ["0.12345678", "0.02010000"],
                        True,
                        None,
                    ),
                    "maximum": (2, [native, legacy], ["0.12345678", None], True, None),
                    "dust": (2, [native], ["0.00000001"], False, None),
                    "insufficient": (2, [native], ["1.00000000"], False, None),
                    "ancestor": (2, [unconfirmed], ["0.01000000"], False, None),
                    "unexpected-input": (2, [native], ["0.10000000"], False, "input"),
                    "unexpected-recipient": (
                        2,
                        [native],
                        ["0.10000000"],
                        False,
                        "recipient",
                    ),
                    "unexpected-change": (2, [native], ["0.10000000"], False, "change"),
                    "mismatched-fee": (2, [native], ["0.10000000"], False, "fee"),
                    "final-witness": (2, [native], ["0.10000000"], False, "signature"),
                    "new-lock": (2, [native], ["0.10000000"], False, "lock"),
                }
                for name in () if args.native else selected:
                    mode, inputs, amounts, succeeds, mutation = scenarios[name]
                    offset = len(proxy.events)
                    proxy.mutation = mutation
                    recipients = [
                        dict(address=address, amount=amount)
                        for address, amount in zip(destinations, amounts)
                    ]
                    payload = dict(base, mode=mode, coins=inputs, recipients=recipients)
                    if name == "pinned":
                        payload["check_stale"] = True
                    fixture = out / (name + ".json")
                    write_json(fixture, payload)
                    argv = [
                        str(args.binary),
                        str(fixture),
                        str(proxy.port),
                        str(node.data / "regtest/.cookie"),
                    ]
                    run = subprocess.run(
                        argv, capture_output=True, text=True, timeout=22
                    )
                    record["executed"] += 1
                    entry = dict(
                        id=name,
                        argv=argv,
                        exit=run.returncode,
                        log=run.stderr,
                        calls=proxy.events[offset:],
                    )
                    record["results"].append(entry)
                    assert not run.returncode, run.stderr
                    result = json.loads(run.stdout)
                    entry["result"] = result
                    assert result["ok"] == succeeds, (name, result)
                    funded = [
                        event for event in entry["calls"] if event["method"] == FUND
                    ]
                    assert len(funded) == int(
                        name not in {"insufficient", "ancestor"}
                    ), (name, funded)
                    if funded:
                        request = funded[0]["options"]
                        options = request["options"]
                        assert options["add_inputs"] == (mode == 0)
                        assert options["fee_rate"] == 2.125 and options["minconf"] == 1
                        assert (
                            options["lockUnspents"] is False
                            and options["include_unsafe"] is False
                        )
                        assert options["subtractFeeFromOutputs"] == (
                            [1] if name == "maximum" else []
                        )
                        assert [i["txid"] for i in request["inputs"]] == (
                            [] if mode == 0 else [i["txid"] for i in inputs]
                        )
                    if succeeds:
                        parsed = node.call("decodepsbt", [result["psbt"]])
                        skeleton = parsed.get("tx")
                        inputs_seen = skeleton["vin"] if skeleton else parsed["inputs"]
                        outputs_seen = (
                            skeleton["vout"] if skeleton else parsed["outputs"]
                        )
                        assert len(inputs_seen) == result["inputs"]
                        keys = {
                            (i["txid"], i["vout"]): sat(i["amount"]) for i in inputs
                        }
                        if skeleton:
                            assert (
                                sum(keys[i["txid"], i["vout"]] for i in inputs_seen)
                                == result["total"]
                            )
                            assert (
                                sum(sat(o["value"]) for o in outputs_seen)
                                + result["fee"]
                                == result["total"]
                            )
                        else:
                            assert (
                                len(outputs_seen) == len(recipients) + result["change"]
                            )
                            assert sat(parsed["fee"]) == result["fee"]
                        if name == "maximum":
                            assert (
                                not result["change"]
                                and result["max"]
                                == result["total"] - sat(amounts[0]) - result["fee"]
                            )
                    if mutation:
                        assert mutation in result["error"]
                    record["passed"] += 1
                assert not any(event.get("blocked") for event in proxy.events)
                unchanged_policy = (
                    node.call("listdescriptors", {"private": False}, wallet=wallet)
                    == policy_before
                )
                if not args.sign:
                    assert unchanged_policy
                assert node.call("listlockunspent", wallet=wallet) == locks_before
                assert node.call("getbalances", wallet=wallet) == balance_before
                record.update(
                    keypoolUnchanged=unchanged_policy,
                    daemonLocksUnchanged=True,
                    walletBalancesUnchanged=True,
                    applicationSignSendCalls=int(args.sign),
                    applicationSigningCalls=int(args.sign),
                    applicationBroadcastCalls=0,
                )
            finally:
                proxy.close()
                write_json(out / "transcript.json", proxy.events)
                record["proxySettled"] = True
        record["daemonSettled"] = (
            node.record["settled"] and node.record["datadirRemoved"]
        )
        assert record["daemonSettled"]
    except Exception as error:
        record["failed"] = 1
        record["failure"] = str(error)
        raise
    finally:
        write_json(out / "result.json", record)
        print(
            json.dumps(
                {
                    k: record[k]
                    for k in ("selected", "executed", "passed", "failed", "skipped")
                }
            )
        )
        print(f"Funding evidence: {out}")
    return int(record["passed"] != len(selected))


if __name__ == "__main__":
    raise SystemExit(main())
