# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Owned public-session interruption/restart checks, no daemon or RPC transport."""

import sys

sys.dont_write_bytecode = True
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)
from storage.store_runner import Actor


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    out = evidence_directory(args.output)
    records = []
    for case in ["contracts", "interrupt-before-save", "interrupt-after-save"]:
        record = dict(
            id=case,
            startUTC=datetime.now(timezone.utc).isoformat(),
            passed=False,
            processes=[],
        )
        with tempfile.TemporaryDirectory(
            prefix="recover-", dir=validated_scratch()
        ) as tmp:
            path = Path(tmp) / "metadata.sqlite"
            actors = []
            try:
                if case == "contracts":
                    argv = [str(args.binary), "contracts", str(path)]
                    run = subprocess.run(
                        argv, capture_output=True, text=True, timeout=15
                    )
                    record.update(
                        argv=argv,
                        cwd=os.getcwd(),
                        exit=run.returncode,
                        stdout=run.stdout,
                        stderr=run.stderr,
                    )
                    assert run.returncode == 0 and not run.stderr
                    record["result"] = json.loads(run.stdout)
                else:
                    actor = Actor(args.binary, path)
                    actors.append(actor)
                    assert actor.read() == "ready"
                    assert actor.send("edit") == "edited"
                    if case == "interrupt-after-save":
                        assert actor.send("save") == "saved"
                    actor.process.kill()
                    actor.process.wait(timeout=3)
                    record["intentionalKillExit"] = actor.process.returncode
                    again = Actor(args.binary, path)
                    actors.append(again)
                    assert again.read() == "ready"
                    value = again.send("inspect")
                    assert value == (
                        "Interrupted edit after boundary"
                        if case == "interrupt-after-save"
                        else "Independent public draft 4"
                    )
                    record["restoredPublicEdit"] = value
                record["passed"] = True
            except Exception as error:
                record["failure"] = str(error)
            finally:
                for actor in actors:
                    result = actor.close()
                    record["processes"].append(result)
                    if (
                        not result["settled"]
                        or result["forcedTermination"]
                        or result["stderr"]
                    ):
                        record["passed"] = False
        record["scratchRemoved"] = not Path(tmp).exists()
        record["endUTC"] = datetime.now(timezone.utc).isoformat()
        records.append(record)
    result = dict(
        selected=3,
        executed=len(records),
        passed=sum(r["passed"] for r in records),
        failed=sum(not r["passed"] for r in records),
        skipped=0,
        binary=binary_identity(args.binary),
        cases=records,
        observedUTC=datetime.now(timezone.utc).isoformat(),
        rpcMutations=0,
        rpcCoverage="No RPC port exists in RecoverySession; this runner starts no daemon or RPC fixture.",
    )
    write_json(out / "result.json", result)
    print(
        json.dumps(
            {
                k: result[k]
                for k in ["selected", "executed", "passed", "failed", "skipped"]
            }
        )
    )
    return int(result["failed"] != 0)


if __name__ == "__main__":
    raise SystemExit(main())
