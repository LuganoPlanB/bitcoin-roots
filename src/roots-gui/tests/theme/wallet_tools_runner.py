# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Exercise the production wallet tools on an authenticated, child-owned X11 display."""

import argparse
from datetime import datetime, timezone
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
from capability_tests import GENESIS, steps
from fake_http import FakeHttp, Step


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--fonts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = evidence_directory(args.output)
    owned = Path(__file__).resolve().parents[1] / "platform/owned_display.py"
    script = steps(wallet=None, pruned=True) + [
        Step(
            "getblockchaininfo",
            dict(
                chain="regtest",
                blocks=19,
                initialblockdownload=False,
                pruned=True,
                bestblockhash="b" * 64,
            ),
        ),
        Step("getblockhash", GENESIS),
    ]
    with (
        tempfile.TemporaryDirectory(
            prefix="wallet-tools-", dir=validated_scratch()
        ) as scratch,
        FakeHttp(script) as node,
    ):
        cookie = Path(scratch) / "explicit-cookie"
        cookie.write_text("fixture:" + node.secret)
        cookie.chmod(0o600)
        command = [
            sys.executable,
            "-B",
            str(owned),
            "--output",
            str(output),
            "--timeout",
            "25",
            "--",
            str(args.binary.resolve()),
            str(output / "captures"),
            str(args.fonts.resolve()),
            str(Path(scratch) / "metadata.sqlite"),
            str(node.port),
            str(cookie),
        ]
        started = datetime.now(timezone.utc).isoformat()
        with (output / "runner.log").open("w") as log:
            result = subprocess.run(
                command, stdout=log, stderr=subprocess.STDOUT, timeout=45
            )
    processes = json.loads((output / "processes.json").read_text())
    settled = (
        processes["child"]["settled"]
        and all(
            p["settled"] and not p.get("remainingLiveGroupMembers")
            for p in processes["processes"]
        )
        and not node.thread.is_alive()
        and node.active == 0
        and not Path(scratch).exists()
    )
    rpc = [(r["method"], r["status"], r["wallet"]) for r in node.records]
    transcript = (
        rpc == [(step.method, 200, False) for step in script]
        and not node.check_failures
    )
    report = output / "captures/result.json"
    record = dict(
        argv=command,
        cwd=str(Path.cwd()),
        startUTC=started,
        endUTC=datetime.now(timezone.utc).isoformat(),
        exit=result.returncode,
        processesSettled=settled,
        transcript=transcript,
        requests=node.records,
        binary=binary_identity(args.binary),
        result=json.loads(report.read_text()) if report.exists() else None,
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
            {
                key: record[key]
                for key in ("exit", "processesSettled", "transcript", "result")
            }
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
