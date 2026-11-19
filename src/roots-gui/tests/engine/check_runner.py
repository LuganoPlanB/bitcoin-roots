# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Require observed rejection preconditions, exact counts and settled children."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--runner", type=Path, required=True)
p.add_argument(
    "--probe",
    choices=["failure", "unknown", "count", "undrained", "timeout"],
    required=True,
)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
selection = {
    "failure": "runner.failure",
    "unknown": "no_such_case",
    "count": "roots.preferences",
    "undrained": "roots.preferences",
    "timeout": "runner.timeout",
}[a.probe]
argv = [
    str(a.runner),
    selection,
    str(a.output / "engine.xml"),
    "2" if a.probe == "count" else "1",
] + (["undrained"] if a.probe == "undrained" else [])
child = subprocess.Popen(
    argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
)
timed_out = False
try:
    stdout, _ = child.communicate(timeout=1 if a.probe == "timeout" else 6)
except subprocess.TimeoutExpired:
    timed_out = True
    child.kill()
    stdout, _ = child.communicate(timeout=2)
(a.output / "child.log").write_text(stdout)
checks = []


def check(value, label):
    checks.append(dict(id=label, passed=bool(value)))


check(child.poll() is not None, "owned child settled")
if a.probe == "timeout":
    check(timed_out, "outer wall-clock timeout observed")
    check("TIMEOUT_PRECONDITION_ENTERED" in stdout, "held callback actually entered")
    check(child.returncode != 0, "timeout cannot return success")
else:
    check(not timed_out, "ordinary negative run did not time out")
    check(child.returncode == 1, "contract rejection exit1")
    m = re.search(
        r"FAIL filter=.* tested=(\d+) passed=(\d+) expected=(\d+) remaining=(\d+) frames=(\d+)",
        stdout,
    )
    check(m is not None, "structured runner summary present")
    if m:
        tested, passed, expected, remaining, frames = map(int, m.groups())
        wanted = {
            "failure": (1, 0, 1),
            "unknown": (0, 0, 1),
            "count": (1, 1, 2),
            "undrained": (0, 0, 1),
        }[a.probe]
        check(
            (tested, passed, expected) == wanted, "exact tested/passed/expected counts"
        )
        check(
            (remaining > 0 and frames == 0)
            if a.probe == "undrained"
            else remaining == 0,
            "queue precondition and drainage",
        )
    report = a.output / "engine.xml"
    check(report.is_file() and report.stat().st_size > 0, "current JUnit produced")
    if report.is_file() and report.stat().st_size:
        cases = ET.parse(report).getroot().findall(".//testcase")
        active = [c for c in cases if c.get("status") in ["Success", "Error"]]
        if a.probe == "undrained":
            check(
                sum(c.get("status") == "Queued" for c in cases) == 1,
                "JUnit retains queued identity without execution",
            )
        check(
            len(active) == (0 if a.probe in ["unknown", "undrained"] else 1),
            "JUnit executed identities count",
        )
failed = sum(not x["passed"] for x in checks)
result = dict(
    probe=a.probe,
    argv=argv,
    childExit=child.returncode,
    childSettled=child.poll() is not None,
    timedOut=timed_out,
    executed=len(checks),
    passed=len(checks) - failed,
    failed=failed,
    skipped=0,
    checks=checks,
)
(a.output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result))
raise SystemExit(bool(failed))
