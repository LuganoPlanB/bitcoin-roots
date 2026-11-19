# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Run the native renderer on bounded views of a large synthetic public wallet."""

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

TESTS = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(TESTS), str(TESTS / "platform")]
from capture_manifest import encode_capture
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)
from rpc.fake_http import FakeHttp, Step


def now():
    return datetime.now(timezone.utc).isoformat()


def compact(value):
    if value < 253:
        return bytes([value])
    return b"\xfd" + value.to_bytes(2, "little")


def vector(payload):
    return compact(len(payload)) + payload


def address(i):
    hrp = "bcrt"
    alphabet = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"
    data = [0]
    value = int.from_bytes(i.to_bytes(20, "big"), "big")
    data.extend((value >> shift) & 31 for shift in range(155, -1, -5))
    remainder = 1
    generators = (0x3B6A57B2, 0x26508E6D, 0x1EA119FA, 0x3D4233DD, 0x2A1462B3)
    for n in (
        [ord(c) >> 5 for c in hrp] + [0] + [ord(c) & 31 for c in hrp] + data + [0] * 6
    ):
        prefix = remainder >> 25
        remainder = ((remainder & 0x1FFFFFF) << 5) ^ n
        for bit, poly in enumerate(generators):
            if prefix & (1 << bit):
                remainder ^= poly
    checksum = remainder ^ 1
    return (
        hrp
        + "1"
        + "".join(
            alphabet[n]
            for n in data + [(checksum >> shift) & 31 for shift in range(25, -1, -5)]
        )
    )


def file_identity(path):
    with path.open("rb") as stream:
        sha = hashlib.file_digest(stream, "sha256").hexdigest()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=sha)


