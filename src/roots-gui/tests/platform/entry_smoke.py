# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Launch the actual relocated production executable and close through its real WM."""

import argparse
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--inject-failure",
        action="store_true",
        help="Test-only accounting/cleanup probe after observing the live production window",
    )
    args = parser.parse_args()
    if not os.getenv("ROOTS_GUI_OWNED_TEST_DISPLAY") or os.getenv(
        "ROOTS_GUI_OWNED_TEST_DISPLAY"
    ) != os.getenv("DISPLAY"):
        raise RuntimeError("requires authenticated owned test display")
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    x = c.CDLL("libX11.so.6")
    xt = c.CDLL("libXtst.so.6")
    x.XOpenDisplay.argtypes = [c.c_char_p]
    x.XOpenDisplay.restype = c.c_void_p
    x.XDefaultRootWindow.argtypes = [c.c_void_p]
    x.XDefaultRootWindow.restype = c.c_ulong
    x.XInternAtom.argtypes = [c.c_void_p, c.c_char_p, c.c_int]
    x.XInternAtom.restype = c.c_ulong
    x.XGetWindowProperty.argtypes = [
        c.c_void_p,
        c.c_ulong,
        c.c_ulong,
        c.c_long,
        c.c_long,
        c.c_int,
        c.c_ulong,
        c.POINTER(c.c_ulong),
        c.POINTER(c.c_int),
        c.POINTER(c.c_ulong),
        c.POINTER(c.c_ulong),
        c.POINTER(c.c_void_p),
    ]
    x.XFree.argtypes = [c.c_void_p]
    x.XCloseDisplay.argtypes = [c.c_void_p]
    x.XSetInputFocus.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_ulong]
    x.XRaiseWindow.argtypes = [c.c_void_p, c.c_ulong]
    x.XKeysymToKeycode.argtypes = [c.c_void_p, c.c_ulong]
    x.XKeysymToKeycode.restype = c.c_ubyte
    x.XFlush.argtypes = [c.c_void_p]
    xt.XTestFakeKeyEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError("owned display unavailable")

    def property(window, name):
        actual = c.c_ulong()
        fmt = c.c_int()
        count = c.c_ulong()
        remaining = c.c_ulong()
        data = c.c_void_p()
        x.XGetWindowProperty(
            display,
            window,
            x.XInternAtom(display, name.encode(), 0),
            0,
            1024,
            0,
            0,
            c.byref(actual),
            c.byref(fmt),
            c.byref(count),
            c.byref(remaining),
            c.byref(data),
        )
        try:
            return (
                list(c.cast(data, c.POINTER(c.c_ulong))[: count.value])
                if fmt.value == 32 and data
                else []
            )
        finally:
            if data:
                x.XFree(data)

    process = None
    record = {"executed": 0, "passed": 0, "failed": 0, "skipped": 0}

    def check(ok, label):
        record["executed"] += 1
        if not ok:
            record["failed"] += 1
            raise AssertionError(label)
        record["passed"] += 1

    try:
        with tempfile.TemporaryDirectory(
            prefix="relocated-roots-", dir=os.environ["TMPDIR"]
        ) as scratch:
            folder = Path(scratch)
            package = folder / "package"
            package.mkdir()
            working = folder / "unrelated-cwd"
            working.mkdir()
            binary = package / "roots-gui"
            shutil.copy2(args.binary, binary)
            shutil.copytree(
                args.binary.parent / "roots-gui-assets", package / "roots-gui-assets"
            )
            record["originalBinarySha256"] = hashlib.sha256(
                args.binary.read_bytes()
            ).hexdigest()
            check(
                hashlib.sha256(binary.read_bytes()).hexdigest()
                == record["originalBinarySha256"],
                "relocated binary identity",
            )
            check(
                working != binary.parent and working != args.binary.resolve().parent,
                "working directory independent of source and relocated assets",
            )
            record.update(workingDirectory=str(working), relocatedBinary=str(binary))
            with (args.output / "application.log").open("wb") as log:
                process = subprocess.Popen(
                    [str(binary)], cwd=working, stdout=log, stderr=subprocess.STDOUT
                )
                record["appPid"] = process.pid
                deadline = time.monotonic() + 10
                window = None
                while window is None:
                    for candidate in property(
                        x.XDefaultRootWindow(display), "_NET_CLIENT_LIST"
                    ):
                        if property(candidate, "_NET_WM_PID") == [process.pid]:
                            window = candidate
                            break
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("production window not observed")
                    if window is None:
                        time.sleep(0.02)
                check(window is not None, "production window observed")
                extents = property(window, "_NET_FRAME_EXTENTS")
                check(len(extents) == 4 and extents[2] > 0, "native decorated window")
                time.sleep(0.3)
                check(process.poll() is None, "production host remains live")
                if args.inject_failure:
                    check(False, "injected smoke assertion failure")
                x.XRaiseWindow(display, window)
                x.XSetInputFocus(display, window, 2, 0)
                for key, down in [(0xFFE9, 1), (0xFFC1, 1), (0xFFC1, 0), (0xFFE9, 0)]:
                    if not xt.XTestFakeKeyEvent(
                        display, x.XKeysymToKeycode(display, key), down, 0
                    ):
                        raise RuntimeError("WM key injection failed")
                x.XFlush(display)
                result = process.wait(timeout=10)
                check(result == 0, "production WM close exit0")
                deadline = time.monotonic() + 2
                while (
                    window
                    in property(x.XDefaultRootWindow(display), "_NET_CLIENT_LIST")
                    and time.monotonic() < deadline
                ):
                    time.sleep(0.02)
                check(
                    window
                    not in property(x.XDefaultRootWindow(display), "_NET_CLIENT_LIST"),
                    "owned window removed",
                )
                record.update(
                    window=window,
                    nativeFrameExtents=extents,
                    appExit=result,
                    appSettled=True,
                    relocation="binary + licensed font assets only; unrelated working directory; actual default Run path",
                )
    except BaseException as error:
        if record["failed"] == 0:
            record["failed"] += 1
            record["executed"] += 1
        record["error"] = str(error)
        raise
    finally:
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
            record.update(appExit=process.returncode, appSettled=True)
            if record["failed"] == 0:
                record["failed"] += 1
                record["executed"] += 1
        x.XCloseDisplay(display)
        assert record["executed"] == record["passed"] + record["failed"]
        (args.output / "result.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
