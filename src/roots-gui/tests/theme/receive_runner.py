# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Pay the exact address from one owned native Receive journey on Roots regtest."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
tests = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(tests))
from integration.owned_regtest import OwnedRegtest
from rpc.history_tests import ReadOnlyProxy
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)


class ReceiveProxy(ReadOnlyProxy):
    allowed = ReadOnlyProxy.allowed | {"getnewaddress"}


def main():
    def interrupted(*_):
        raise SystemExit(143)

    signal.signal(signal.SIGTERM, interrupted)
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("binary", "daemon", "fonts", "output"):
        parser.add_argument("--" + option, type=Path, required=True)
    args = parser.parse_args()
    out = evidence_directory(args.output)
    started = datetime.now(timezone.utc).isoformat()
    node = proxy = result = command = failure = exit_code = None
    observed = {}
    wallet = "receive-first"
    funder = "owned-controller"
    with tempfile.TemporaryDirectory(
        prefix="roots-gui-receive-", dir=validated_scratch()
    ) as scratch:
        try:
            with OwnedRegtest(
                args.daemon, out / "daemon", public_descriptors=True
            ) as node:
                node.ready()
                assert node.call("getnetworkinfo")["connections"] == 0
                for name in (wallet, funder):
                    node.call(
                        "createwallet",
                        dict(wallet_name=name, descriptors=True, load_on_startup=False),
                    )
                funding = node.call("getnewaddress", wallet=funder)
                node.call("generatetoaddress", dict(nblocks=101, address=funding))
                genesis = node.call("getblockhash", [0])
                proxy = ReceiveProxy(node)
                try:
                    captures = out / "captures"
                    metadata = Path(scratch) / "metadata.sqlite"
                    command = [
                        sys.executable,
                        "-B",
                        str(tests / "platform/owned_display.py"),
                        "--output",
                        str(out / "display"),
                        "--timeout",
                        "69",
                        "--",
                        str(args.binary.resolve()),
                        str(captures),
                        str(args.fonts.resolve()),
                        str(metadata),
                        str(proxy.port),
                        str(node.data / "regtest/.cookie"),
                        genesis,
                        wallet,
                    ]
                    with (out / "native.log").open("w") as log:
                        with subprocess.Popen(
                            command, stdout=log, stderr=subprocess.STDOUT
                        ) as native:
                            try:
                                until = time.monotonic() + 25
                                while not (captures / "displayed.json").exists():
                                    assert (
                                        native.poll() is None
                                        and time.monotonic() < until
                                    ), "native displayed-address deadline"
                                    time.sleep(0.02)
                                displayed = json.loads(
                                    (captures / "displayed.json").read_text()
                                )
                                proof = node.call(
                                    "getaddressinfo",
                                    dict(address=displayed["address"]),
                                    wallet=wallet,
                                )
                                assert proof["ismine"] and not proof["ischange"]
                                assert (
                                    proof["address"],
                                    proof["scriptPubKey"],
                                    proof["desc"],
                                    proof["hdkeypath"],
                                ) == (
                                    displayed["address"],
                                    displayed["script"],
                                    displayed["descriptor"],
                                    displayed["path"],
                                )
                                assert node.call(
                                    "deriveaddresses",
                                    dict(descriptor=displayed["descriptor"]),
                                ) == [displayed["address"]]
                                txid = node.call(
                                    "sendtoaddress",
                                    dict(
                                        address=displayed["address"],
                                        amount="1.23456789",
                                        fee_rate=2,
                                    ),
                                    wallet=funder,
                                )
                                raw = node.call(
                                    "gettransaction", dict(txid=txid), wallet=funder
                                )["hex"]
                                assert (
                                    node.call("sendrawtransaction", dict(hexstring=raw))
                                    == txid
                                )
                                node.call(
                                    "generatetoaddress",
                                    dict(nblocks=1, address=funding),
                                )
                                received = node.call(
                                    "gettransaction", dict(txid=txid), wallet=wallet
                                )
                                assert received["confirmations"] == 1
                                exact = [
                                    row
                                    for row in received["details"]
                                    if row["category"] == "receive"
                                    and row["address"] == displayed["address"]
                                ]
                                assert (
                                    len(exact) == 1
                                    and str(exact[0]["amount"]) == "1.23456789"
                                )
                                observed.update(
                                    displayed=displayed,
                                    paidTxid=txid,
                                    paidSats=123456789,
                                    confirmations=1,
                                    controllerPayments=1,
                                )
                                (captures / "paid.ready").touch()
                                exit_code = native.wait(timeout=50)
                            finally:
                                if native.poll() is None:
                                    native.terminate()
                                    try:
                                        exit_code = native.wait(timeout=8)
                                    except subprocess.TimeoutExpired:
                                        native.kill()
                                        exit_code = native.wait(timeout=3)
                finally:
                    proxy.close()
                result_path = out / "captures/result.json"
                if result_path.exists():
                    result = json.loads(result_path.read_text())
                assert (
                    exit_code == 0
                    and result
                    and result["passed"] == 1
                    and result["teardown"]
                ), "native receive workflow incomplete"
                assert not proxy.failure, "unexpected GUI RPC"
                mutations = [
                    event
                    for event in proxy.events
                    if event["method"] == "getnewaddress"
                ]
                assert (
                    mutations
                    == [
                        dict(
                            method="getnewaddress", path="/wallet/" + wallet, params=[]
                        )
                    ]
                    * 2
                )
                reserved = node.call(
                    "getaddressinfo",
                    dict(address=result["reservedAddress"]),
                    wallet=wallet,
                )
                assert reserved["ismine"] and not reserved["ischange"]
                assert (
                    reserved["scriptPubKey"] == result["reservedScript"]
                    and reserved["desc"] == result["reservedDescriptor"]
                )
                listed = node.call(
                    "listdescriptors", {"private": False}, wallet=wallet
                )["descriptors"]
                branch = next(
                    item for item in listed if item["desc"] == proof["parent_desc"]
                )
                assert (
                    reserved["parent_desc"] == branch["desc"]
                    and branch["next_index"] == 2
                )
                observed.update(
                    applicationAllocations=len(mutations),
                    externalBranchNext=branch["next_index"],
                    exactDaemonProof=True,
                    offlineReservationAddress=reserved["address"],
                )
        except BaseException as exc:
            failure = f"{type(exc).__name__}: {exc}"
    process_file = out / "display/processes.json"
    processes = json.loads(process_file.read_text()) if process_file.exists() else {}
    settled = dict(
        display=bool(
            processes.get("child", {}).get("settled")
            and processes.get("processes")
            and all(
                item["settled"] and not item.get("remainingLiveGroupMembers")
                for item in processes["processes"]
            )
        ),
        daemon=bool(
            node and node.record.get("settled") and node.record.get("datadirRemoved")
        ),
        proxy=bool(proxy and not proxy.thread.is_alive()),
        metadataRemoved=not Path(scratch).exists(),
    )
    secrets_absent = bool(node) and all(
        all(secret not in path.read_text(errors="replace") for secret in node.secrets)
        for path in out.rglob("*")
        if path.suffix in (".log", ".txt")
    )
    record = dict(
        argv=command,
        cwd=str(Path.cwd()),
        startUTC=started,
        endUTC=datetime.now(timezone.utc).isoformat(),
        exit=exit_code,
        binary=binary_identity(args.binary),
        daemonProcess=node.record if node else None,
        result=result,
        observed=observed,
        settlement=settled,
        secretsAbsent=secrets_absent,
        applicationRequests=proxy.events if proxy else [],
        failure=failure,
    )
    write_json(out / "report.json", record)
    print(
        json.dumps(
            {
                k: record[k]
                for k in ("exit", "result", "observed", "settlement", "failure")
            }
        )
    )
    return int(failure is not None or not secrets_absent or not all(settled.values()))


if __name__ == "__main__":
    raise SystemExit(main())
