# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Verify wallet-scoped import, export, rescan and restart on an owned Roots node."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import threading
import traceback

sys.dont_write_bytecode = True
tests = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(tests))
from integration.owned_regtest import OwnedRegtest
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)
from theme.wallet_import_runner import TransferProxy


class ScanProxy(TransferProxy):
    allowed = TransferProxy.allowed | {"rescanblockchain"}


def now():
    return datetime.now(timezone.utc).isoformat()


def main():
    def interrupted(*_):
        raise SystemExit(143)

    signal.signal(signal.SIGTERM, interrupted)
    parser = argparse.ArgumentParser(description=__doc__)
    for item in ("binary", "daemon", "fonts", "output"):
        parser.add_argument("--" + item, type=Path, required=True)
    args = parser.parse_args()
    out = evidence_directory(args.output)
    started = now()
    node = proxy = result = command = exit_code = failure = scratch = restart = None
    observed = {}
    scan_replies = []
    before_scans = []
    release = threading.Event()
    original, signer, created = (
        "rescan-origin",
        "controller-rescan-signer",
        "rescan-watch",
    )

    def after_read(method, path, params, status, body):
        if (
            method == "importdescriptors"
            and path == "/wallet/" + created
            and status == 200
        ):
            before_scans.extend(
                node.call("listdescriptors", {"private": False}, wallet=created)[
                    "descriptors"
                ]
            )
        if method != "rescanblockchain":
            return
        response = json.loads(body)
        scan_replies.append(
            dict(
                path=path,
                params=params,
                status=status,
                result=response.get("result"),
                errorCode=(response.get("error") or {}).get("code"),
                observedUTC=now(),
            )
        )
        if params == {"start_height": 2, "stop_height": 2}:
            (out / "rescan-accepted").touch()
            release.wait(timeout=18)

    try:
        with (
            tempfile.TemporaryDirectory(
                prefix="settings-rescan-", dir=validated_scratch()
            ) as scratch,
            OwnedRegtest(args.daemon, out / "daemon", public_descriptors=True) as node,
        ):
            node.ready()
            assert node.call("getnetworkinfo")["connections"] == 0
            for name in (original, signer):
                node.call(
                    "createwallet",
                    dict(wallet_name=name, descriptors=True, load_on_startup=False),
                )
            mining_address = node.call("getnewaddress", wallet=signer)
            assert (
                len(
                    node.call(
                        "generatetoaddress", dict(nblocks=2, address=mining_address)
                    )
                )
                == 2
            )
            original_records = node.call(
                "listdescriptors", {"private": False}, wallet=original
            )
            public_records = node.call(
                "listdescriptors", {"private": False}, wallet=signer
            )
            public = next(
                row["desc"]
                for row in public_records["descriptors"]
                if row["active"]
                and not row["internal"]
                and row["desc"].startswith("wpkh(")
            )
            expected = node.call("getdescriptorinfo", dict(descriptor=public))
            assert not expected["hasprivatekeys"] and expected["isrange"]
            genesis = node.call("getblockhash", [0])
            proxy = ScanProxy(node, after_read=after_read)
            metadata = Path(scratch) / "metadata.sqlite"
            try:
                command = [
                    sys.executable,
                    "-B",
                    str(tests / "platform/owned_display.py"),
                    "--output",
                    str(out / "display"),
                    "--timeout",
                    "54",
                    "--",
                    str(args.binary.resolve()),
                    str(out / "captures"),
                    str(args.fonts.resolve()),
                    str(proxy.port),
                    str(node.data / "regtest/.cookie"),
                    genesis,
                    public,
                    str(metadata),
                    str(out / "rescan-accepted"),
                ]
                with (out / "native.log").open("w") as log:
                    exit_code = subprocess.run(
                        command, stdout=log, stderr=subprocess.STDOUT, timeout=65
                    ).returncode
                result_path = out / "captures/result.json"
                if result_path.exists():
                    result = json.loads(result_path.read_text())
                assert (
                    exit_code == 0
                    and result
                    and result["passed"] == 1
                    and result["teardown"]
                )
                restart_argv = [str(args.binary.resolve()), "--restore", str(metadata)]
                before_restart = len(proxy.events)
                restart_start = now()
                child = subprocess.run(
                    restart_argv, capture_output=True, text=True, timeout=12
                )
                restart = dict(
                    argv=restart_argv,
                    cwd=str(Path.cwd()),
                    startedUTC=restart_start,
                    completedUTC=now(),
                    exit=child.returncode,
                    stdout=child.stdout,
                    stderr=child.stderr,
                    rpcRequests=len(proxy.events) - before_restart,
                )
                assert (
                    child.returncode == 0
                    and not restart["rpcRequests"]
                    and not child.stderr
                )
                assert json.loads(child.stdout) == dict(
                    exactOfflineWallets=2, pendingRange=[2, 2], commandsRestored=0
                )
            finally:
                release.set()
                proxy.close()
            assert not proxy.failure, "unexpected application RPC"
            assert result["watchExport"] == expected["descriptor"]
            assert result["oldExport"].splitlines() == [
                row["desc"] for row in original_records["descriptors"]
            ]
            assert (
                node.call("listdescriptors", {"private": False}, wallet=original)
                == original_records
            )
            assert (
                node.call("listdescriptors", {"private": False}, wallet=signer)
                == public_records
            )
            assert node.call("getwalletinfo", wallet=original)["private_keys_enabled"]
            assert not node.call("getwalletinfo", wallet=created)[
                "private_keys_enabled"
            ]
            saved = node.call("listdescriptors", {"private": False}, wallet=created)[
                "descriptors"
            ]
            assert len(saved) == 1 and saved[0]["desc"] == expected["descriptor"], (
                "watch descriptor identity changed"
            )
            assert saved == before_scans and saved[0]["next_index"] == 1, (
                "rescans changed the imported wallet's descriptor inventory"
            )
            retained = node.call(
                "listunspent", {"minconf": 0, "include_unsafe": True}, wallet=created
            )
            assert all(entry["address"] == mining_address for entry in retained)
            assert node.call("getwalletinfo", wallet=created)["txcount"] == 2, (
                "historical coinbases were not retained"
            )
            mutations = [
                entry
                for entry in proxy.events
                if entry["method"]
                in {
                    "loadwallet",
                    "createwallet",
                    "backupwallet",
                    "restorewallet",
                    "importdescriptors",
                    "rescanblockchain",
                }
            ]
            ranges = [
                dict(start_height=1, stop_height=2),
                dict(start_height=2, stop_height=2),
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
                                desc=expected["descriptor"],
                                timestamp=0,
                                active=True,
                                internal=False,
                                range=[0, 99],
                            )
                        ]
                    ),
                ),
                *[
                    dict(
                        method="rescanblockchain",
                        path="/wallet/" + created,
                        params=bounds,
                    )
                    for bounds in ranges
                ],
            ], mutations
            assert [each["result"] for each in scan_replies] == ranges and all(
                each["status"] == 200 for each in scan_replies
            )
            observed.update(
                independentSavedExports=2,
                exactDaemonWallets=[original, created],
                originalPolicyPreserved=True,
                scanAcknowledged=1,
                responseWithheldAfterDaemonCompleted=1,
                rescanRequests=2,
                applicationMutations=4,
                allocatedAddresses=0,
                restoredWithNoRequests=True,
                priorHistoryEntries=2,
                watchNextIndexBeforeAndAfter=saved[0]["next_index"],
            )
    except BaseException as error:
        (out / "failure.txt").write_text(traceback.format_exc())
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
        metadata=bool(scratch and not Path(scratch).exists()),
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
        endUTC=now(),
        exit=exit_code,
        binary=binary_identity(args.binary),
        daemonProcess=node.record if node else None,
        result=result,
        observed=observed,
        settlement=settlement,
        secretsAbsent=secret_free,
        restart=restart,
        applicationRequests=proxy.events if proxy else [],
        daemonScanResponses=scan_replies,
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
