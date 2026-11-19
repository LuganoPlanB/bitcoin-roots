# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Hold one real Xvfb launch before exec, then overlap another authenticated owner."""

import sys

sys.dont_write_bytecode = True
import ctypes
import json
import os
from pathlib import Path
import runpy
import signal
import subprocess
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import validated_scratch, evidence_directory


def wait_for(path, process, timeout=12):
    deadline = time.monotonic() + timeout
    while not path.exists():
        if process.poll() is not None:
            raise RuntimeError("owner exited before " + str(path))
        if time.monotonic() > deadline:
            raise TimeoutError(str(path))
        time.sleep(0.01)


def main():
    if sys.argv[1] == "held-owner":
        root = Path(sys.argv[2])
        original = subprocess.Popen

        def launch(argv, *args, **kwargs):
            if argv[0] == "Xvfb":
                (root / "selected").write_text(argv[1])
                deadline = time.monotonic() + 12
                while not (root / "release-server").exists():
                    if time.monotonic() > deadline:
                        raise TimeoutError("held real Xvfb launch")
                    time.sleep(0.01)
            return original(argv, *args, **kwargs)

        subprocess.Popen = launch
        sys.argv = [str(Path(__file__).with_name("owned_display.py")), *sys.argv[3:]]
        runpy.run_path(sys.argv[0], run_name="__main__")
        return 0
    if sys.argv[1] == "child":
        root = Path(sys.argv[2])
        lib = ctypes.CDLL("libX11.so.6")
        lib.XOpenDisplay.restype = ctypes.c_void_p
        lib.XCloseDisplay.argtypes = [ctypes.c_void_p]
        display = lib.XOpenDisplay(None)
        if not display:
            raise RuntimeError("actual owned cookie cannot authenticate")
        lib.XCloseDisplay(display)
        (root / "child-ready").write_text(
            json.dumps(dict(display=os.environ["DISPLAY"], authenticated=True))
        )
        deadline = time.monotonic() + 12
        while not (root / "release-child").exists():
            if time.monotonic() > deadline:
                raise TimeoutError("held authenticated child")
            time.sleep(0.01)
        return 0
    scratch = validated_scratch()
    out = Path(
        tempfile.mkdtemp(
            prefix="display-allocation-", dir=evidence_directory(Path(sys.argv[1]))
        )
    )
    owners = []
    streams = []
    error = None
    try:
        first = out / "first"
        second = out / "second"
        first.mkdir()
        second.mkdir()
        for name, folder in [("first", first), ("second", second)]:
            argv = (
                [
                    sys.executable,
                    "-B",
                    str(Path(__file__).resolve()),
                    "held-owner",
                    str(first),
                ]
                if name == "first"
                else [
                    sys.executable,
                    "-B",
                    str(Path(__file__).with_name("owned_display.py")),
                ]
            )
            argv += [
                "--output",
                str(folder / "desktop"),
                "--",
                sys.executable,
                "-B",
                str(Path(__file__).resolve()),
                "child",
                str(folder),
            ]
            stream = (folder / "outer.log").open("w")
            streams.append(stream)
            proc = subprocess.Popen(
                argv, stdout=stream, stderr=subprocess.STDOUT, start_new_session=True
            )
            owners.append(proc)
            wait_for(
                first / "selected" if name == "first" else second / "child-ready", proc
            )
        (first / "release-server").touch()
        wait_for(first / "child-ready", owners[0])
        a = json.loads((first / "child-ready").read_text())
        b = json.loads((second / "child-ready").read_text())
        if a["display"] == b["display"]:
            raise AssertionError("overlapping owners share an X display")
        for folder in [first, second]:
            (folder / "release-child").touch()
        for proc in owners:
            proc.wait(timeout=12)
        if any(proc.returncode for proc in owners):
            raise AssertionError("authenticated owner failed")
        for folder in [first, second]:
            record = json.loads((folder / "desktop/processes.json").read_text())
            if not record["child"]["settled"] or not all(
                x["settled"] and not x.get("remainingLiveGroupMembers")
                for x in record["processes"]
            ):
                raise AssertionError("owner teardown incomplete")
    except Exception as exc:
        error = str(exc)
    finally:
        for proc in owners:
            if proc.poll() is None:
                try:
                    os.killpg(proc.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid, signal.SIGKILL)
                    proc.wait(timeout=5)
        for stream in streams:
            stream.close()
        result = dict(
            passed=error is None,
            error=error,
            executed=1,
            selected=1,
            failed=int(error is not None),
            scratch=str(scratch),
            owners=[
                dict(pid=p.pid, exit=p.returncode, settled=p.poll() is not None)
                for p in owners
            ],
        )
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(dict(output=str(out), **result)))
    return int(error is not None)


if __name__ == "__main__":
    raise SystemExit(main())
