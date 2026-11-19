# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Inspect and stage fixture public signer material in an owned native window."""

import argparse
from datetime import datetime, timezone
import hashlib
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
from rpc.history_tests import ReadOnlyProxy
from support.runtime import binary_identity, evidence_directory, write_json

ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"


def vpub_for(tpub):
    number = 0
    for char in tpub:
        number = number * 58 + ALPHABET.index(char)
    original = number.to_bytes(82, "big")

    def checksum(raw):
        return hashlib.sha256(hashlib.sha256(raw).digest()).digest()[:4]

    assert original[:4] == bytes.fromhex("043587cf") and original[-4:] == checksum(
        original[:-4]
    )
    public = bytes.fromhex("045f1cf6") + original[4:-4]
    number = int.from_bytes(public + checksum(public), "big")
    text = ""
    while number:
        number, digit = divmod(number, 58)
        text = ALPHABET[digit] + text
    assert text.startswith("vpub")
    return text


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
    origin, alternate = "keystore-origin", "controller-public-signer"
    try:
        with OwnedRegtest(args.daemon, out / "daemon", public_descriptors=True) as node:
            node.ready()
            assert node.call("getnetworkinfo")["connections"] == 0
            for wallet in (origin, alternate):
                node.call(
                    "createwallet",
                    dict(wallet_name=wallet, descriptors=True, load_on_startup=False),
                )
            old_wallet = node.call(
                "listdescriptors", dict(private=False), wallet=origin
            )
            public = node.call("listdescriptors", dict(private=False), wallet=alternate)
            native = next(
                row["desc"]
                for row in public["descriptors"]
                if row["active"]
                and not row["internal"]
                and row["desc"].startswith("wpkh(")
            )
            key = re.search(r"tpub[1-9A-HJ-NP-Za-km-z]{107}", native).group()
            alias = vpub_for(key)
            expected = node.call(
                "getdescriptorinfo",
                dict(descriptor=f"wpkh([deada00d/84h/1h/0h]{key}/0/*)"),
            )
            assert (
                expected["issolvable"]
                and expected["isrange"]
                and not expected["hasprivatekeys"]
            )
            genesis = node.call("getblockhash", [0])
            proxy = ReadOnlyProxy(node)
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
                    origin,
                    alias,
                    key,
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
            ), "native keystore did not complete"
            assert not proxy.failure, "unapproved GUI RPC"
            assert (
                result["canonicalXpub"] == key
                and result["descriptor"] == expected["descriptor"]
            )
            assert result["savedBranches"] == len(old_wallet["descriptors"])
            assert (
                node.call("listdescriptors", dict(private=False), wallet=origin)
                == old_wallet
            )
            assert (
                node.call("listdescriptors", dict(private=False), wallet=alternate)
                == public
            )
            assert len(node.call("listwallets")) == 2
            assert node.call("getwalletinfo", wallet=origin)["private_keys_enabled"]
            observed.update(
                canonicalKeyChecked=True,
                exactStagedDescriptor=True,
                daemonCustodyPreserved=True,
                originalPoliciesPreserved=True,
                applicationMutations=0,
                allocatedAddresses=0,
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
