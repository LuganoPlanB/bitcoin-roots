# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Drive lossless offline transaction import and native inspection with INET denied."""

import argparse
import base64
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import traceback

sys.dont_write_bytecode = True
tests = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(tests))
sys.path.insert(0, str(tests / "platform"))
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)
from capture_manifest import encode_capture


def now():
    return datetime.now(timezone.utc).isoformat()


def v2():
    # BIP370 globals: v2, txversion 2, 1 input and 1 output. The output is a
    # one-satoshi P2WPKH script. The uninterpreted entries stay byte-identical.
    return bytes.fromhex(
        "70736274ff01fb040200000001020402000000010401010105010104fc01610003beef0100"
        "010e20" + "11" * 32 + "010f040000000000"
        "01030801000000000000000104160014751e76e8199196d454941c45d1b3a323f1433bd600"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("binary", "offline-exec", "fonts", "output"):
        parser.add_argument("--" + option, type=Path, required=True)
    args = parser.parse_args()
    out = Path(tempfile.mkdtemp(prefix="attempt-", dir=evidence_directory(args.output)))
    start = now()
    scratch = command = exit_code = restart = result = failure = None
    observed = {}
    origin = v2()
    try:
        with tempfile.TemporaryDirectory(
            prefix="offline-document-", dir=validated_scratch()
        ) as scratch:
            files = Path(scratch)
            (files / "binary-with-text-extension.txt").write_bytes(origin)
            (files / "existing.psbt").write_bytes(b"sentinel")
            raw = (
                "  02000000000101" + "11" * 32 + "0000000000feffffff010100000000000000"
                "160014751e76e8199196d454941c45d1b3a323f1433bd601010100000000\r\n"
            ).encode("ascii")
            (files / "raw.txt").write_bytes(raw)
            (files / "future.txt").write_bytes(
                base64.b64encode(bytes.fromhex("70736274ff01fb040300000000"))
            )
            command = [
                sys.executable,
                "-B",
                str(tests / "platform/owned_display.py"),
                "--output",
                str(out / "display"),
                "--timeout",
                "52",
                "--",
                str(args.offline_exec.resolve()),
                "--unix-only",
                str(args.binary.resolve()),
                str(out / "captures"),
                str(args.fonts.resolve()),
                scratch,
            ]
            with (out / "native.log").open("w") as log:
                exit_code = subprocess.run(
                    command, stdout=log, stderr=subprocess.STDOUT, timeout=64
                ).returncode
            assert exit_code == 0, "owned native driver did not complete"
            result = json.loads((out / "captures/result.json").read_text())
            assert (
                result["executed"] == result["passed"] == 1
                and result["teardown"]
                and result["rpcPorts"] == 0
            )
            assert (files / "saved.psbt").read_bytes() == origin and (
                files / "existing.psbt"
            ).read_bytes() == b"sentinel"
            selected = (files / "wire-selection.bin").read_bytes()
            assert selected == bytes.fromhex(raw.decode())[49:57] == b"\x01" + bytes(7)
            assert (files / "wire-selection.bin").stat().st_mode & 0o777 == 0o600
            assert (
                out / "captures/copied-hex.txt"
            ).read_text().strip() == selected.hex()
            (out / "saved-wire-selection.bin").write_bytes(selected)
            (out / "original-raw-fixture.txt").write_bytes(raw)
            (out / "original-psbt-fixture.bin").write_bytes(origin)
            restart_argv = [
                str(args.offline_exec.resolve()),
                str(args.binary.resolve()),
                "--restore",
                scratch,
            ]
            restart_start = now()
            restored = subprocess.run(
                restart_argv, capture_output=True, text=True, timeout=10
            )
            restart = dict(
                argv=restart_argv,
                cwd=str(Path.cwd()),
                startedUTC=restart_start,
                completedUTC=now(),
                exit=restored.returncode,
                stdout=restored.stdout,
                stderr=restored.stderr,
            )
            assert restored.returncode == 0 and not restored.stderr
            assert json.loads(restored.stdout) == dict(
                exactPublicDocumentsRestored=2,
                savedActiveRepresentation=True,
                rpcPorts=0,
            )
            observed = dict(
                exactOriginalPSBT=hashlib.sha256(origin).hexdigest(),
                exactRawSource=hashlib.sha256(raw).hexdigest(),
                noOverwrite=True,
                rpcPorts=0,
                inetDenied=True,
                restoredOriginalDocuments=2,
                exactSelectedBytes=selected.hex(),
                selectionOffset=49,
                selectionLength=8,
            )
            observed["captures"] = {
                name: encode_capture(out / "captures" / (name + ".ppm"))
                for name in (
                    "workbench-overview",
                    "workbench-input",
                    "workbench-output",
                    "workbench-hex",
                )
            }
    except BaseException as exception:
        failure = str(exception)
        (out / "failure.txt").write_text(traceback.format_exc())
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
        metadata=bool(scratch and not Path(scratch).exists()),
    )
    report = dict(
        argv=command,
        cwd=str(Path.cwd()),
        startUTC=start,
        endUTC=now(),
        exit=exit_code,
        binary=binary_identity(args.binary),
        offlineFilter=binary_identity(args.offline_exec),
        native=result,
        observed=observed,
        settlement=settled,
        restart=restart,
        failure=failure,
    )
    write_json(out / "report.json", report)
    print("Native document evidence:", out)
    print(
        json.dumps(
            {
                key: report[key]
                for key in ("exit", "native", "observed", "settlement", "failure")
            }
        )
    )
    return int(failure is not None or not all(settled.values()))


if __name__ == "__main__":
    raise SystemExit(main())
