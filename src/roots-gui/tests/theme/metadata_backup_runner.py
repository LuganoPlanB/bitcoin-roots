# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Actual NativeHost recovery guidance and encrypted wallet metadata journey."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import secrets
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


def now():
    return datetime.now(timezone.utc).isoformat()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "fonts", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    args = p.parse_args()
    out = evidence_directory(args.output)
    password = secrets.token_urlsafe(28)
    archive_identity = None
    with tempfile.TemporaryDirectory(
        prefix="l77-native-", dir=validated_scratch()
    ) as folder:
        root = Path(folder)
        control = root / "archive-password"
        control.write_text(password)
        control.chmod(0o600)
        command = [
            sys.executable,
            "-B",
            str(tests / "platform/owned_display.py"),
            "--output",
            str(out),
            "--timeout",
            "24",
            "--",
            str(args.binary),
            str(out / "captures"),
            str(args.fonts),
            str(root),
        ]
        start = now()
        with (out / "runner.log").open("w") as log:
            completed = subprocess.run(
                command, stdout=log, stderr=subprocess.STDOUT, timeout=38
            )
        end = now()
        candidate = root / "owned-wallet.rmeta"
        if candidate.exists():
            archive_identity = dict(
                bytes=candidate.stat().st_size,
                sha256=hashlib.sha256(candidate.read_bytes()).hexdigest(),
                mode=oct(candidate.stat().st_mode & 0o777),
            )
    for related in [
        *out.rglob("*.log"),
        *out.rglob("*semantics.txt"),
        *out.rglob("*.json"),
        *out.rglob("native-timeout.txt"),
    ]:
        assert password not in related.read_text(errors="replace")
    report = json.loads((out / "processes.json").read_text())
    settled = report["child"]["settled"] and all(
        item["settled"] and not item.get("remainingLiveGroupMembers")
        for item in report["processes"]
    )
    summary = out / "captures/result.json"
    result = json.loads(summary.read_text()) if summary.is_file() else None
    receipt = dict(
        argv=command,
        cwd=str(Path.cwd()),
        startUTC=start,
        endUTC=end,
        exit=completed.returncode,
        binary=binary_identity(args.binary),
        processesSettled=settled,
        scratchRemoved=not root.exists(),
        archive=archive_identity,
        result=result,
        realFiles=True,
        daemonRequests=0,
    )
    write_json(out / "report.json", receipt)
    print(
        json.dumps(
            {
                key: receipt[key]
                for key in ("exit", "processesSettled", "scratchRemoved", "result")
            }
        )
    )
    return int(
        completed.returncode != 0
        or not settled
        or root.exists()
        or not archive_identity
        or not result
        or result["passed"] != 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
