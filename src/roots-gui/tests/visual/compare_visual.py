# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Nonzero exit for wrong/missing pixel evidence or an empty visual selection."""

import sys

sys.dont_write_bytecode = True
import argparse
import json
from pathlib import Path
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import evidence_directory, write_json
from pixels import compare, compare_files, Unavailable


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--actual", type=Path)
    p.add_argument("--baseline", type=Path)
    p.add_argument("--actual-file", type=Path)
    p.add_argument("--baseline-file", type=Path)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    out = Path(tempfile.mkdtemp(prefix="compare-", dir=evidence_directory(a.output)))
    record = dict(
        selected=1 if a.actual_file else 36,
        executed=0,
        passed=0,
        failed=0,
        skipped=0,
        unavailable=0,
    )
    code = 1
    try:
        if a.actual_file and a.baseline_file:
            result = compare_files(a.actual_file, a.baseline_file)
            record.update(
                executed=1,
                passed=int(result["passed"]),
                failed=int(not result["passed"]),
                kind="individual file probe; not matrix qualification",
                comparison=result,
            )
        elif a.actual and a.baseline:
            record.update(
                compare(a.actual, a.baseline), kind="full native matrix comparison"
            )
        else:
            raise ValueError("supply one complete file pair or manifest pair")
        code = int(record["failed"] > 0 or record["executed"] == 0)
    except Unavailable as e:
        record.update(unavailable=record["selected"], reason=str(e))
        code = 2
    except Exception as e:
        record.update(runnerErrors=1, error=type(e).__name__ + ": " + str(e))
        code = 1
    record["exit"] = code
    write_json(out / "result.json", record)
    print(json.dumps(record | {"output": str(out)}))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
