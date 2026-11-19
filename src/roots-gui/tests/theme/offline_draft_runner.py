# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Observe an explicit native offline PSBT journey with test-only external signing."""

import argparse
import base64
from datetime import datetime, timezone
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import traceback

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


def now():
    return datetime.now(timezone.utc).isoformat()


def read_maps(container):
    assert container[:5] == b"psbt\xff"
    offset = 5

    def compact():
        nonlocal offset
        first = container[offset]
        offset += 1
        if first < 253:
            return first
        size = {253: 2, 254: 4, 255: 8}[first]
        end = offset + size
        value = int.from_bytes(container[offset:end], "little")
        offset = end
        return value

    maps = []
    while offset < len(container):
        values = {}
        while keylen := compact():
            key = container[offset : offset + keylen]
            offset += keylen
            value_len = compact()
            assert key not in values and offset + value_len <= len(container)
            values[key] = container[offset : offset + value_len]
            offset += value_len
        maps.append(values)
    return maps


def pack(maps):
    def compact(value):
        return bytes((value,)) if value < 253 else b"\xfd" + value.to_bytes(2, "little")

    return b"psbt\xff" + b"".join(
        b"".join(compact(len(k)) + k + compact(len(v)) + v for k, v in values.items())
        + b"\0"
        for values in maps
    )


def review_fixtures(files, maps):
    def le(number, width):
        return number.to_bytes(width, "little")

    def write(name, rows):
        (files / (name + ".psbt")).write_bytes(pack(rows))

    tx = maps[0][b"\0"]
    assert tx[4] == 1 and tx[41] == 0 and tx[46] == 2
    first_end = 56 + tx[55]
    second_end = first_end + 9 + tx[first_end + 8]
    assert second_end == len(tx) - 4 and tx[55] == tx[first_end + 8]
    outputs = (tx[47:first_end], tx[first_end:second_end])
    v2 = [
        {
            b"\xfb": le(2, 4),
            b"\x02": tx[:4],
            b"\x03": tx[-4:],
            b"\x04": b"\x01",
            b"\x05": b"\x02",
        },
        {**maps[1], b"\x0e": tx[5:37], b"\x0f": tx[37:41], b"\x10": tx[42:46]},
    ]
    v2.extend(
        {**fields, b"\x03": part[:8], b"\x04": part[9:]}
        for fields, part in zip(maps[2:], outputs)
    )
    write("equivalent-v2", v2)
    v2[0][b"\x06"] = b"\x01"
    write("modifiable", v2)
    altered = [dict(reversed(list(row.items()))) for row in maps]
    altered[0][b"\xfc\x01k\x00"] = b"opaque-container-metadata"
    write("reordered-maps", altered)
    for name, wire in {
        "reordered-outputs": tx[:47] + outputs[1] + outputs[0] + tx[-4:],
        "wrong-version": le(3, 4) + tx[4:],
        "wrong-locktime": tx[:-4] + le(1, 4),
        "wrong-sequence": tx[:42] + le(0xFFFFFFFC, 4) + tx[46:],
        "forged-change": tx[: first_end + 9] + outputs[0][9:] + tx[-4:],
    }.items():
        write(name, [{b"\0": wire}, *maps[1:]])
    input_fields = maps[1]
    write("wrong-sighash", [maps[0], {**input_fields, b"\x03": le(2, 4)}, *maps[2:]])
    write("policy-conflict", [maps[0], {**input_fields, b"\x04": b"\x51"}, *maps[2:]])
    prev = input_fields[b"\x00"]
    write(
        "wrong-prevout",
        [maps[0], {**input_fields, b"\x00": prev[:-4] + le(1, 4)}, *maps[2:]],
    )
    witness = input_fields[b"\x01"]
    value = int.from_bytes(witness[:8], "little")
    write(
        "prevout-conflict",
        [maps[0], {**input_fields, b"\x01": le(value - 1, 8) + witness[8:]}, *maps[2:]],
    )


