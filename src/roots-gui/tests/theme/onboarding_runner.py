# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Drive the native wallet workspace against an explicit disposable Roots node."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import signal
import subprocess
import sys
import tempfile

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


class LifecycleProxy(ReadOnlyProxy):
    allowed = ReadOnlyProxy.allowed | {
        "listwallets",
        "listwalletdir",
        "loadwallet",
        "createwallet",
    }

    def __init__(self, node, *, after_read=None):
        super().__init__(node, after_read=after_read)
        self.server.daemon_threads = False  # wait for every owned handler at teardown


def main():
    def interrupted(*_):
        raise SystemExit(143)

    signal.signal(signal.SIGTERM, interrupted)
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("binary", "daemon", "fonts", "output"):
        parser.add_argument("--" + option, required=True, type=Path)
    args = parser.parse_args()
    out = evidence_directory(args.output)
    start = datetime.now(timezone.utc).isoformat()
    scratch = None
    node = None
    proxy = None
    result = None
    command = None
    exit_code = None
    failure = None
    observed = {}
    try:
        with (
            tempfile.TemporaryDirectory(
                prefix="wallet-onboarding-", dir=validated_scratch()
            ) as scratch,
            OwnedRegtest(args.daemon, out / "daemon") as node,
        ):
            node.ready()
            assert node.call("getnetworkinfo")["connections"] == 0
            node.call("createwallet", {"wallet_name": "existing-cold"})
            node.call("unloadwallet", {"wallet_name": "existing-cold"})
            assert node.call("listwallets") == []
            genesis = node.call("getblockhash", [0])
            proxy = LifecycleProxy(node)
            try:
                command = [
                    sys.executable,
                    "-B",
                    str(tests / "platform/owned_display.py"),
                    "--output",
                    str(out / "display"),
                    "--timeout",
                    "42",
                    "--",
                    str(args.binary.resolve()),
                    str(out / "captures"),
                    str(args.fonts.resolve()),
                    str(proxy.port),
                    str(node.data / "regtest/.cookie"),
                    genesis,
                    str(Path(scratch) / "public.sqlite"),
                ]
                with (out / "native.log").open("w") as log:
                    exit_code = subprocess.run(
                        command, stdout=log, stderr=subprocess.STDOUT, timeout=55
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
            ), "native interaction did not complete"
            assert not proxy.failure, "application issued an unexpected RPC"
            mutating = [
                event
                for event in proxy.events
                if event["method"] in {"createwallet", "loadwallet"}
            ]
            assert mutating == [
                {
                    "method": "createwallet",
                    "path": "/",
                    "params": {
                        "wallet_name": "native created wallet",
                        "disable_private_keys": False,
                        "blank": False,
                        "descriptors": True,
                        "external_signer": False,
                    },
                },
                {
                    "method": "loadwallet",
                    "path": "/",
                    "params": {"filename": "existing-cold"},
                },
            ], mutating
            observed["loaded"] = sorted(node.call("listwallets"))
            assert observed["loaded"] == ["existing-cold", "native created wallet"]
            observed["privateKeysHeldByDaemon"] = node.call(
                "getwalletinfo", wallet="native created wallet"
            )["private_keys_enabled"]
            assert observed["privateKeysHeldByDaemon"] is True
            assert all("getnewaddress" != event["method"] for event in proxy.events)
            observed["applicationMutations"] = len(mutating)
    except BaseException as error:
        failure = f"{type(error).__name__}: {error}"
    processes_path = out / "display/processes.json"
    processes = (
        json.loads(processes_path.read_text()) if processes_path.exists() else {}
    )
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
        metadata=bool(scratch and not Path(scratch).exists()),
    )
    secrets_absent = bool(node) and all(
        all(secret not in path.read_text(errors="replace") for secret in node.secrets)
        for path in out.rglob("*")
        if path.suffix in (".log", ".txt")
    )
    record = dict(
        argv=command,
        cwd=str(Path.cwd()),
        startUTC=start,
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
