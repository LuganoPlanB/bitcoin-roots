# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Rejection probes use real stored pixels and explicit synthetic metadata only."""

import sys

sys.dont_write_bytecode = True
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import evidence_directory, write_json
from pixels import manifest, Unavailable


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--baseline", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    out = Path(
        tempfile.mkdtemp(prefix="visual-contract-", dir=evidence_directory(a.output))
    )
    checks = []
    commands = []
    try:
        data = manifest(a.baseline, curated=True)
    except Unavailable:
        write_json(
            out / "result.json",
            dict(selected=6, executed=0, passed=0, failed=0, skipped=0, unavailable=6),
        )
        return 2
    light = a.baseline / "panel-96/captures/test-panel-960x640-light-100.ppm"
    dark = a.baseline / "panel-96/captures/test-panel-960x640-dark-100.ppm"

    def run(name, args, expected, condition):
        argv = [
            sys.executable,
            "-B",
            str(Path(__file__).with_name("compare_visual.py")),
            *args,
            "--output",
            str(out / name),
        ]
        with (out / (name + ".log")).open("w") as log:
            r = subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT, timeout=20)
        result = json.loads(
            next((out / name).glob("compare-*/result.json")).read_text()
        )
        commands.append(dict(argv=argv, exit=r.returncode))
        checks.append(
            dict(id=name, passed=r.returncode == expected and condition(result))
        )

    run(
        "real-same-file",
        ["--actual-file", str(light), "--baseline-file", str(light)],
        0,
        lambda r: r["executed"] == r["passed"] == 1,
    )
    run(
        "wrong-real-baseline",
        ["--actual-file", str(light), "--baseline-file", str(dark)],
        1,
        lambda r: r["failed"] == 1 and r["comparison"]["changedChannels"] > 0,
    )
    run(
        "missing-framebuffer",
        ["--actual-file", str(out / "missing.ppm"), "--baseline-file", str(light)],
        2,
        lambda r: r["executed"] == 0 and r["unavailable"] == 1,
    )
    zero = out / "zero"
    zero.mkdir()
    write_json(zero / "visual-manifest.json", data | {"matrix": []})
    run(
        "zero-execution",
        ["--actual", str(zero), "--baseline", str(a.baseline)],
        1,
        lambda r: r["executed"] == 0 and r["runnerErrors"] == 1,
    )
    # Missing baseline curation cannot silently adopt the current captures.
    no_review = out / "unreviewed"
    no_review.mkdir()
    write_json(no_review / "visual-manifest.json", data)
    run(
        "unreviewed-baseline",
        ["--actual", str(a.baseline), "--baseline", str(no_review)],
        2,
        lambda r: r["executed"] == 0 and r["unavailable"] == 36,
    )
    argv = [
        sys.executable,
        "-B",
        str(Path(__file__).with_name("run_visual.py")),
        "--native-capture",
        str(out / "missing-binary"),
        "--native-lifecycle",
        str(out / "missing-binary"),
        "--fonts",
        str(out),
        "--output",
        str(out / "unavailable-native"),
        "--force-unavailable",
    ]
    with (out / "unavailable-native.log").open("w") as log:
        r = subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT, timeout=10)
    result = json.loads(
        next((out / "unavailable-native").glob("visual-*/result.json")).read_text()
    )
    commands.append(dict(argv=argv, exit=r.returncode))
    checks.append(
        dict(
            id="unavailable-native-never-passes",
            passed=r.returncode == 2
            and result["executed"] == 0
            and result["unavailable"] == 48
            and not result["commands"],
        )
    )
    failed = sum(not x["passed"] for x in checks)
    result = dict(
        selected=6,
        executed=len(checks),
        passed=len(checks) - failed,
        failed=failed,
        skipped=0,
        checks=checks,
        commands=commands,
    )
    write_json(out / "result.json", result)
    print(json.dumps(result | {"output": str(out)}))
    return int(failed > 0 or len(checks) != 6)


if __name__ == "__main__":
    raise SystemExit(main())
