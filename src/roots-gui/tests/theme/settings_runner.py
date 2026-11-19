# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Drive staged Settings and a distinct verified watch-only wallet on owned Roots."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import signal
import subprocess
import sys

sys.dont_write_bytecode = True
tests = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(tests))
from integration.owned_regtest import OwnedRegtest
from theme.wallet_import_runner import TransferProxy
from support.runtime import binary_identity, evidence_directory, write_json


def main():
    def interrupted(*_):
        raise SystemExit(143)

    signal.signal(signal.SIGTERM, interrupted)
    parser = argparse.ArgumentParser(description=__doc__)
    for item in ("binary", "daemon", "fonts", "output"):
        parser.add_argument("--" + item, type=Path, required=True)
    args = parser.parse_args()
    out = evidence_directory(args.output)
    started = datetime.now(timezone.utc).isoformat()
    node = proxy = result = command = exit_code = failure = None
    observed = {}
    original, signer, created = (
        "settings-origin",
        "fixture-other-custodian",
        "settings-watch",
    )
    try:
        with OwnedRegtest(args.daemon, out / "daemon", public_descriptors=True) as node:
            node.ready()
            assert node.call("getnetworkinfo")["connections"] == 0
            for name in (original, signer):
                node.call(
                    "createwallet",
                    dict(wallet_name=name, descriptors=True, load_on_startup=False),
                )
            original_records = node.call(
                "listdescriptors", {"private": False}, wallet=original
            )
            records = [
                original_records,
                node.call("listdescriptors", {"private": False}, wallet=signer),
            ]

            def key(report):
                descriptor = next(
                    row["desc"]
                    for row in report["descriptors"]
                    if row["active"]
                    and not row["internal"]
                    and row["desc"].startswith("wpkh(")
                )
                return re.search(r"tpub[1-9A-HJ-NP-Za-km-z]{107}", descriptor).group()

            first, second = map(key, records)
            assert first != second
            expression = f"wsh(sortedmulti(2,{first}/0/*,{second}/0/*))"
            expected = node.call("getdescriptorinfo", dict(descriptor=expression))
            assert not expected["hasprivatekeys"] and expected["isrange"]
            genesis = node.call("getblockhash", [0])
            proxy = TransferProxy(node)
            try:
                command = [
                    sys.executable,
                    "-B",
                    str(tests / "platform/owned_display.py"),
                    "--output",
                    str(out / "display"),
                    "--timeout",
                    "56",
                    "--",
                    str(args.binary.resolve()),
                    str(out / "captures"),
                    str(args.fonts.resolve()),
                    str(proxy.port),
                    str(node.data / "regtest/.cookie"),
                    genesis,
                    original,
                    expression,
                ]
                with (out / "native.log").open("w") as log:
                    exit_code = subprocess.run(
                        command, stdout=log, stderr=subprocess.STDOUT, timeout=70
                    ).returncode
            finally:
                proxy.close()
            written = out / "captures/result.json"
            if written.exists():
                result = json.loads(written.read_text())
            assert (
                exit_code == 0
                and result
                and result["passed"] == 1
                and result["teardown"]
            )
            assert not proxy.failure, "unapproved GUI RPC"
            new = node.call("getwalletinfo", wallet=created)
            old = node.call("getwalletinfo", wallet=original)
            assert (
                not new["private_keys_enabled"]
                and old["private_keys_enabled"]
                and new["walletname"] == created
            )
            assert (
                node.call("listdescriptors", dict(private=False), wallet=original)
                == original_records
            )
            assert result["originalExport"].splitlines() == [
                row["desc"] for row in original_records["descriptors"]
            ]
            assert result["descriptor"] == expected["descriptor"]
            saved = node.call("listdescriptors", dict(private=False), wallet=created)[
                "descriptors"
            ]
            assert (
                len(saved) == 1
                and saved[0]["desc"] == result["descriptor"]
                and saved[0]["active"]
                and not saved[0]["internal"]
            )
            assert (
                saved[0]["range"][0] == 0
                and saved[0]["range"][1] >= 99
                and saved[0]["next_index"] == 0
            )
            assert node.call(
                "deriveaddresses", dict(descriptor=expected["descriptor"], range=[0, 0])
            ) == [result["address"]]
            proof = node.call(
                "getaddressinfo", dict(address=result["address"]), wallet=created
            )
            assert (
                proof["ismine"]
                and proof["scriptPubKey"] == result["script"]
                and proof["parent_desc"] == result["descriptor"]
            )
            mutations = [
                event
                for event in proxy.events
                if event["method"]
                in {
                    "loadwallet",
                    "createwallet",
                    "backupwallet",
                    "restorewallet",
                    "importdescriptors",
                }
            ]
            assert mutations == [
                dict(
                    method="createwallet",
                    path="/",
                    params=dict(
                        wallet_name=created,
                        disable_private_keys=True,
                        blank=True,
                        descriptors=True,
                        external_signer=False,
                    ),
                ),
                dict(
                    method="importdescriptors",
                    path="/wallet/" + created,
                    params=dict(
                        requests=[
                            dict(
                                desc=result["descriptor"],
                                timestamp=0,
                                active=True,
                                internal=False,
                                range=[0, 99],
                            )
                        ]
                    ),
                ),
            ]
            observed.update(
                oldWalletPreserved=True,
                savedMultisigVerified=True,
                acknowledgedWallet=created,
                originalWallet=original,
                originalDescriptors=len(original_records["descriptors"]),
                applicationMutations=len(mutations),
                allocatedAddresses=0,
                requestedRange=[0, 99],
                daemonRange=saved[0]["range"],
                daemonNext=saved[0]["next_index"],
            )
    except BaseException as error:
        failure = f"{type(error).__name__}: {error}"
    process_file = out / "display/processes.json"
    processes = json.loads(process_file.read_text()) if process_file.exists() else {}
    settlement = dict(
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
    )
    secret_free = bool(node) and all(
        all(s not in p.read_text(errors="replace") for s in node.secrets)
        for p in out.rglob("*")
        if p.suffix in (".log", ".txt")
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
        settlement=settlement,
        secretsAbsent=secret_free,
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
    return int(failure is not None or not secret_free or not all(settlement.values()))


if __name__ == "__main__":
    raise SystemExit(main())
