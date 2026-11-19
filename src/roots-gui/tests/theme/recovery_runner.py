# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""One actual960x640/150% public recovery/Clear journey."""

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
    p.add_argument("--fonts", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    out = evidence_directory(args.output)
    start = datetime.now(timezone.utc).isoformat()
    with tempfile.TemporaryDirectory(
        prefix="recover-ui-", dir=validated_scratch()
    ) as tmp:
        argv = [
            sys.executable,
            "-B",
            str(Path(__file__).resolve().parents[1] / "platform/owned_display.py"),
            "--output",
            str(out / "native"),
            "--timeout",
            "25",
            "--",
            str(args.binary),
            str(out / "captures"),
            str(args.fonts),
            str(Path(tmp) / "metadata.sqlite"),
        ]
        run = subprocess.run(argv, capture_output=True, text=True, timeout=40)
        (out / "output.log").write_text(run.stdout + run.stderr)
    result = (
        json.loads((out / "captures/result.json").read_text())
        if (out / "captures/result.json").exists()
        else dict(selected=1, executed=0, passed=0, failed=1, skipped=0)
    )
    result.update(
        argv=argv,
        exit=run.returncode,
        startUTC=start,
        endUTC=datetime.now(timezone.utc).isoformat(),
        binary=binary_identity(args.binary),
        scratchRemoved=not Path(tmp).exists(),
    )
    write_json(out / "result.json", result)
    print(
        json.dumps(
            {
                k: result[k]
                for k in ["selected", "executed", "passed", "failed", "skipped", "exit"]
            }
        )
    )
    return int(
        run.returncode != 0 or result["passed"] != 1 or not result["scratchRemoved"]
    )


if __name__ == "__main__":
    raise SystemExit(main())
