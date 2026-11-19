# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""One real native Server Test/Apply/replacement journey on an owned display."""

import sys

sys.dont_write_bytecode = True
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "rpc"))
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)
from fake_http import FakeHttp
from capability_tests import GENESIS, steps


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", type=Path, required=True)
    p.add_argument("--fonts", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--general-binary", type=Path)
    args = p.parse_args()
    out = evidence_directory(args.output)
    owned_display = str(
        Path(__file__).resolve().parents[1] / "platform/owned_display.py"
    )
    general = []
    if args.general_binary:
        with tempfile.TemporaryDirectory(
            prefix="preferences-ui-", dir=validated_scratch()
        ) as tmp:
            for mode in ("write", "read"):
                run = out / ("general-" + mode)
                run.mkdir()
                command = [
                    sys.executable,
                    "-B",
                    owned_display,
                    "--output",
                    str(run),
                    "--timeout",
                    "25",
                    "--",
                    str(args.general_binary.resolve()),
                    str(run / "captures"),
                    str(args.fonts.resolve()),
                    str(Path(tmp) / "metadata.sqlite"),
                    str(Path(tmp) / "appearance"),
                    mode,
                ]
                started = datetime.now(timezone.utc).isoformat()
                with (run / "runner.log").open("w") as log:
                    completed = subprocess.run(
                        command, stdout=log, stderr=subprocess.STDOUT, timeout=50
                    )
                processes = json.loads((run / "processes.json").read_text())
                settled = processes["child"]["settled"] and all(
                    x["settled"] and not x.get("remainingLiveGroupMembers")
                    for x in processes["processes"]
                )
                receipt = run / "captures/result.json"
                general.append(
                    dict(
                        argv=command,
                        cwd=str(Path.cwd()),
                        startUTC=started,
                        endUTC=datetime.now(timezone.utc).isoformat(),
                        exit=completed.returncode,
                        processesSettled=settled,
                        binary=binary_identity(args.general_binary),
                        result=json.loads(receipt.read_text())
                        if receipt.exists()
                        else None,
                    )
                )
                write_json(out / "general-report.json", general)
                if completed.returncode or not settled or not general[-1]["result"]:
                    print(
                        json.dumps(
                            dict(
                                mode=mode,
                                exit=completed.returncode,
                                processesSettled=settled,
                            )
                        )
                    )
                    return 1
    with (
        tempfile.TemporaryDirectory(
            prefix="server-ui-", dir=validated_scratch()
        ) as tmp,
        FakeHttp(steps(policy=True, ibd=True, pruned=True)) as first,
        FakeHttp([]) as second,
    ):
        cookie = Path(tmp) / "explicit-cookie"
        cookie.write_text("fixture:" + first.secret)
        command = [
            sys.executable,
            "-B",
            owned_display,
            "--output",
            str(out),
            "--timeout",
            "25",
            "--",
            str(args.binary.resolve()),
            str(out / "captures"),
            str(args.fonts.resolve()),
            str(first.port),
            str(second.port),
            str(cookie),
            GENESIS,
        ]
        started = datetime.now(timezone.utc).isoformat()
        with (out / "runner.log").open("w") as log:
            completed = subprocess.run(
                command, stdout=log, stderr=subprocess.STDOUT, timeout=50
            )
    processes = json.loads((out / "processes.json").read_text())
    settled = (
        processes["child"]["settled"]
        and all(
            x["settled"] and not x.get("remainingLiveGroupMembers")
            for x in processes["processes"]
        )
        and not first.thread.is_alive()
        and not second.thread.is_alive()
        and first.active == second.active == 0
    )
    record = dict(
        argv=command,
        cwd=str(Path.cwd()),
        startUTC=started,
        endUTC=datetime.now(timezone.utc).isoformat(),
        exit=completed.returncode,
        binary=binary_identity(args.binary),
        processesSettled=settled,
        requests=first.records + second.records,
        general=general,
    )
    if (out / "captures/result.json").exists():
        record["result"] = json.loads((out / "captures/result.json").read_text())
    for log in out.rglob("*.log"):
        text = log.read_text(errors="replace")
        assert (
            first.auth not in text
            and first.secret not in text
            and second.auth not in text
            and second.secret not in text
        )
    write_json(out / "report.json", record)
    expected_methods = [
        step.method for step in steps(policy=True, ibd=True, pruned=True)
    ]
    transcript = [
        request["method"] for request in first.records
    ] == expected_methods and len(second.records) == 1
    print(
        json.dumps(
            dict(
                exit=record["exit"],
                processesSettled=settled,
                transcript=transcript,
                result=record.get("result"),
                general=[item["result"] for item in general],
            )
        )
    )
    return int(
        completed.returncode != 0
        or not settled
        or not transcript
        or record.get("result", {}).get("passed") != 1
        or any(item["result"]["passed"] != 1 for item in general)
    )


if __name__ == "__main__":
    raise SystemExit(main())
