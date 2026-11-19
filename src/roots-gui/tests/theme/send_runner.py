# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Capture the production recipient workflow on an owned native display."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))
sys.path.insert(0, str(TESTS / "platform"))
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)
from capture_manifest import encode_capture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("binary", "fonts", "offline-exec", "output"):
        parser.add_argument("--" + key, required=True, type=Path)
    parser.add_argument("--funding", action="store_true")
    args = parser.parse_args()
    validated_scratch()
    out = Path(tempfile.mkdtemp(prefix="attempt-", dir=evidence_directory(args.output)))
    captures = out / "captures"
    captures.mkdir(exist_ok=False)
    runner = TESTS / "platform/owned_display.py"
    command = [
        sys.executable,
        "-B",
        str(runner),
        "--output",
        str(out / "native"),
        "--timeout",
        "20",
        "--",
        str(args.offline_exec.resolve()),
        "--unix-only",
        str(args.binary.resolve()),
        str(captures),
        str(args.fonts.resolve()),
    ]
    if args.funding:
        command.append("--funding")
    started = datetime.now(timezone.utc).isoformat()
    with (out / "output.log").open("wb") as log:
        result = subprocess.run(
            command, stdout=log, stderr=subprocess.STDOUT, timeout=50
        )
    status = dict(selected=1, executed=0, passed=0, failed=1, skipped=0)
    native = out / "native/processes.json"
    if native.is_file():
        processes = json.loads(native.read_text())
        settled = (
            bool(processes.get("child", {}).get("settled"))
            and len(processes["processes"]) == 2
        )
        settled = settled and all(
            p.get("settled") and not p.get("remainingLiveGroupMembers")
            for p in processes["processes"]
        )
        status["processesSettled"] = settled
        status["executed"] = int("child" in processes)
    summary = captures / "result.json"
    if result.returncode == 0 and status.get("processesSettled") and summary.is_file():
        observed = json.loads(summary.read_text())
        status["checks"] = observed["checks"]
        expected = (
            ("funding-unavailable",)
            if args.funding
            else ("send-recipient", "send-errors")
        )
        images = [encode_capture(captures / (name + ".ppm")) for name in expected]
        assert (
            observed["frames"] == len(images)
            and observed["teardown"]
            and observed["externalHostCommands"] == 0
        )
        assert not observed["connected"]
        for item in images:
            assert (captures / item["rawPath"]).with_suffix(".semantics.txt").is_file()
        status.update(passed=1, failed=0, frames=images, externalRequests=0)
    status.update(
        command=command,
        exit=result.returncode,
        startedUTC=started,
        completedUTC=datetime.now(timezone.utc).isoformat(),
        binary=binary_identity(args.binary),
    )
    write_json(out / "result.json", status)
    print(
        json.dumps(
            {
                key: status[key]
                for key in (
                    "selected",
                    "executed",
                    "passed",
                    "failed",
                    "skipped",
                    "exit",
                )
            }
        )
    )
    print(f"Recipient evidence: {out}")
    return int(status["passed"] != 1)


if __name__ == "__main__":
    raise SystemExit(main())
