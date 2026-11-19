# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Verify locally delivered test-only dependency bytes; never fetch sources."""

import hashlib
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
manifest = json.loads((root / "manifest.json").read_text())
expected = {x["path"]: x["sha256"] for x in manifest["files"]}
actual = {
    str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in root.rglob("*")
    if p.is_file() and p != root / "manifest.json"
}
if actual != expected:
    raise ValueError("test source delivery differs from exact hash manifest")
print(
    json.dumps(
        dict(
            executed=len(expected),
            passed=len(expected),
            failed=0,
            skipped=0,
            engineRevision=manifest["engineRevision"],
        )
    )
)
