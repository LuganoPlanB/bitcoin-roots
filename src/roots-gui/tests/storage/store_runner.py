# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Focused pinned-SQLite store checks; owned scratch and settled process actors."""

import sys

sys.dont_write_bytecode = True
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import selectors
import subprocess
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)


class Actor:
    def __init__(self, binary, path):
        self.argv = [str(binary), "actor", str(path)]
        self.process = subprocess.Popen(
            self.argv,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.events = []

    def read(self):
        with selectors.DefaultSelector() as selector:
            selector.register(self.process.stdout, selectors.EVENT_READ)
            assert selector.select(3), "owned actor deadline"
        value = self.process.stdout.readline().strip()
        self.events.append(value)
        return value

    def send(self, value):
        self.process.stdin.write(value + "\n")
        self.process.stdin.flush()
        return self.read()

    def close(self):
        try:
            self.process.stdin.close()
        except BrokenPipeError:
            pass
        forced = False
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            forced = True
            self.process.kill()
            self.process.wait(timeout=3)
        error = self.process.stderr.read()
        self.process.stdout.close()
        self.process.stderr.close()
        return dict(
            argv=self.argv,
            events=self.events,
            exit=self.process.returncode,
            settled=self.process.poll() is not None,
            forcedTermination=forced,
            stderr=error,
        )


def main():
    cases = [
        "persistence",
        "migrations",
        "rollback",
        "disk-full",
        "corruption",
        "schema-collision",
        "concurrent-instances",
        "receive-reservations",
        "rescan-tickets",
        "broadcast-attempts",
    ]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--case", choices=cases + ["broadcast-recovery"], action="append")
    args = p.parse_args()
    selected = args.case or cases
    if len(set(selected)) != len(selected):
        p.error("duplicate selected case")
    out = Path(tempfile.mkdtemp(prefix="attempt-", dir=evidence_directory(args.output)))
    records = []
    for case in selected:
        record = dict(
            id=case,
            startUTC=datetime.now(timezone.utc).isoformat(),
            passed=False,
            processes=[],
        )
        with tempfile.TemporaryDirectory(
            prefix="store-", dir=validated_scratch()
        ) as tmp:
            path = Path(tmp) / "metadata.sqlite"
            record["disposableDirectory"] = tmp
            try:
                if case != "concurrent-instances":
                    command = [str(args.binary), case, str(path)]
                    completed = subprocess.run(
                        command, capture_output=True, text=True, timeout=15
                    )
                    record.update(
                        argv=command,
                        cwd=os.getcwd(),
                        exit=completed.returncode,
                        stdout=completed.stdout,
                        stderr=completed.stderr,
                        settled=True,
                    )
                    assert completed.returncode == 0 and not completed.stderr
                    record["result"] = json.loads(completed.stdout)
                    assert record["result"]["passed"]
                    record["passed"] = True
                else:
                    actors = []
                    try:
                        first = Actor(args.binary, path)
                        actors.append(first)
                        assert first.read() == "ready"
                        second = Actor(args.binary, path)
                        actors.append(second)
                        assert second.read() == "ready"
                        assert first.send("save") == "success"
                        assert second.send("save") == "metadata-conflict"
                        assert second.send("reload") == "1"
                        assert first.send("hold") == "held"
                        assert second.send("save") == "store-busy"
                        assert first.send("release") == "released"
                        assert second.send("save") == "success"
                        assert first.send("reload") == "2"
                        record["passed"] = True
                        record["checks"] = 8
                    finally:
                        cleanup_errors = []
                        for actor in actors:
                            try:
                                record["processes"].append(actor.close())
                            except Exception as error:
                                cleanup_errors.append(type(error).__name__)
                        if cleanup_errors or any(
                            not a["settled"]
                            or a["forcedTermination"]
                            or a["exit"] != 0
                            or a["stderr"]
                            for a in record["processes"]
                        ):
                            record["passed"] = False
                            record["cleanupErrors"] = cleanup_errors
            except Exception as error:
                record["failure"] = type(error).__name__
        record["scratchRemoved"] = not Path(tmp).exists()
        record["endUTC"] = datetime.now(timezone.utc).isoformat()
        records.append(record)
    result = dict(
        selected=len(selected),
        executed=len(records),
        passed=sum(r["passed"] for r in records),
        failed=sum(not r["passed"] for r in records),
        skipped=0,
        binary=binary_identity(args.binary),
        cases=records,
        observedUTC=datetime.now(timezone.utc).isoformat(),
    )
    write_json(out / "result.json", result)
    print("Metadata store evidence:", out)
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