def external_fixtures(node, wallet, files):
    source = (files / "unsigned.psbt").read_bytes()
    maps = read_maps(source)
    assert sum(key[0] == 6 for key in maps[1]) == 1, (
        "verified public input origin omitted"
    )
    assert sum(key[0] == 2 for key in maps[3]) == 1, (
        "verified public change origin omitted"
    )
    review_fixtures(files, maps)

    def sign(data, *, finalize=False):
        return node.call(
            "walletprocesspsbt",
            dict(
                psbt=base64.b64encode(data).decode(),
                sign=True,
                sighashtype="ALL",
                bip32derivs=False,
                finalize=finalize,
            ),
            wallet=wallet,
        )

    valid = read_maps(base64.b64decode(sign(source)["psbt"], validate=True))
    assert len(valid) == len(maps) == 4 and valid[0][b"\0"] == maps[0][b"\0"]
    assert len([key for key in valid[1] if key.startswith(b"\x02")]) == 1
    key = b"\xfc\x01r\x00"
    valid[0][key] = b"offline-proprietary-example"
    (files / "signed.psbt").write_bytes(pack(valid))
    valid[0][key] = b"contradictory-metadata"
    (files / "conflict.psbt").write_bytes(pack(valid))

    # This controlled fixture has one blank unsigned input and two outputs.
    original = maps[0][b"\0"]
    assert original[4] == 1 and original[41] == 0 and original[46] == 2
    alternate = bytearray(original)
    value = int.from_bytes(alternate[47:55], "little")
    alternate[47:55] = (value - 1).to_bytes(8, "little")
    maps[0][b"\0"] = bytes(alternate)
    wrong = read_maps(base64.b64decode(sign(pack(maps))["psbt"], validate=True))
    (files / "wrong-transaction.psbt").write_bytes(pack(wrong))
    wrong[0][b"\0"] = original
    (files / "wrong-signature.psbt").write_bytes(pack(wrong))
    (files / "signed-ready").touch()
    alternate_raw = sign(pack(maps), finalize=True)["hex"]
    return alternate_raw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("binary", "probe", "offline-exec", "daemon", "fonts", "output"):
        parser.add_argument("--" + option, type=Path, required=True)
    args = parser.parse_args()
    out = evidence_directory(args.output)
    start = now()
    node = proxy = runner = scratch = failed = native = None
    commands = []
    observed = {}

    def run(command, title):
        began = now()
        execution = subprocess.run(command, capture_output=True, text=True, timeout=24)
        commands.append(
            dict(
                title=title,
                argv=command,
                cwd=str(Path.cwd()),
                startedUTC=began,
                completedUTC=now(),
                exit=execution.returncode,
                stdout=execution.stdout,
                stderr=execution.stderr,
            )
        )
        assert execution.returncode == 0 and not execution.stderr, (
            title,
            execution.stderr,
        )
        return json.loads(execution.stdout)

    try:
        with (
            tempfile.TemporaryDirectory(
                prefix="offline-draft-", dir=validated_scratch()
            ) as scratch,
            OwnedRegtest(args.daemon, out / "daemon", public_descriptors=True) as node,
        ):
            node.ready()
            assert node.call("getnetworkinfo")["connections"] == 0
            wallet = "offline-draft-fixture"
            node.call(
                "createwallet",
                dict(wallet_name=wallet, descriptors=True, load_on_startup=False),
            )
            node.call(
                "createwallet",
                dict(
                    wallet_name="controller-miner",
                    descriptors=True,
                    load_on_startup=False,
                ),
            )
            funded = node.call(
                "getnewaddress", dict(address_type="bech32"), wallet=wallet
            )
            input_origin = node.call(
                "getaddressinfo", dict(address=funded), wallet=wallet
            )
            descriptors = node.call(
                "listdescriptors", dict(private=False), wallet=wallet
            )["descriptors"]
            internal = next(
                row
                for row in descriptors
                if row["active"] and row["internal"] and row["desc"].startswith("wpkh(")
            )
            change_index = internal["next_index"]
            change_address = node.call(
                "deriveaddresses",
                dict(descriptor=internal["desc"], range=[change_index, change_index]),
            )[0]
            recipient = node.call(
                "getnewaddress", dict(address_type="bech32"), wallet="controller-miner"
            )
            origin = node.call("generatetoaddress", dict(nblocks=1, address=funded))[0]
            assert (
                len(
                    node.call("generatetoaddress", dict(nblocks=100, address=recipient))
                )
                == 100
            )
            files = Path(scratch)
            metadata = files / "metadata.sqlite"
            (files / "recipient.txt").write_text(recipient + "\n")
            genesis = node.call("getblockhash", [0])
            proxy = ReadOnlyProxy(node)
            try:
                online = [
                    str(proxy.port),
                    str(node.data / "regtest/.cookie"),
                    genesis,
                    wallet,
                    str(metadata),
                ]
                cache = run(
                    [str(args.probe), "cache", *online],
                    "verified public cache and disconnect",
                )
                before_native = len(proxy.events)
                argv = [
                    sys.executable,
                    "-B",
                    str(tests / "platform/owned_display.py"),
                    "--output",
                    str(out / "display"),
                    "--timeout",
                    "52",
                    "--",
                    str(args.offline_exec),
                    "--unix-only",
                    str(args.binary),
                    str(out / "captures"),
                    str(args.fonts),
                    scratch,
                ]
                command = dict(
                    title="owned compact native offline journey",
                    argv=argv,
                    cwd=str(Path.cwd()),
                    startedUTC=now(),
                )
                commands.append(command)
                with (out / "native.log").open("w") as log:
                    runner = subprocess.Popen(
                        argv, stdout=log, stderr=subprocess.STDOUT
                    )
                    try:
                        deadline = time.monotonic() + 30
                        while (
                            not (files / "unsigned-ready").exists()
                            and runner.poll() is None
                            and time.monotonic() < deadline
                        ):
                            time.sleep(0.02)
                        assert (files / "unsigned-ready").exists(), (
                            "offline unsigned export not acknowledged"
                        )
                        alternative = external_fixtures(node, wallet, files)
                        command["exit"] = runner.wait(timeout=32)
                        assert command["exit"] == 0, "native journey did not complete"
                    finally:
                        if runner.poll() is None:
                            runner.terminate()
                            runner.wait(timeout=8)
                        command.setdefault("exit", runner.returncode)
                        command["completedUTC"] = now()
                assert len(proxy.events) == before_native, (
                    "offline native process made an RPC request"
                )
                native = json.loads((out / "captures/result.json").read_text())
                assert (
                    native["passed"] == 1
                    and native["teardown"]
                    and native["rpcPorts"] == 0
                )
                verified = run(
                    [
                        str(args.offline_exec),
                        str(args.probe),
                        "verify",
                        scratch,
                        str(metadata),
                    ],
                    "offline construction, public signature, exact merge and refusal checks",
                )
                raw = (files / "final.tx").read_bytes()
                psbt = base64.b64encode((files / "unsigned.psbt").read_bytes()).decode()
                # Deliberate post-offline comparison through the separate controller.
                prev = node.call("getblock", dict(blockhash=origin, verbosity=2))["tx"][
                    0
                ]
                unsigned = node.call("decodepsbt", dict(psbt=psbt))
                final = node.call("decoderawtransaction", dict(hexstring=raw.hex()))
                assert len(unsigned["tx"]["vin"]) == len(final["vin"]) == 1
                assert len(unsigned["tx"]["vout"]) == len(final["vout"]) == 2
                assert (
                    unsigned["tx"]["vin"][0]["txid"]
                    == final["vin"][0]["txid"]
                    == prev["txid"]
                )
                for key in ("txid", "vout", "sequence"):
                    assert unsigned["tx"]["vin"][0][key] == final["vin"][0][key]
                assert unsigned["tx"]["vout"] == final["vout"]
                assert unsigned["fee"] == Decimal("0.00001200")
                assert (
                    final["vout"][0]["value"] == Decimal("0.00123456")
                    and final["vout"][0]["scriptPubKey"]["address"] == recipient
                )
                assert final["vin"][0]["txinwitness"]
                change_info = node.call(
                    "getaddressinfo", dict(address=change_address), wallet=wallet
                )
                assert (
                    final["vout"][1]["scriptPubKey"]["address"] == change_address
                    and change_info["ischange"]
                )
                for derivation, info in (
                    (unsigned["inputs"][0]["bip32_derivs"][0], input_origin),
                    (unsigned["outputs"][1]["bip32_derivs"][0], change_info),
                ):
                    assert derivation["pubkey"] == info["pubkey"]
                    assert (
                        derivation["master_fingerprint"] == info["hdmasterfingerprint"]
                    )
                    assert derivation["path"].replace("h", "'") == info[
                        "hdkeypath"
                    ].replace("h", "'")
                artifacts = out / "public-vectors"
                artifacts.mkdir()
                fixture_refs = {}
                for fixture in [*sorted(files.glob("*.psbt")), files / "final.tx"]:
                    raw_fixture = fixture.read_bytes()
                    (artifacts / fixture.name).write_bytes(raw_fixture)
                    fixture_refs[fixture.name] = dict(
                        bytes=len(raw_fixture),
                        sha256=hashlib.sha256(raw_fixture).hexdigest(),
                    )
                node.call("sendrawtransaction", [alternative])
                assert (
                    node.call(
                        "gettxout", dict(txid=prev["txid"], n=final["vin"][0]["vout"])
                    )
                    is None
                )
                spent = run(
                    [str(args.probe), "spent", *online],
                    "deliberate fresh read rejects older spent pin",
                )
                assert not proxy.failure
                observed = dict(
                    cache=cache,
                    verified=verified,
                    spent=spent,
                    exactDecodedTxid=final["txid"],
                    feeSat=1200,
                    recipientSat=123456,
                    offlineApplicationRequests=0,
                    unsafeChangesRejected=True,
                    controllerOnlyMutations=True,
                    controllerSpentAlternate=True,
                    matchingDaemonSerialization=True,
                    independentInputChangeOrigins=True,
                    fixtureArtifacts=fixture_refs,
                )
            finally:
                proxy.close()
    except BaseException as error:
        failed = str(error)
        (out / "failure.txt").write_text(traceback.format_exc())
    proc_file = out / "display/processes.json"
    processes = json.loads(proc_file.read_text()) if proc_file.exists() else {}
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
    record = dict(
        startedUTC=start,
        completedUTC=now(),
        commands=commands,
        binary=binary_identity(args.binary),
        probe=binary_identity(args.probe),
        offlineFilter=binary_identity(args.offline_exec),
        daemonProcess=node.record if node else None,
        native=native,
        observed=observed,
        settlement=settled,
        applicationReadRequests=proxy.events if proxy else [],
        failure=failed,
    )
    write_json(out / "report.json", record)
    print(
        json.dumps(
            {
                key: record[key]
                for key in ("native", "observed", "settlement", "failure")
            }
        )
    )
    return int(failed is not None or not all(settled.values()))


if __name__ == "__main__":
    raise SystemExit(main())
