# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""One native refresh failure -> dated offline-cache journey, real X11 input."""

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
from fake_http import FakeHttp, Step
from capability_tests import GENESIS, steps
from refresh_tests import refresh_steps


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", type=Path, required=True)
    p.add_argument("--fonts", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    out = evidence_directory(args.output)
    scripted = steps(policy=True)
    scripted[3].result += "getbalances\nlistsinceblock\n"
    scripted[4].result["methods"] += [
        dict(name="getbalances", params=[]),
        dict(
            name="listsinceblock",
            params=[
                dict(name=n, schema=dict(type=t))
                for n, t in [("blockhash", "string"), ("include_removed", "boolean")]
            ],
        ),
    ]
    scripted += refresh_steps(wallet="fixture-public") + [
        Step("getblockchaininfo", status=503)
    ]
    with (
        tempfile.TemporaryDirectory(
            prefix="refresh-ui-", dir=validated_scratch()
        ) as tmp,
        FakeHttp(scripted) as server,
    ):
        cookie = Path(tmp) / "explicit-cookie"
        cookie.write_text("fixture:" + server.secret)
        command = [
            sys.executable,
            "-B",
            str(Path(__file__).resolve().parents[1] / "platform/owned_display.py"),
            "--output",
            str(out),
            "--timeout",
            "25",
            "--",
            str(args.binary.resolve()),
            str(out / "captures"),
            str(args.fonts.resolve()),
            str(server.port),
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
        and not server.thread.is_alive()
        and server.active == 0
    )
    record = dict(
        argv=command,
        cwd=str(Path.cwd()),
        startUTC=started,
        endUTC=datetime.now(timezone.utc).isoformat(),
        exit=completed.returncode,
        binary=binary_identity(args.binary),
        processesSettled=settled,
        requests=server.records,
        requestChecks=server.check_failures,
    )
    if (out / "captures/result.json").exists():
        record["result"] = json.loads((out / "captures/result.json").read_text())
    record["noOfflineTransportCalls"] = (
        len(server.records) == 13
        and server.completed[-1:] == ["getblockchaininfo"]
        and not server.check_failures
    )
    for log in out.rglob("*.log"):
        text = log.read_text(errors="replace")
        assert server.secret not in text and server.auth not in text
    write_json(out / "report.json", record)
    print(
        json.dumps(
            {
                k: record.get(k)
                for k in [
                    "exit",
                    "processesSettled",
                    "noOfflineTransportCalls",
                    "result",
                ]
            }
        )
    )
    return int(
        completed.returncode != 0
        or not settled
        or not record["noOfflineTransportCalls"]
        or record.get("result", {}).get("passed") != 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
