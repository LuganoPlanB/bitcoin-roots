# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Sequential positive/negative production smoke with independent report checks."""

import argparse
import json
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--binary", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
checks = []
commands = []


def check(value, label):
    checks.append(dict(id=label, passed=bool(value)))


for negative in [False, True]:
    name = "negative" if negative else "positive"
    out = a.output / name
    argv = [
        sys.executable,
        str(Path(__file__).with_name("entry_smoke.py")),
        "--binary",
        str(a.binary),
        "--output",
        str(out),
    ] + (["--inject-failure"] if negative else [])
    with (a.output / (name + ".log")).open("w") as log:
        r = subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT, timeout=30)
    commands.append(dict(argv=argv, exit=r.returncode))
    check(r.returncode == (1 if negative else 0), name + " expected exit")
    data = json.loads((out / "result.json").read_text())
    check(
        data["executed"] == data["passed"] + data["failed"],
        name + " coherent attempted count",
    )
    check(
        (data["executed"], data["passed"], data["failed"], data["skipped"])
        == ((6, 5, 1, 0) if negative else (7, 7, 0, 0)),
        name + " exact outcome counts",
    )
    check(data["appSettled"], name + " owned app settled")
    check(
        Path(data["workingDirectory"]) != Path(data["relocatedBinary"]).parent,
        name + " cwd differs from relocated package",
    )
    if negative:
        check(
            data["error"] == "injected smoke assertion failure",
            name + " actual injected assertion reached",
        )
failed = sum(not x["passed"] for x in checks)
result = dict(
    executed=len(checks),
    passed=len(checks) - failed,
    failed=failed,
    skipped=0,
    commands=commands,
    checks=checks,
)
(a.output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result))
raise SystemExit(bool(failed))
