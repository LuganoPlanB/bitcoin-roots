# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Actual conversion interruption and authenticated, byte-preserving restart."""

import sys

sys.dont_write_bytecode = True
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
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
    p.add_argument("--stop-library", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    out = evidence_directory(args.output)
    cases = []
    for mode in [
        "identical",
        "different-interrupted-resolution",
        "backup-conflict",
        "tampered",
    ]:
        record = dict(
            id=mode,
            startUTC=datetime.now(timezone.utc).isoformat(),
            passed=False,
            processes=[],
        )
        with tempfile.TemporaryDirectory(
            prefix="conversion-", dir=validated_scratch()
        ) as tmp:
            path = Path(tmp) / "metadata.sqlite"
            encrypted = Path(str(path) + ".protected")
            backup = Path(str(path) + ".conversion-backup.protected")

            def hashes():
                return {
                    p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in [path, encrypted, backup]
                    if p.is_file()
                }

            def run(command, expected=0, stop=False):
                argv = [str(args.binary), command, str(path)]
                environment = dict(os.environ)
                if stop:
                    environment.update(
                        LD_PRELOAD=str(args.stop_library),
                        ROOTS_GUI_STOP_UNLINK=str(path),
                    )
                start = datetime.now(timezone.utc).isoformat()
                r = subprocess.run(
                    argv, capture_output=True, text=True, env=environment, timeout=25
                )
                entry = dict(
                    argv=argv,
                    cwd=os.getcwd(),
                    startUTC=start,
                    endUTC=datetime.now(timezone.utc).isoformat(),
                    exit=r.returncode,
                    expectedExit=expected,
                    stdout=r.stdout,
                    stderr=r.stderr,
                    settled=True,
                    intentionalBoundaryStop=stop,
                )
                record["processes"].append(entry)
                assert r.returncode == expected and not r.stderr
                if expected == 0:
                    entry["result"] = json.loads(r.stdout)
                    assert entry["result"]["passed"]

            try:
                run("seed", 86, True)
                assert path.is_file() and encrypted.is_file()
                record["afterInterruptedConversion"] = hashes()
                if mode == "identical":
                    run("identical")
                elif mode == "different-interrupted-resolution":
                    run("mutate")
                    before = hashes()
                    run("denied")
                    assert hashes() == before
                    run("different", 86, True)
                    after = hashes()
                    assert (
                        all(after[k] == v for k, v in before.items())
                        and backup.is_file()
                    )
                    record["afterInterruptedResolution"] = after
                    run("different")
                    assert not path.exists() and backup.is_file()
                elif mode == "backup-conflict":
                    run("mutate")
                    shutil.copyfile(encrypted, backup)
                    backup.chmod(0o600)
                    before = hashes()
                    run("backup-conflict")
                    assert hashes() == before
                elif mode == "tampered":
                    raw = bytearray(encrypted.read_bytes())
                    raw[-1] ^= 1
                    encrypted.write_bytes(raw)
                    before = hashes()
                    run("tampered")
                    assert hashes() == before
                record["finalFiles"] = hashes()
                record["passed"] = True
            except Exception as error:
                record["failure"] = type(error).__name__
        record.update(
            endUTC=datetime.now(timezone.utc).isoformat(),
            scratchRemoved=not Path(tmp).exists(),
        )
        cases.append(record)
    result = dict(
        selected=4,
        executed=len(cases),
        passed=sum(c["passed"] for c in cases),
        failed=sum(not c["passed"] for c in cases),
        skipped=0,
        binary=binary_identity(args.binary),
        stopLibrary=binary_identity(args.stop_library),
        cases=cases,
        rpcMutations=0,
        rpcCoverage="No RPC/command port in RecoverySession or this fixture; no daemon started.",
    )
    write_json(out / "result.json", result)
    print(
        json.dumps(
            {
                k: result[k]
                for k in ["selected", "executed", "passed", "failed", "skipped"]
            }
        )
    )
    return int(result["failed"] != 0)


if __name__ == "__main__":
    raise SystemExit(main())
