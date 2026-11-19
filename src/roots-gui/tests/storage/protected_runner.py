# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Focused native encryption/password/epoch checks in private disposable data."""

import sys

sys.dont_write_bytecode = True
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import (
    validated_scratch,
    evidence_directory,
    binary_identity,
    write_json,
)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    out = evidence_directory(args.output)
    cases = []
    for mode in ["storage", "session", "held"]:
        record = dict(
            id=mode, startUTC=datetime.now(timezone.utc).isoformat(), passed=False
        )
        with tempfile.TemporaryDirectory(
            prefix="protected-", dir=validated_scratch()
        ) as tmp:
            argv = [str(args.binary), mode, str(Path(tmp) / "metadata.sqlite")]
            run = subprocess.run(argv, capture_output=True, text=True, timeout=25)
            record.update(
                argv=argv,
                exit=run.returncode,
                stdout=run.stdout,
                stderr=run.stderr,
                settled=True,
            )
            if run.returncode == 0 and not run.stderr:
                record["result"] = json.loads(run.stdout)
                record["passed"] = record["result"]["passed"]
        record.update(
            endUTC=datetime.now(timezone.utc).isoformat(),
            scratchRemoved=not Path(tmp).exists(),
        )
        cases.append(record)
    result = dict(
        selected=3,
        executed=len(cases),
        passed=sum(c["passed"] for c in cases),
        failed=sum(not c["passed"] for c in cases),
        skipped=0,
        binary=binary_identity(args.binary),
        cases=cases,
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
