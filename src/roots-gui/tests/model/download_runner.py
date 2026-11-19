# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Run actual GnuPG verification against owned public release fixtures."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
tests = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(tests))
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)
from download_fixture import make, now


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fonts", type=Path)
    args = parser.parse_args()
    base = evidence_directory(args.output)
    output = (
        base
        if args.fonts
        else Path(tempfile.mkdtemp(prefix="download-attempt-", dir=base))
    )
    scratch = Path(validated_scratch())
    records = []
    with tempfile.TemporaryDirectory(prefix="release-", dir=scratch) as folder:
        root = Path(folder)
        fingerprints = make(root, records)
        published = output / "public-fixtures"
        published.mkdir()
        for path in root.iterdir():
            assert path.is_file(), "only public fixture files may leave scratch"
            shutil.copy2(path, published / path.name)
        binary = str(args.binary.resolve())
        if args.fonts:
            command = [
                sys.executable,
                "-B",
                str(tests / "platform/owned_display.py"),
                "--output",
                str(output),
                "--timeout",
                "26",
                "--",
                binary,
                str(output / "captures"),
                str(args.fonts.resolve()),
                str(root),
                fingerprints["current"],
            ]
        else:
            command = [
                binary,
                str(root),
                fingerprints["current"],
                fingerprints["expired"],
            ]
        started = now()
        with (output / "command-output.log").open("w") as log:
            ran = subprocess.run(
                command, stdout=log, stderr=subprocess.STDOUT, timeout=45
            )
        ended = now()

    if args.fonts:
        owned = json.loads((output / "processes.json").read_text())
        settled = owned["child"]["settled"] and all(
            p["settled"] and not p.get("remainingLiveGroupMembers")
            for p in owned["processes"]
        )
        summary = output / "captures/result.json"
        result = json.loads(summary.read_text()) if summary.exists() else None
    else:
        settled = True
        raw = (output / "command-output.log").read_text()
        result = json.loads(raw) if ran.returncode == 0 else None
    summary = dict(
        argv=command,
        cwd=str(Path.cwd()),
        startUTC=started,
        endUTC=ended,
        exit=ran.returncode,
        binary=binary_identity(args.binary),
        fixtureFingerprints=fingerprints,
        fixtureCommands=records,
        processesSettled=settled and not root.exists(),
        result=result,
        syntheticFixture=True,
    )
    write_json(output / "report.json", summary)
    print(f"Download evidence: {output}")
    print(
        json.dumps(
            {key: summary[key] for key in ("exit", "processesSettled", "result")}
        )
    )
    return int(
        ran.returncode != 0
        or not summary["processesSettled"]
        or not result
        or result["passed"] != 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
