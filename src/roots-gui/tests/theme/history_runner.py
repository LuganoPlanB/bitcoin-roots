# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""One production history UI journey against explicit owned Roots regtest."""

import sys

sys.dont_write_bytecode = True
import argparse
import json
import subprocess
import tempfile
from decimal import Decimal
from pathlib import Path
from datetime import datetime, timezone

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "rpc"))
from owned_regtest import OwnedRegtest
from history_tests import ReadOnlyProxy, sat
from support.runtime import (
    validated_scratch,
    evidence_directory,
    binary_identity,
    write_json,
)

p = argparse.ArgumentParser(description=__doc__)
for name in ["binary", "fonts", "daemon", "output"]:
    p.add_argument("--" + name, type=Path, required=True)
p.add_argument("--states", action="store_true")
p.add_argument("--addresses", action="store_true")
p.add_argument("--coins", action="store_true")
p.add_argument("--controls", action="store_true")
a = p.parse_args()
out = evidence_directory(a.output)
start = datetime.now(timezone.utc).isoformat()
proxy = None
node = None
failure = None
run = None
argv = []
record = {}
try:
    with OwnedRegtest(
        a.daemon,
        out / "daemon",
        public_descriptors=(a.addresses or a.coins or a.controls),
    ) as node:
        chain = node.ready()
        assert (
            chain["chain"] == "regtest"
            and node.call("getnetworkinfo")["connections"] == 0
        )
        if a.states or a.addresses or a.coins or a.controls:
            for method in [
                "listsinceblock",
                "getwalletinfo",
                "getblockchaininfo",
                "listdescriptors",
                "deriveaddresses",
                "getdescriptorinfo",
                "getaddressinfo",
            ]:
                (out / (method + "-help.txt")).write_text(
                    node.call("help", dict(command=method))
                )
        if a.coins or a.controls:
            (out / "listunspent-help.txt").write_text(
                node.call("help", dict(command="listunspent"))
            )
        funder = "screen owned funder"
        wallet = "screen existing / café"
        empty = "screen empty / 札幌"
        for w in [funder, wallet, empty]:
            node.call(
                "createwallet",
                dict(wallet_name=w, descriptors=True, load_on_startup=False),
            )

        def address(w):
            return node.call("getnewaddress", wallet=w)

        def mine(n):
            node.call("generatetoaddress", dict(nblocks=n, address=address(funder)))

        def broadcast(w, txid):
            node.call(
                "sendrawtransaction",
                dict(
                    hexstring=node.call("gettransaction", dict(txid=txid), wallet=w)[
                        "hex"
                    ]
                ),
            )

        node.call("setmocktime", dict(timestamp=1800000000))
        mine(101)
        for amount in ["5.00000001", "3.00000000"]:
            broadcast(
                funder,
                node.call(
                    "sendtoaddress",
                    dict(address=address(wallet), amount=amount, fee_rate=2),
                    wallet=funder,
                ),
            )
        mine(1)
        broadcast(
            wallet,
            node.call(
                "sendtoaddress",
                dict(address=address(wallet), amount="1.00000000", fee_rate=2),
                wallet=wallet,
            ),
        )
        mine(1)
        if a.coins or a.controls:
            node.call("generatetoaddress", dict(nblocks=1, address=address(wallet)))
        balances = node.call("getbalances", wallet=wallet)["mine"]
        expected = sum(
            sat(balances[k]) for k in ["trusted", "untrusted_pending", "immature"]
        )
        genesis = node.call("getblockhash", [0])
        record.update(
            expectedSatoshis=expected,
            genesis=genesis,
            wallet=wallet,
            emptyWallet=empty,
            backend=node.identity,
        )
        if a.addresses or a.coins or a.controls:
            record["descriptorIndexesBefore"] = node.call(
                "listdescriptors", dict(private=False), wallet=wallet
            )

        def external_conditions(method):
            marker = out / "captures/external-conditions.json"
            if (
                not a.controls
                or method != "getblockchaininfo"
                or not marker.exists()
                or "externalController" in record
            ):
                return
            payload = json.loads(marker.read_text())
            assert payload["genesis"] == genesis and payload["wallet"] == wallet
            pins = payload["pins"]
            assert len(pins) == 2 and len({(p["txid"], p["vout"]) for p in pins}) == 2
            available = {
                (p["txid"], p["vout"]): p
                for p in node.call(
                    "listunspent", dict(minconf=0, include_unsafe=True), wallet=wallet
                )
            }
            for pin in pins:
                assert (pin["txid"], pin["vout"]) in available and sat(
                    available[(pin["txid"], pin["vout"])]["amount"]
                ) == pin["satoshis"]
            effects = dict(
                startUTC=datetime.now(timezone.utc).isoformat(),
                requestedPins=pins,
                scope="Explicit owned controller effects; NOT GUI mutation RPC",
            )
            record["externalController"] = effects
            try:
                locked = {k: pins[0][k] for k in ["txid", "vout"]}
                spent = {k: pins[1][k] for k in ["txid", "vout"]}
                assert node.call(
                    "lockunspent",
                    dict(unlock=False, transactions=[locked]),
                    wallet=wallet,
                )
                effects["locked"] = locked
                destination = address(funder)
                amount = format(Decimal(pins[1]["satoshis"] - 2000) / 100000000, ".8f")
                raw = node.call(
                    "createrawtransaction",
                    dict(inputs=[spent], outputs=[{destination: amount}]),
                )
                signed = node.call(
                    "signrawtransactionwithwallet", dict(hexstring=raw), wallet=wallet
                )
                assert signed["complete"]
                effects["spent"] = spent
                effects["spentTxid"] = node.call(
                    "sendrawtransaction", dict(hexstring=signed["hex"])
                )
                effects["completed"] = True
                effects["endUTC"] = datetime.now(timezone.utc).isoformat()
            except BaseException as error:
                effects["failure"] = dict(
                    type=type(error).__name__, rpcCode=getattr(error, "code", None)
                )
                raise

        proxy = ReadOnlyProxy(
            node,
            out / "captures/disconnect" if a.states or a.coins else None,
            external_conditions if a.controls else None,
        )
        with tempfile.TemporaryDirectory(
            prefix="history-ui-", dir=validated_scratch()
        ) as tmp:
            argv = [
                sys.executable,
                "-B",
                str(Path(__file__).resolve().parents[1] / "platform/owned_display.py"),
                "--output",
                str(out / "native"),
                "--timeout",
                "35",
                "--",
                str(a.binary),
                str(out / "captures"),
                str(a.fonts),
                str(Path(tmp) / "metadata.sqlite"),
                str(proxy.port),
                str(node.data / "regtest/.cookie"),
                genesis,
                wallet,
                empty,
                str(expected),
            ]
            if a.states:
                argv.append("states")
            with (out / "output.log").open("w") as log:
                run = subprocess.run(
                    argv, stdout=log, stderr=subprocess.STDOUT, timeout=50
                )
        record["metadataScratchRemoved"] = not Path(tmp).exists()
        if a.controls and record.get("externalController", {}).get("completed"):
            effects = record["externalController"]
            locks = node.call("listlockunspent", wallet=wallet)
            record["externalLockRetained"] = effects["locked"] in locks
            remaining = {
                (p["txid"], p["vout"])
                for p in node.call(
                    "listunspent", dict(minconf=0, include_unsafe=True), wallet=wallet
                )
            }
            record["spentPinAbsent"] = (
                effects["spent"]["txid"],
                effects["spent"]["vout"],
            ) not in remaining
            assert record["externalLockRetained"] and record["spentPinAbsent"]
        if a.addresses or a.coins or a.controls:
            record["descriptorIndexesAfter"] = node.call(
                "listdescriptors", dict(private=False), wallet=wallet
            )
            record["noIndexAdvance"] = (
                record["descriptorIndexesBefore"] == record["descriptorIndexesAfter"]
            )
            assert record["noIndexAdvance"], (
                "GUI enumeration changed descriptor ranges/next indexes"
            )
