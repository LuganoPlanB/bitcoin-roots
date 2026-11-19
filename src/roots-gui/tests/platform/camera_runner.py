# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Owned X11 NativeHost with a controlled V4L2 syscall port and real QR pixels."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

TESTS = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(TESTS), str(TESTS / "platform")]
from capture_manifest import encode_capture
from protocol.qr_exchange import ADDRESS, URI, systematic
from protocol.signature_exchange import now
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)

FOREIGN = "1BoatSLRHtKNngkdXEeobR76b53LETtpyT"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("binary", "fonts", "offline-exec", "vector", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    args = parser.parse_args()
    out = Path(tempfile.mkdtemp(prefix="attempt-", dir=evidence_directory(args.output)))
    print("Camera native evidence:", out, flush=True)
    started, record = now(), {}
    exit_code = 1
    try:
        with tempfile.TemporaryDirectory(
            prefix="camera-journey-", dir=validated_scratch()
        ) as folder:
            fixture = Path(folder)
            psbt = bytes.fromhex(args.vector.read_text())
            _, _, parts = systematic(psbt, 100)
            _, _, other = systematic(psbt[:-1] + bytes([psbt[-1] ^ 1]), 100)
            assert len(parts) == len(other) == 2
            inputs = {
                "public.psbt": psbt,
                "address.txt": ADDRESS.encode(),
                "uri.txt": URI.encode(),
                "foreign.txt": FOREIGN.encode(),
                "part-first.txt": parts[0].encode(),
                "part-mixed.txt": other[0].encode(),
                "part-last.txt": parts[1].encode(),
            }
            raw = out / "public-fixtures"
            raw.mkdir()
            for name, payload in inputs.items():
                (fixture / name).write_bytes(payload)
                (raw / name).write_bytes(payload)
            argv = [
                sys.executable,
                "-B",
                str(TESTS / "platform/owned_display.py"),
                "--timeout",
                "33",
                "--output",
                str(out / "display"),
                "--",
                str(args.offline_exec),
                "--unix-only",
                str(args.binary),
                str(out),
                str(args.fonts),
                str(fixture),
            ]
            native_started = now()
            process = subprocess.run(argv, capture_output=True, timeout=43)
            record.update(
                argv=argv,
                cwd=str(Path.cwd()),
                nativeStartedUTC=native_started,
                nativeCompletedUTC=now(),
                exit=process.returncode,
            )
            (out / "stdout").write_bytes(process.stdout)
            (out / "stderr").write_bytes(process.stderr)
            assert process.returncode == 0
            display = json.loads((out / "display/processes.json").read_text())
            result = json.loads((out / "native-result.json").read_text())
            assert display["child"]["settled"] and display["child"]["exit"] == 0
            assert all(
                row["settled"] and not row.get("remainingLiveGroupMembers")
                for row in display["processes"]
            )
            assert (
                result["settled"]
                and result["networkDenied"]
                and result["controlledSyscalls"]
            )
            names = (
                "camera-permission-denied",
                "qr-scan-error",
                "qr-scan",
                "camera-mixed-fragments",
            )
            record.update(
                native=result,
                captures={
                    name: encode_capture(out / (name + ".ppm")) for name in names
                },
            )
            exit_code = 0
    finally:
        record.update(
            binary=binary_identity(args.binary),
            scratchRemoved=not Path(folder).exists(),
            startedUTC=started,
            completedUTC=now(),
            selected=1,
            executed=1,
            skipped=0,
            passed=int(exit_code == 0),
            failed=int(exit_code != 0),
        )
        write_json(out / "report.json", record)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
