# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Create and exercise independent 2-of-3 PSBT exchange fixtures."""

import argparse
import base64
from datetime import datetime, timezone
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
from shutil import copyfile
import subprocess
import sys
import tempfile
import threading
from urllib.parse import unquote

TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))
sys.path.insert(0, str(TESTS / "platform"))
from capture_manifest import encode_capture
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)

KEYS = [
    bytes.fromhex(key)
    for key in (
        "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798",
        "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5",
        "02f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f9",
    )
]
ORDER = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
OPAQUE = b"\xfc\x05roots\x02"
GENESIS = "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"
WALLET = "Offline multisig"


def integer(n, width=4):
    return n.to_bytes(width, "little")


def compact(n):
    assert 0 <= n <= 65535
    return bytes([n]) if n < 253 else b"\xfd" + integer(n, 2)


def vector(data):
    return compact(len(data)) + data


def sha2(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()


def wire_facts(wire):
    """Independently retain scriptSigs while excluding marker, flag and witnesses for txid."""
    position = 4
    witnessed = wire[position : position + 2] == b"\0\x01"
    if witnessed:
        position += 2
    body = position

    def count():
        nonlocal position
        value = wire[position]
        position += 1
        if value >= 253:
            width = 2 ** (value - 252)
            value = int.from_bytes(wire[position : position + width], "little")
            position += width
        return value

    def skip_vector():
        nonlocal position
        size = count()
        position += size

    inputs = count()
    for _ in range(inputs):
        position += 36
        skip_vector()
        position += 4
    outputs = count()
    for _ in range(outputs):
        position += 8
        skip_vector()
    witness = position
    if witnessed:
        for _ in range(inputs):
            for _ in range(count()):
                skip_vector()
    assert position + 4 == len(wire)
    stripped = wire[:4] + wire[body:witness] + wire[position:]
    weight = len(stripped) * 3 + len(wire)
    return dict(
        txid=sha2(stripped)[::-1].hex(),
        wtxid=sha2(wire)[::-1].hex(),
        size=len(wire),
        stripped=len(stripped),
        weight=weight,
        vsize=(weight + 3) // 4,
    )


def transaction(inputs, outputs, script_index=None, redeem=b""):
    return (
        integer(2)
        + compact(len(inputs))
        + b"".join(
            point + vector(redeem if i == script_index else b"") + integer(0xFFFFFFFD)
            for i, point in enumerate(inputs)
        )
        + compact(len(outputs))
        + b"".join(integer(value, 8) + vector(script) for value, script in outputs)
        + integer(0)
    )


def record(key, value):
    return vector(key) + vector(value)


def packed(rows):
    return b"psbt\xff" + b"".join(
        b"".join(record(k, v) for k, v in row.items()) + b"\x00" for row in rows
    )


def der_number(n):
    raw = n.to_bytes((n.bit_length() + 7) // 8, "big")
    if raw[0] & 0x80:
        raw = b"\x00" + raw
    return b"\x02" + bytes([len(raw)]) + raw


def sign(openssl, file, digest):
    result = subprocess.run(
        [str(openssl), "pkeyutl", "-sign", "-inkey", str(file), "-keyform", "DER"],
        input=digest,
        capture_output=True,
        timeout=7,
        check=True,
    )
    der = result.stdout
    assert der[0] == 0x30 and der[2] == 2 and der[4 + der[3]] == 2
    at = 4 + der[3]
    r = int.from_bytes(der[4:at], "big")
    s = int.from_bytes(der[at + 2 :], "big")
    encoded = der_number(r) + der_number(min(s, ORDER - s))
    return b"\x30" + bytes([len(encoded)]) + encoded + b"\x01"


def make_fixtures(directory, openssl):
    redeem = b"\x52" + b"".join(vector(key) for key in KEYS) + b"\x53\xae"
    root = (
        b"\xa9\x14"
        + hashlib.new("ripemd160", hashlib.sha256(redeem).digest()).digest()
        + b"\x87"
    )
    previous = [
        transaction([bytes([i]) * 32 + integer(0)], [(amount, root)])
        for i, amount in enumerate((4000000, 3000000), 1)
    ]
    points = [sha2(tx) + integer(0) for tx in previous]
    outputs = [
        (1234567, b"\x00\x14" + b"\x71" * 20),
        (5764933, b"\x00\x14" + b"\x73" * 20),
    ]
    assert sum(amount for amount, _ in outputs) == 6999500
    with tempfile.TemporaryDirectory(
        prefix="fixture-keys-", dir=validated_scratch()
    ) as secrets:
        keys = []
        for i in range(1, 4):
            file = Path(secrets) / f"{i}.der"
            file.touch(mode=0o600)
            # Temporary public-test scalar in a SEC1 envelope, never an application input.
            file.write_bytes(
                bytes.fromhex("302e0201010420")
                + i.to_bytes(32, "big")
                + bytes.fromhex("a00706052b8104000a")
            )
            keys.append(file)
        digest = [
            sha2(transaction(points, outputs, i, redeem) + integer(1)) for i in range(2)
        ]
        signed = {
            (i, k): sign(openssl, keys[k], digest[i])
            for i, k in ((0, 0), (0, 1), (1, 0), (1, 2))
        }
    scripts = [
        b"\0"
        + vector(signed[i, 0])
        + vector(signed[i, k])
        + b"\x4c"
        + bytes([len(redeem)])
        + redeem
        for i, k in ((0, 1), (1, 2))
    ]
    expected = (
        integer(2)
        + compact(2)
        + b"".join(
            point + vector(script) + integer(0xFFFFFFFD)
            for point, script in zip(points, scripts)
        )
        + compact(2)
        + b"".join(integer(amount, 8) + vector(script) for amount, script in outputs)
        + integer(0)
    )
    for version in (0, 2):
        folder = directory / f"v{version}"
        folder.mkdir()
        if version == 0:
            globals_ = {b"\x00": transaction(points, outputs)}
        else:
            globals_ = {
                b"\x02": integer(2),
                b"\x03": integer(0),
                b"\x04": compact(2),
                b"\x05": compact(2),
                b"\xfb": integer(2),
            }
        unsigned = [dict(globals_)] + [
            {
                b"\x00": prior,
                b"\x04": redeem,
                OPAQUE: b"original input " + bytes([65 + i]),
            }
            for i, prior in enumerate(previous)
        ]
        if version == 2:
            for row, point in zip(unsigned[1:], points):
                row.update(
                    {
                        b"\x0e": point[:32],
                        b"\x0f": integer(0),
                        b"\x10": integer(0xFFFFFFFD),
                    }
                )
        for i, (amount, script) in enumerate(outputs):
            row = {OPAQUE: b"original output " + bytes([65 + i])}
            if version == 2:
                row.update({b"\x03": integer(amount, 8), b"\x04": script})
            unsigned.append(row)
        partial = [dict(row) for row in unsigned]
        for i, k in ((0, 0), (0, 1), (1, 0)):
            partial[i + 1][b"\x02" + KEYS[k]] = signed[i, k]
        complete = [dict(row) for row in partial]
        complete[2][b"\x02" + KEYS[2]] = signed[1, 2]
        complete[2][OPAQUE + b"\xa0"] = b"external signer opaque material"
        daemon = [dict(row) for row in unsigned]
        airgap = [dict(row) for row in unsigned]
        for i, k in ((0, 1), (1, 2)):
            daemon[i + 1][b"\x02" + KEYS[0]] = signed[i, 0]
            airgap[i + 1][b"\x02" + KEYS[k]] = signed[i, k]
        airgap[2][OPAQUE + b"\xa0"] = complete[2][OPAQUE + b"\xa0"]
        daemon_conflict = [dict(row) for row in daemon]
        daemon_conflict[2][OPAQUE + b"\xa0"] = b"different verified reply metadata"
        invalid = [dict(row) for row in partial]
        invalid[2][b"\x02" + KEYS[0]] = signed[0, 0]
        conflicting = [dict(row) for row in complete]
        conflicting[2][OPAQUE] = b"conflicting existing opaque map value"
        wrong = [dict(row) for row in complete]
        if version == 0:
            wrong[0][b"\x00"] = transaction(
                points, [(outputs[0][0] + 1, outputs[0][1]), outputs[1]]
            )
        else:
            wrong[3][b"\x03"] = integer(outputs[0][0] + 1, 8)
        unknown = [dict(row) for row in unsigned]
        unknown[2].pop(b"\x00")
        for name, rows in (
            ("original", unsigned),
            ("partial", partial),
            ("complete", complete),
            ("invalid", invalid),
            ("conflicting", conflicting),
            ("wrong", wrong),
            ("unknown", unknown),
            ("daemon", daemon),
            ("airgap", airgap),
            ("daemon-conflict", daemon_conflict),
        ):
            (folder / f"{name}.psbt").write_bytes(packed(rows))
        duplicate = (
            packed(partial[:1]) + record(b"\x00", previous[0]) + packed(partial[1:])[5:]
        )
        (folder / "duplicate.psbt").write_bytes(duplicate)
        (folder / "descriptor.txt").write_text(
            "sh(multi(2," + ",".join(k.hex() for k in KEYS) + "))"
        )
        for i, prior in enumerate(previous):
            (folder / f"previous-{i}.hex").write_text(prior.hex())
        (folder / "redeem.hex").write_text(redeem.hex())
        (folder / "expected.tx").write_bytes(expected)


def now():
    return datetime.now(timezone.utc).isoformat()


def run_mixed_signer(args, out, fixtures):
    original = base64.b64encode((fixtures / "v2/original.psbt").read_bytes()).decode()
    signed = base64.b64encode((fixtures / "v2/daemon.psbt").read_bytes()).decode()
    cookie = fixtures / "fixture.cookie"
    port = fixtures / "loopback-port.txt"
    cookie.touch(mode=0o600, exist_ok=False)
    cookie.write_text("fixture:loopback-cosigner")
    auth = "Basic " + base64.b64encode(cookie.read_bytes()).decode()
    calls = []
    failures = []

    class Signer(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            try:
                assert (
                    self.client_address[0] == "127.0.0.1"
                    and self.headers["Authorization"] == auth
                )
                length = int(self.headers["Content-Length"])
                assert 0 < length < 65536
                request = json.loads(self.rfile.read(length))
                assert request["jsonrpc"] == "2.0"
                method = request["method"]
                path = unquote(self.path)
                params = request["params"]
                if method == "getblockhash":
                    assert path == "/" and params == [0]
                    result = GENESIS
                elif method == "getwalletinfo":
                    assert path == "/wallet/" + WALLET and params == []
                    result = dict(
                        walletname=WALLET,
                        descriptors=True,
                        private_keys_enabled=True,
                        external_signer=False,
                    )
                elif method == "walletprocesspsbt":
                    assert path == "/wallet/" + WALLET
                    assert params == dict(
                        psbt=original, sign=True, finalize=False, bip32derivs=False
                    )
                    result = dict(psbt=signed, complete=False)
                else:
                    raise AssertionError("unexpected method")
                calls.append(method)
                response = json.dumps(
                    dict(jsonrpc="2.0", id=request["id"], result=result)
                ).encode()
                self.send_response(200)
                self.send_header("Content-Length", str(len(response)))
                self.end_headers()
                self.wfile.write(response)
            except (AssertionError, ValueError, KeyError) as error:
                failures.append(type(error).__name__)
                self.send_error(400)

    native = out / "mixed-signer"
    native.mkdir()
    argv = [
        sys.executable,
        "-B",
        str(TESTS / "platform/owned_display.py"),
        "--output",
        str(native / "display"),
        "--timeout",
        "25",
        "--",
        str(args.native),
        str(native),
        str(args.fonts),
        str(fixtures / "v2"),
        "--mixed-online",
    ]
    start = now()
    with ThreadingHTTPServer(("127.0.0.1", 0), Signer) as server:
        port.write_text(str(server.server_port))
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        try:
            run = subprocess.run(argv, text=True, capture_output=True, timeout=32)
        finally:
            server.shutdown()
            thread.join(timeout=5)
            port.unlink()
            cookie.unlink()
    (native / "output.log").write_text(run.stdout + run.stderr)
    observed = dict(
        argv=argv,
        cwd=str(Path.cwd()),
        startedUTC=start,
        completedUTC=now(),
        exit=run.returncode,
        orderedMethods=calls,
        fixtureFailures=failures,
        serverSettled=not thread.is_alive(),
    )
    write_json(native / "rpc-result.json", observed)
    assert run.returncode == 0 and not failures and observed["serverSettled"]
    assert calls == ["getblockhash", "getwalletinfo", "walletprocesspsbt"] * 2
    orders = {
        order: json.loads((native / order / "native-result.json").read_text())
        for order in ("airgap-first", "daemon-first")
    }
    assert all(
        group["settled"] and group["productionSignerPoll"] and group["checks"] > 0
        for group in orders.values()
    )
    return observed | {"orders": orders}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ("openssl", "offline-exec", "output"):
        p.add_argument("--" + key, type=Path, required=True)
    for key in ("probe", "native", "fonts"):
        p.add_argument("--" + key, type=Path)
    p.add_argument("--finalize", action="store_true")
    p.add_argument("--daemon", type=Path)
    args = p.parse_args()
    assert bool(args.probe) != bool(args.native)
    assert not args.finalize or args.native
    assert not args.daemon or args.finalize
    out = Path(tempfile.mkdtemp(prefix="attempt-", dir=evidence_directory(args.output)))
    start = now()
    with tempfile.TemporaryDirectory(
        prefix="signature-exchange-", dir=validated_scratch()
    ) as private:
        fixtures = Path(private)
        make_fixtures(fixtures, args.openssl)
        if args.probe:
            argv = [str(args.offline_exec), str(args.probe), str(fixtures)]
        else:
            argv = [
                sys.executable,
                "-B",
                str(TESTS / "platform/owned_display.py"),
                "--output",
                str(out / "display"),
                "--timeout",
                "30",
                "--",
                str(args.offline_exec),
                "--unix-only",
                str(args.native),
                str(out),
                str(args.fonts),
                str(fixtures / "v2"),
            ]
            if args.finalize:
                argv += ["--finalize"]
        completed = subprocess.run(argv, text=True, capture_output=True, timeout=40)
        (out / "output.log").write_text(completed.stdout + completed.stderr)
        mixed_signer = (
            run_mixed_signer(args, out, fixtures)
            if args.finalize and completed.returncode == 0
            else None
        )
        retained = out / "public-fixtures"
        for folder in sorted(fixtures.iterdir()):
            target = retained / folder.name
            target.mkdir(parents=True)
            for artifact in folder.iterdir():
                copyfile(artifact, target / artifact.name)
    result = dict(
        startedUTC=start,
        completedUTC=now(),
        argv=argv,
        cwd=str(Path.cwd()),
        exit=completed.returncode,
        scratchRemoved=not Path(private).exists(),
        binary=binary_identity(args.probe or args.native),
        openssl=binary_identity(args.openssl),
    )
    if completed.returncode == 0:
        if args.probe:
            result["cases"] = json.loads(completed.stdout)
        else:
            display = json.loads((out / "display/processes.json").read_text())
            assert display["child"]["settled"] and all(
                p["settled"] and not p.get("remainingLiveGroupMembers")
                for p in display["processes"]
            )
            result.update(
                native=json.loads((out / "native-result.json").read_text()),
                displaySettled=True,
            )
            captures = (
                ("finalized-transaction",)
                if args.finalize
                else ("signing-partial", "signing-import-rejected")
            )
            result["captures"] = {
                name: encode_capture(out / (name + ".ppm")) for name in captures
            }
            if args.finalize:
                result["productionMixedSigner"] = mixed_signer
                result["mixedOrders"] = {
                    order: json.loads((out / order / "native-result.json").read_text())
                    for order in ("airgap-first", "daemon-first")
                }
                assert all(
                    group["settled"] and group["networkDenied"] and group["checks"] > 0
                    for group in result["mixedOrders"].values()
                )
                final = out / "final.tx"
                serialized = final.read_bytes()
                assert serialized == (retained / "v2/expected.tx").read_bytes()
                oracle = wire_facts(serialized)
                assert all(
                    result["native"]["final"][key] == value
                    for key, value in oracle.items()
                )
                assert (
                    hashlib.sha256(serialized).hexdigest()
                    == result["native"]["final"]["sha256"]
                )
                result["independentWireFacts"] = oracle
                if args.daemon:
                    from integration.owned_regtest import OwnedRegtest

                    with OwnedRegtest(args.daemon, out / "decode") as daemon:
                        daemon.ready()
                        decoded = daemon.call(
                            "decoderawtransaction", [serialized.hex()]
                        )
                    result["rootsDecoded"] = {
                        key: decoded["hash" if key == "wtxid" else key]
                        for key in ("txid", "wtxid", "size", "weight", "vsize")
                    }
                    assert result["rootsDecoded"] == {
                        key: oracle[key] for key in result["rootsDecoded"]
                    }
        result.update(
            selected=2 if args.probe else 1,
            executed=2 if args.probe else 1,
            passed=2 if args.probe else 1,
            failed=0,
            skipped=0,
        )
    write_json(out / "report.json", result)
    print("Signature exchange evidence:", out)
    print(
        json.dumps(
            {
                key: result[key]
                for key in ("selected", "executed", "passed", "failed", "skipped")
                if key in result
            }
        )
    )
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
