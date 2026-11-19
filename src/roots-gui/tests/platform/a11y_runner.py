# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Isolated Linux accessibility evidence; no dependency installation or OS settings."""

import sys

sys.dont_write_bytecode = True
import argparse
from datetime import datetime, timezone
import hashlib
import gzip
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import evidence_directory, validated_scratch, binary_identity


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--kind", choices=["native", "orca", "transport", "input"], required=True
    )
    p.add_argument("--binary", type=Path, required=True)
    p.add_argument("--fonts", type=Path)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    validated_scratch()
    out = Path(tempfile.mkdtemp(prefix=a.kind + "-", dir=evidence_directory(a.output)))
    here = Path(__file__).resolve().parent
    source = here.parents[1]
    selected_files = sorted(
        {
            path
            for folder in ["ui", "platform", "app", "tests/platform", "docs"]
            for path in (source / folder).rglob("*")
            if path.is_file() and "__pycache__" not in path.parts
        }
        | {source / "CMakeLists.txt", source / "third_party/NOTICES.md"}
    )
    source_before = []
    for path in selected_files:
        target = out / "source" / path.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        source_before.append(
            dict(
                path=str(path.relative_to(source)),
                bytes=target.stat().st_size,
                sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
            )
        )
    binary_before = binary_identity(a.binary)
    with (
        a.binary.open("rb") as source_binary,
        (out / "executed-binary.gz").open("wb") as destination,
    ):
        with gzip.GzipFile(
            fileobj=destination, mode="wb", compresslevel=1, mtime=0
        ) as archive:
            shutil.copyfileobj(source_binary, archive)
    script = here / (
        "input_session.py"
        if a.kind == "input"
        else "a11y_transport.py"
        if a.kind == "transport"
        else "a11y_session.py"
    )
    child_argv = [
        "/usr/bin/python3",
        "-B",
        str(script),
        "--output",
        str(out / "journey"),
        "--binary",
        str(a.binary.resolve()),
    ]
    if a.kind != "transport":
        child_argv += ["--fonts", str(a.fonts.resolve())]
    if a.kind == "orca":
        child_argv += ["--orca", "--orca-output", "braille-monitor"]
    if a.kind in ["transport", "input"]:
        child_argv = ["dbus-run-session", "--", *child_argv]
    argv = [
        sys.executable,
        "-B",
        str(here / "owned_display.py"),
        "--output",
        str(out / "desktop"),
        *(
            ["--timeout", "248" if a.kind == "orca" else "220"]
            if a.kind in ["native", "orca"]
            else []
        ),
        "--",
        *child_argv,
    ]
    start = datetime.now(timezone.utc).isoformat()
    timed_out = False
    with (out / "runner.log").open("w") as log:
        proc = subprocess.Popen(
            argv, stdout=log, stderr=subprocess.STDOUT, start_new_session=True
        )
        try:
            proc.wait(timeout=263 if a.kind == "orca" else 240)
        except subprocess.TimeoutExpired:
            timed_out = True
            os.killpg(proc.pid, signal.SIGTERM)
        finally:
            try:
                proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait(timeout=5)
    groups = {proc.pid}
    for name in ["journey/processes-live.json", "journey/processes.json"]:
        path = out / name
        if path.is_file():
            groups.update(x["pgid"] for x in json.loads(path.read_text()))
    desktop = (
        json.loads((out / "desktop/processes.json").read_text())
        if (out / "desktop/processes.json").exists()
        else {}
    )
    groups.update(x["pgid"] for x in desktop.get("processes", []) if "pgid" in x)

    def live(group):
        rows = []
        for path in Path("/proc").glob("[0-9]*/stat"):
            try:
                fields = path.read_text().rsplit(")", 1)[1].split()
                if int(fields[2]) == group and fields[0] != "Z":
                    rows.append(int(path.parent.name))
            except (OSError, ValueError, IndexError):
                pass
        return rows

    for group in groups:
        if live(group):
            try:
                os.killpg(group, signal.SIGTERM)
            except ProcessLookupError:
                pass
    deadline = time.monotonic() + 2
    while any(live(g) for g in groups) and time.monotonic() < deadline:
        time.sleep(0.02)
    for group in groups:
        if live(group):
            try:
                os.killpg(group, signal.SIGKILL)
            except ProcessLookupError:
                pass
    deadline = time.monotonic() + 2
    while any(live(g) for g in groups) and time.monotonic() < deadline:
        time.sleep(0.02)
    remaining = {str(g): live(g) for g in groups if live(g)}
    result = (
        json.loads((out / "journey/result.json").read_text())
        if (out / "journey/result.json").exists()
        else {"error": "result missing"}
    )
    settled = (
        not remaining
        and bool(desktop)
        and desktop.get("child", {}).get("settled", False)
        and all(
            x.get("settled") and not x.get("remainingLiveGroupMembers")
            for x in desktop.get("processes", [])
        )
    )
    ok = (
        proc.returncode == 0
        and not timed_out
        and settled
        and result.get("executed", 0) > 0
        and not result.get("failed", 1)
        and not result.get("error")
    )
    if a.kind == "orca":
        ok = ok and result.get("orcaExecuted") and result.get("readerOutputVerified")
    changed = [
        entry["path"]
        for entry in source_before
        if hashlib.sha256((source / entry["path"]).read_bytes()).hexdigest()
        != entry["sha256"]
    ]
    binary_changed = binary_identity(a.binary)["sha256"] != binary_before["sha256"]
    ok = ok and not changed and not binary_changed
    record = dict(
        kind=a.kind,
        argv=argv,
        cwd=str(Path.cwd()),
        startUTC=start,
        endUTC=datetime.now(timezone.utc).isoformat(),
        exit=proc.returncode,
        timedOut=timed_out,
        processesSettled=settled,
        remainingLiveGroups=remaining,
        result=result,
        binary=binary_identity(a.binary),
        passed=bool(ok),
    )
    record.update(
        source=source_before,
        sourceSnapshot=str(out / "source"),
        executedBinarySnapshot=str(out / "executed-binary.gz"),
        executedBinaryCompression="gzip; uncompressed identity is binary.sha256",
        sourceChangedDuringRun=changed,
        binaryChangedDuringRun=binary_changed,
    )
    if a.fonts:
        record["fonts"] = [
            dict(
                path=str(path),
                bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            )
            for path in sorted(a.fonts.glob("*"))
            if path.is_file()
        ]
    (out / "manifest.json").write_text(json.dumps(record, indent=2) + "\n")
    print(
        json.dumps(
            dict(
                kind=a.kind,
                passed=bool(ok),
                result=result,
                manifest=str(out / "manifest.json"),
            )
        )
    )
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
