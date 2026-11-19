# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Run production desktop routes and replace one owned GUI on private X11."""

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
from capability_tests import steps
from fake_http import FakeHttp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--fonts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = evidence_directory(args.output)
    owned = Path(__file__).resolve().parents[1] / "platform/owned_display.py"
    script = steps(wallet=None)
    with (
        tempfile.TemporaryDirectory(
            prefix="native-desktop-", dir=validated_scratch()
        ) as folder,
        FakeHttp(script) as node,
    ):
        root = Path(folder)
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
            "32",
            "--",
            str(args.binary.resolve()),
            str(output / "captures"),
            str(args.fonts.resolve()),
            str(root / "metadata.sqlite"),
            str(node.port),
        ]
        started = datetime.now(timezone.utc).isoformat()
        with (output / "runner.log").open("w") as log:
            result = subprocess.run(
                command, stdout=log, stderr=subprocess.STDOUT, timeout=50
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
        and not root.exists()
    )
    requests = [(r["method"], r["status"], r["wallet"]) for r in node.records]
    transcript = (
        requests == [(step.method, 200, False) for step in script]
        and not node.check_failures
    )

    def load(name):
        file = output / "captures" / name
        return json.loads(file.read_text()) if file.exists() else None

    first, second = load("restart-before.json"), load("restart-after.json")
    replaced = bool(
        first
        and second
        and first["pid"] == second["pid"] == processes["child"]["pid"]
        and first["teardown"]
        and second["teardown"]
        and first["probes"] == 1
        and second["probes"] == 0
    )
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
        first=first,
        replacement=second,
        sameOwnedGuiProcess=replaced,
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
                k: record[k]
                for k in (
                    "exit",
                    "processesSettled",
                    "transcript",
                    "sameOwnedGuiProcess",
                    "first",
                    "replacement",
                )
            }
        )
    )
    return int(result.returncode != 0 or not settled or not transcript or not replaced)


if __name__ == "__main__":
    raise SystemExit(main())
