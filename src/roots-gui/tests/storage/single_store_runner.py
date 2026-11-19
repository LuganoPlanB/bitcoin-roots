# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""One deterministic native model with an isolated private metadata file."""

import sys

sys.dont_write_bytecode = True
import argparse
import json
import subprocess
import tempfile
from pathlib import Path
from datetime import datetime, timezone

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import (
    validated_scratch,
    evidence_directory,
    binary_identity,
    write_json,
)

p = argparse.ArgumentParser()
p.add_argument("--binary", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
out = evidence_directory(a.output)
start = datetime.now(timezone.utc).isoformat()
with tempfile.TemporaryDirectory(
    prefix="history-model-", dir=validated_scratch()
) as tmp:
    argv = [str(a.binary), str(Path(tmp) / "metadata.sqlite")]
    run = subprocess.run(argv, capture_output=True, text=True, timeout=20)
    (out / "output.log").write_text(run.stdout + run.stderr)
record = dict(
    argv=argv,
    cwd=str(Path.cwd()),
    startUTC=start,
    endUTC=datetime.now(timezone.utc).isoformat(),
    exit=run.returncode,
    binary=binary_identity(a.binary),
    selected=1,
    executed=1,
    passed=int(run.returncode == 0),
    failed=int(run.returncode != 0),
    skipped=0,
    scratchRemoved=not Path(tmp).exists(),
)
write_json(out / "result.json", record)
print(json.dumps(record))
raise SystemExit(run.returncode)
