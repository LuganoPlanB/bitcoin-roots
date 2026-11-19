# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Native message UI against the owned authenticated loopback fixture."""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "rpc"))
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)
from message_runner import now, scenario
from fake_http import FakeHttp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--fonts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = evidence_directory(args.output)
    owned = Path(__file__).resolve().parents[1] / "platform/owned_display.py"
    with tempfile.TemporaryDirectory(
        prefix="native-message-", dir=validated_scratch()
    ) as folder:
        root = Path(folder)
        script, scopes, _ = scenario("success", root)
        with FakeHttp(script) as node:
            cookie = root / "rpc-cookie"
            cookie.write_text("fixture:" + node.secret)
            cookie.chmod(0o600)
            command = [
                sys.executable,
                "-B",
                str(owned),
                "--output",
                str(output),
                "--timeout",
                "23",
                "--",
                str(args.binary.resolve()),
                str(output / "captures"),
                str(args.fonts.resolve()),
                str(node.port),
                str(cookie),
            ]
            started = now()
            with (output / "runner.log").open("w") as log:
                result = subprocess.run(
                    command, stdout=log, stderr=subprocess.STDOUT, timeout=40
                )
            ended = now()
    processes = json.loads((output / "processes.json").read_text())
    settled = (
        processes["child"]["settled"]
        and all(
            p["settled"] and not p.get("remainingLiveGroupMembers")
            for p in processes["processes"]
        )
        and not node.thread.is_alive()
        and node.active == 0
        and not root.exists()
    )
    transcript = [(r["method"], r["status"], r["wallet"]) for r in node.records] == [
        (step.method, 200, wallet) for step, wallet in zip(script, scopes, strict=True)
    ] and not node.check_failures
    summary = output / "captures/result.json"
    record = dict(
        argv=command,
        cwd=str(Path.cwd()),
        startUTC=started,
        endUTC=ended,
        exit=result.returncode,
        binary=binary_identity(args.binary),
        processesSettled=settled,
        transcript=transcript,
        requests=node.records,
        result=json.loads(summary.read_text()) if summary.exists() else None,
    )
    write_json(output / "report.json", record)
    for log in [
        *output.rglob("*.log"),
        *output.rglob("*semantics.txt"),
        output / "report.json",
    ]:
        text = log.read_text(errors="replace")
        assert node.auth not in text and node.secret not in text
    print(
        json.dumps(
            {k: record[k] for k in ("exit", "processesSettled", "transcript", "result")}
        )
    )
    return int(
        result.returncode != 0
        or not settled
        or not transcript
        or (record["result"] or {}).get("passed") != 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