except BaseException as e:
    failure = type(e).__name__ + ": " + str(e)
finally:
    if proxy:
        proxy.close()
        record.update(
            readOnlyRequests=proxy.events,
            mutationRejected=proxy.failure,
            proxySettled=not proxy.thread.is_alive(),
        )
    if node:
        record["daemonProcess"] = node.record
record["journey"] = (
    "coin-context-actions/send-pinned-coins"
    if a.controls
    else "utxos-selection/utxos-unavailable"
    if a.coins
    else "addresses-receive-change"
    if a.addresses
    else "history-stale/history-partial-scan"
    if a.states
    else "history-screen"
)
record.update(
    argv=argv,
    cwd=str(Path.cwd()),
    startUTC=start,
    endUTC=datetime.now(timezone.utc).isoformat(),
    exit=run.returncode if run else None,
    failure=failure,
    binary=binary_identity(a.binary),
    coreRuntime="UNEXECUTED",
)
if (out / "captures/result.json").exists():
    record["result"] = json.loads((out / "captures/result.json").read_text())
if (out / "native/processes.json").exists():
    record["displayProcess"] = json.loads((out / "native/processes.json").read_text())
record["readOnlyVerified"] = bool(
    proxy
    and not proxy.failure
    and any(
        e["method"] == "gettransaction" and e["params"].get("verbose") is True
        for e in proxy.events
    )
)
record["secretsAbsent"] = bool(
    node
    and all(
        all(secret not in file.read_text(errors="replace") for secret in node.secrets)
        for file in out.rglob("*.log")
    )
)
write_json(out / "report.json", record)
ok = (
    not failure
    and run
    and run.returncode == 0
    and record.get("result", {}).get("passed") == 1
    and record["readOnlyVerified"]
    and record["secretsAbsent"]
    and record["daemonProcess"]["settled"]
    and record["daemonProcess"]["datadirRemoved"]
    and record["proxySettled"]
    and record["metadataScratchRemoved"]
)
ok = ok and (
    not a.controls
    or (
        record.get("externalController", {}).get("completed")
        and record.get("externalLockRetained")
        and record.get("spentPinAbsent")
    )
)
print(
    json.dumps(
        dict(
            passed=bool(ok),
            exit=record["exit"],
            failure=failure,
            result=record.get("result"),
        )
    )
)
raise SystemExit(0 if ok else 1)