def fixtures(folder):
    folder.mkdir()
    with (folder / "payments.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("Address", "Amount", "Label", "Unit"))
        for i in range(1000):
            writer.writerow(
                (address(i + 1), str(1000 + i), f"Public recipient {i + 1}", "sat")
            )
    inputs, outputs = 512, 768
    transaction = (
        (2).to_bytes(4, "little")
        + compact(inputs)
        + b"".join(
            (i + 1).to_bytes(32, "little") + b"\0" * 5 + b"\xff" * 4
            for i in range(inputs)
        )
        + compact(outputs)
        + b"".join(
            (i + 1000).to_bytes(8, "little")
            + vector(b"\x00\x14" + bytes([i % 256]) * 20)
            for i in range(outputs)
        )
        + b"\0" * 4
    )
    psbt = (
        b"psbt\xff"
        + vector(b"\0")
        + vector(transaction)
        + vector(b"\x70")
        + vector(b"P" * 14000)
        + b"\0" * (1 + inputs + outputs)
    )
    assert 48000 < len(psbt) <= 65536
    (folder / "large.psbt").write_bytes(psbt)
    return dict(
        inputs=inputs,
        outputs=outputs,
        bytes=len(psbt),
        sha256=hashlib.sha256(psbt).hexdigest(),
        batchRows=1000,
    )


def distribution(samples):
    ordered = sorted(samples)
    if not ordered:
        return {}

    def percentile(p):
        return ordered[max(0, math.ceil(len(ordered) * p) - 1)] / 1e6

    return dict(
        samples=len(ordered),
        minMs=ordered[0] / 1e6,
        p50Ms=percentile(0.50),
        p95Ms=percentile(0.95),
        p99Ms=percentile(0.99),
        worstMs=ordered[-1] / 1e6,
    )


def read_profiles(output):
    groups = {}
    with (output / "frames.csv").open() as stream:
        for row in csv.DictReader(stream):
            groups.setdefault(row.pop("phase"), []).append(
                {k: int(v) for k, v in row.items()}
            )
    return {
        phase: dict(
            metrics={
                key: distribution(
                    [
                        row[key]
                        for row in rows
                        if row.get("phase_frame", 25) >= 25 and row[key] > 0
                    ]
                )
                for key in ("ui_ns", "idle_to_render_ns", "previous_frame_interval_ns")
            },
            coldMetrics={
                key: distribution(
                    [
                        row[key]
                        for row in rows
                        if row.get("phase_frame", 25) < 25 and row[key] > 0
                    ]
                )
                for key in ("ui_ns", "idle_to_render_ns", "previous_frame_interval_ns")
            },
            maxWork={
                key: max(row[key] for row in rows)
                for key in rows[0]
                if not key.endswith("_ns") and key not in ("frame", "phase_frame")
            },
        )
        for phase, rows in groups.items()
    }


def admission(binary, out):
    def inventory_params(path, request, _):
        assert path == "/wallet/large-wallet"
        assert request["params"]["query_options"]["maximumCount"] == 8193

    tip = "b" * 64
    genesis = "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"
    # The same 100k population produces a server result honoring the requested
    # one-extra-row sentinel instead of sending the whole inventory.
    source = [
        dict(
            txid=f"{n // 1000 + 1:064x}",
            vout=n % 1000,
            amount=1,
            confirmations=2,
            safe=True,
            solvable=True,
            spendable=False,
            scriptPubKey="0014" + "00" * 20,
        )
        for n in range(100000)
    ]
    steps = [
        Step("listunspent", source[:8193], request_check=inventory_params),
        *[
            Step(
                "gettransaction",
                dict(
                    txid=f"{n:064x}",
                    amount=1,
                    time=1700000000,
                    confirmations=2,
                    details=[dict(category="receive", vout=0, amount=1)],
                ),
            )
            for n in range(1, 10)
        ],
        Step(
            "getbalances",
            dict(mine=dict(trusted=1, untrusted_pending=0, immature=0, nonmempool=0)),
        ),
        Step("getblockchaininfo", dict(blocks=900, bestblockhash=tip)),
        Step("getblockhash", genesis),
    ]
    with (
        tempfile.TemporaryDirectory(
            prefix="dense-bounds-", dir=validated_scratch()
        ) as scratch,
        FakeHttp(steps) as server,
    ):
        cookie = Path(scratch) / "cookie"
        cookie.write_text("fixture:" + server.secret)
        cookie.chmod(0o600)
        argv = [str(binary), "--admission", str(server.port), str(cookie)]
        start = now()
        child = subprocess.run(argv, capture_output=True, text=True, timeout=24)
        end = now()
        accepted = (
            child.returncode == 0
            and len(server.records) == len(steps)
            and not server.check_failures
        )
    settled = (
        not server.thread.is_alive()
        and not server.active
        and not Path(scratch).exists()
    )
    write_json(
        out / "admission.json",
        dict(
            argv=argv,
            cwd=str(Path.cwd()),
            startedUTC=start,
            completedUTC=end,
            exit=child.returncode,
            sourceCoins=len(source),
            stdout=child.stdout,
            stderr=child.stderr,
            requests=server.records,
            checkFailures=server.check_failures,
            settled=settled,
            passed=bool(accepted and settled),
        ),
    )
    return bool(accepted and settled)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("binary", "fonts", "output"):
        parser.add_argument("--" + flag, type=Path, required=True)
    parser.add_argument("--admission", action="store_true")
    args = parser.parse_args()
    out = Path(
        tempfile.mkdtemp(prefix="large-wallet-", dir=evidence_directory(args.output))
    )
    if args.admission:
        print(out)
        return int(not admission(args.binary, out))
    fixture = fixtures(out / "public-fixtures")
    argv = [
        sys.executable,
        "-B",
        str(TESTS / "platform/owned_display.py"),
        "--output",
        str(out / "desktop"),
        "--timeout",
        "55",
        "--",
        str(args.binary),
        str(out),
        str(args.fonts),
        str(out / "public-fixtures"),
    ]
    start = now()
    with (out / "raw.log").open("w") as log:
        child = subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT, timeout=65)
    native_path = out / "result.json"
    captures = {
        image.stem: encode_capture(image) for image in sorted(out.glob("*.ppm"))
    }
    feedback = {}
    if (out / "feedback.csv").exists():
        with (out / "feedback.csv").open() as stream:
            for row in csv.DictReader(stream):
                feedback.setdefault(row["event"], []).append(int(row["nanoseconds"]))
    desktop = json.loads((out / "desktop/processes.json").read_text())
    settled = desktop["child"]["settled"] and all(
        row["settled"] and not row.get("remainingLiveGroupMembers")
        for row in desktop["processes"]
    )
    write_json(
        out / "report.json",
        dict(
            startedUTC=start,
            completedUTC=now(),
            cwd=str(Path.cwd()),
            argv=argv,
            exit=child.returncode,
            binary=binary_identity(args.binary),
            fontInputs=[
                file_identity(p) for p in sorted(args.fonts.glob("*")) if p.is_file()
            ],
            fixture=fixture,
            native=json.loads(native_path.read_text())
            if native_path.exists()
            else None,
            captures=captures,
            profiles=read_profiles(out) if (out / "frames.csv").exists() else {},
            feedback={name: distribution(values) for name, values in feedback.items()},
            settled=settled,
            selected=1,
            executed=1,
            passed=int(not child.returncode and settled),
            failed=int(bool(child.returncode) or not settled),
            skipped=0,
        ),
    )
    print(out)
    return int(bool(child.returncode) or not settled)


if __name__ == "__main__":
    raise SystemExit(main())
