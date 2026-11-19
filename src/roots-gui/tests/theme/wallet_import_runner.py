# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Drive a public import and daemon backup/restore round trip on an owned Roots node."""

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
from theme.onboarding_runner import LifecycleProxy
from support.runtime import binary_identity, evidence_directory, write_json


class TransferProxy(LifecycleProxy):
    allowed = LifecycleProxy.allowed | {
        "importdescriptors",
        "backupwallet",
        "restorewallet",
    }


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
    node = None
    proxy = None
    observed = {}
    result = None
    command = None
    exit_code = None
    failure = None
    try:
        with OwnedRegtest(args.daemon, out / "daemon", public_descriptors=True) as node:
            node.ready()
            assert node.call("getnetworkinfo")["connections"] == 0
            node.call("createwallet", {"wallet_name": "public-fixture-source"})
            report = node.call(
                "listdescriptors", {"private": False}, wallet="public-fixture-source"
            )
            entry = next(
                item
                for item in report["descriptors"]
                if item["active"]
                and not item["internal"]
                and item["desc"].startswith("wpkh(")
            )
            public_key = re.search(
                r"tpub[1-9A-HJ-NP-Za-km-z]{107}", entry["desc"]
            ).group()
            assert len(public_key) == 111
            genesis = node.call("getblockhash", [0])
            backup = node.data / "owned-wallet-backup.dat"
            proxy = TransferProxy(node)
            try:
                command = [
                    sys.executable,
                    "-B",
                    str(tests / "platform/owned_display.py"),
                    "--output",
                    str(out / "display"),
                    "--timeout",
                    "48",
                    "--",
                    str(args.binary.resolve()),
                    str(out / "captures"),
                    str(args.fonts.resolve()),
                    str(proxy.port),
                    str(node.data / "regtest/.cookie"),
                    genesis,
                    public_key,
                    str(backup),
                ]
                with (out / "native.log").open("w") as log:
                    exit_code = subprocess.run(
                        command, stdout=log, stderr=subprocess.STDOUT, timeout=64
                    ).returncode
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
            ), "native workflow did not complete"
            assert not proxy.failure, "application issued an unapproved RPC"
            branches = result["publicBranches"]
            assert len(branches) == 2
            expected_requests = []
            for index, branch in enumerate(branches):
                expected = node.call(
                    "getdescriptorinfo", {"descriptor": f"wpkh({public_key}/{index}/*)"}
                )
                assert (
                    branch["descriptor"] == expected["descriptor"]
                    and not expected["hasprivatekeys"]
                )
                assert node.call(
                    "deriveaddresses",
                    {"descriptor": expected["descriptor"], "range": [0, 0]},
                ) == [branch["address"]]
                expected_requests.append(
                    dict(
                        desc=expected["descriptor"],
                        timestamp=0,
                        active=True,
                        internal=bool(index),
                        range=[0, 99],
                    )
                )
            expected_mutations = [
                dict(
                    method="createwallet",
                    path="/",
                    params=dict(
                        wallet_name="native-watch",
                        disable_private_keys=True,
                        blank=True,
                        descriptors=True,
                        external_signer=False,
                    ),
                ),
                dict(
                    method="importdescriptors",
                    path="/wallet/native-watch",
                    params=dict(requests=expected_requests),
                ),
                dict(
                    method="backupwallet",
                    path="/wallet/native-watch",
                    params=dict(destination=str(backup)),
                ),
                dict(
                    method="restorewallet",
                    path="/",
                    params=dict(wallet_name="native-restored", backup_file=str(backup)),
                ),
            ]
            mutations = [
                event
                for event in proxy.events
                if event["method"] in {x["method"] for x in expected_mutations}
            ]
            assert mutations == expected_mutations, (
                "exact named mutations did not match the verified public preview"
            )
            assert backup.is_file() and backup.stat().st_size > 0
            observed["daemonBackupBytes"] = backup.stat().st_size
            descriptor_states = {}
            for wallet in ("native-watch", "native-restored"):
                info = node.call("getwalletinfo", wallet=wallet)
                assert (
                    info["walletname"] == wallet
                    and not info["private_keys_enabled"]
                    and info["descriptors"]
                ), wallet
                registered = node.call(
                    "listdescriptors", {"private": False}, wallet=wallet
                )["descriptors"]
                assert len(registered) == 2, (wallet, "public descriptor count")
                by_descriptor = {row["desc"]: row for row in registered}
                descriptor_states[wallet] = []
                for index, branch in enumerate(branches):
                    address = node.call(
                        "getaddressinfo", {"address": branch["address"]}, wallet=wallet
                    )
                    assert (
                        address["ismine"]
                        and address["scriptPubKey"] == branch["script"]
                        and address["parent_desc"] == branch["descriptor"]
                    ), (wallet, index, "script proof")
                    entry = by_descriptor[branch["descriptor"]]
                    state = {
                        k: entry[k]
                        for k in ("active", "internal", "range", "next_index")
                    }
                    descriptor_states[wallet].append(state)
                    # The daemon tops up its active keypool beyond the requested
                    # range. The round trip must retain the actual full range and next index.
                    assert (
                        state["active"]
                        and state["range"][0] == 0
                        and state["range"][1] >= 99
                        and state["next_index"] == 0
                        and state["internal"] == bool(index)
                    ), (wallet, index, state)
            assert (
                descriptor_states["native-watch"]
                == descriptor_states["native-restored"]
            ), descriptor_states
            observed.update(
                watchOnlyRestored=True,
                exactPublicScripts=2,
                applicationMutations=len(mutations),
                addressAllocation=0,
                daemonDescriptorStates=descriptor_states,
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
