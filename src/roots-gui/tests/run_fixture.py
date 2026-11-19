# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Allocate immutable per-run artifacts and settle the owned fixture process."""

import sys

sys.dont_write_bytecode = True
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from support.runtime import evidence_directory, write_json


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--kind", choices=["rpc", "regtest"], required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("arguments", nargs=argparse.REMAINDER)
    a = p.parse_args()
    args = a.arguments[1:] if a.arguments and a.arguments[0] == "--" else a.arguments
    out = Path(tempfile.mkdtemp(prefix=a.kind + "-", dir=evidence_directory(a.output)))
    script = Path(__file__).parent / (
        "rpc/fake_http_tests.py" if a.kind == "rpc" else "integration/regtest_tests.py"
    )
    argv = [sys.executable, "-B", str(script), "--output", str(out), *args]
    record = dict(
        argv=argv,
        kind=a.kind,
        timedOut=False,
        counts=dict(
            selected=None,
            executed=None,
            passed=None,
            failed=None,
            skipped=None,
            reason="unknown until current fixture report is read",
        ),
    )
    with (out / "runner.log").open("w") as log:
        child = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT)
        try:
            child.wait(timeout=150)
        except subprocess.TimeoutExpired:
            record["timedOut"] = True
            child.terminate()
            try:
                child.wait(timeout=20)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=3)
    record.update(exit=child.returncode, childSettled=child.poll() is not None)
    result = out / "result.json"
    if result.exists():
        r = json.loads(result.read_text())
        record["counts"] = {
            k: r[k] for k in ["selected", "executed", "passed", "failed", "skipped"]
        }
        valid = (
            r["executed"] > 0
            and r["selected"] == r["executed"]
            and r["executed"] == r["passed"] + r["failed"]
            and r["failed"] == r["skipped"] == 0
        )
    else:
        valid = False
        record["error"] = "missing fixture result; no capability pass"
    record["validCounts"] = valid
    write_json(out / "invocation.json", record)
    print(json.dumps(record))
    return int(child.returncode != 0 or not valid or record["timedOut"])


if __name__ == "__main__":
    raise SystemExit(main())
