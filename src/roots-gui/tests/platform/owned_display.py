# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Authenticated child-owned Xvfb + real Marco WM; no installation or user config."""

import sys

sys.dont_write_bytecode = True
import argparse
import ctypes
import fcntl
import json
import os
from pathlib import Path
import secrets
import signal
import struct
import subprocess
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import validated_scratch, evidence_directory


def reserve_display(scratch):
    # Keep the reservation inode and lock until Xvfb has exited. Unlinking a
    # lock file could allow two owners to lock different inodes for one number.
    for number in range(711, 811):
        path = Path(scratch) / ("owned-x-display-" + str(number) + ".lock")
        fd = os.open(path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
        try:
            if os.fstat(fd).st_uid != os.getuid():
                raise RuntimeError("foreign display reservation")
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            socket = Path("/tmp/.X11-unix") / ("X" + str(number))
            if socket.exists() or Path("/tmp/.X" + str(number) + "-lock").exists():
                os.close(fd)
                continue
            return number, socket, fd
        except BlockingIOError:
            os.close(fd)
        except BaseException:
            os.close(fd)
            raise
    raise RuntimeError("no free owned display")


def main():
    def interrupted(*_):
        raise RuntimeError("owned display interrupted")

    signal.signal(signal.SIGTERM, interrupted)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--dpi", type=int, default=96)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = (
        args.command[1:] if args.command and args.command[0] == "--" else args.command
    )
    if not command:
        parser.error("a command is required")
    args.output = evidence_directory(args.output)
    xlib = ctypes.CDLL("libX11.so.6")
    xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
    xlib.XOpenDisplay.restype = ctypes.c_void_p
    xlib.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
    xlib.XDefaultRootWindow.restype = ctypes.c_ulong
    xlib.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    xlib.XInternAtom.restype = ctypes.c_ulong
    xlib.XGetWindowProperty.argtypes = [
        ctypes.c_void_p,
        ctypes.c_ulong,
        ctypes.c_ulong,
        ctypes.c_long,
        ctypes.c_long,
        ctypes.c_int,
        ctypes.c_ulong,
        ctypes.POINTER(ctypes.c_ulong),
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_ulong),
        ctypes.POINTER(ctypes.c_ulong),
        ctypes.POINTER(ctypes.c_void_p),
    ]
    xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
    xlib.XFree.argtypes = [ctypes.c_void_p]
    record = {"command": command, "dpi": args.dpi, "processes": [], "result": None}
    scratch_root = validated_scratch()
    literal = os.environ.get("TMPDIR")
    if literal and Path(literal).resolve() == scratch_root:
        scratch_root = literal
    with tempfile.TemporaryDirectory(
        prefix="roots-native-", dir=scratch_root
    ) as scratch:
        folder = Path(scratch)
        number, socket, reservation = reserve_display(scratch_root)
        auth = folder / "Xauthority"

        def field(value):
            return struct.pack("!H", len(value)) + value

        auth.write_bytes(
            struct.pack("!H", 65535)
            + field(b"")
            + field(str(number).encode())
            + field(b"MIT-MAGIC-COOKIE-1")
            + field(secrets.token_bytes(16))
        )
        auth.chmod(0o600)
        env = os.environ | {
            "DISPLAY": ":" + str(number),
            "XAUTHORITY": str(auth),
            "ROOTS_GUI_OWNED_TEST_DISPLAY": ":" + str(number),
            "LIBGL_ALWAYS_SOFTWARE": "1",
            "GSETTINGS_BACKEND": "memory",
            "NO_AT_BRIDGE": "1",
            "XDG_CONFIG_HOME": str(folder / "config"),
            "XDG_CACHE_HOME": str(folder / "cache"),
            "XDG_DATA_HOME": str(folder / "data"),
            "XDG_RUNTIME_DIR": str(folder / "runtime"),
        }
        for name in ["config", "cache", "data", "runtime"]:
            (folder / name).mkdir(mode=0o700)
        logs = [
            (args.output / n).open("wb") for n in ["xvfb.log", "wm.log", "command.log"]
        ]
        server = wm = child = None
        try:
            argv = [
                "Xvfb",
                ":" + str(number),
                "-screen",
                "0",
                "3200x2200x24",
                "-nolisten",
                "tcp",
                "-noreset",
                "-auth",
                str(auth),
                "-dpi",
                str(args.dpi),
            ]
            server = subprocess.Popen(
                argv, env=env, stdout=logs[0], stderr=subprocess.STDOUT
            )
            record["processes"].append(
                {"kind": "Xvfb", "pid": server.pid, "argv": argv}
            )
            deadline = time.monotonic() + 10
            while not socket.exists():
                if server.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("owned display failed readiness")
                time.sleep(0.02)
            subprocess.run(
                ["xrdb", "-load"],
                input=f"Xft.dpi: {args.dpi}\n",
                env=env,
                text=True,
                check=True,
            )
            argv = [
                "dbus-run-session",
                "--",
                "marco",
                "--sm-disable",
                "--no-composite",
                "--replace",
            ]
            wm = subprocess.Popen(
                argv,
                env=env,
                stdout=logs[1],
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            record["processes"].append(
                {
                    "kind": "Marco/private-D-Bus",
                    "pid": wm.pid,
                    "pgid": wm.pid,
                    "argv": argv,
                }
            )
            # Use a separate authenticated connection without changing this process's environment.
            saved_auth = os.environ.get("XAUTHORITY")
            os.environ["XAUTHORITY"] = str(auth)
            try:
                display = xlib.XOpenDisplay(env["DISPLAY"].encode())
            finally:
                if saved_auth is None:
                    os.environ.pop("XAUTHORITY", None)
                else:
                    os.environ["XAUTHORITY"] = saved_auth
            if not display:
                raise RuntimeError("owned display authentication failed")
            try:
                atom = xlib.XInternAtom(display, b"_NET_SUPPORTING_WM_CHECK", 0)
                deadline = time.monotonic() + 10
                while True:
                    actual = ctypes.c_ulong()
                    fmt = ctypes.c_int()
                    count = ctypes.c_ulong()
                    remaining = ctypes.c_ulong()
                    data = ctypes.c_void_p()
                    xlib.XGetWindowProperty(
                        display,
                        xlib.XDefaultRootWindow(display),
                        atom,
                        0,
                        1,
                        0,
                        0,
                        ctypes.byref(actual),
                        ctypes.byref(fmt),
                        ctypes.byref(count),
                        ctypes.byref(remaining),
                        ctypes.byref(data),
                    )
                    ready = count.value == 1
                    if data:
                        xlib.XFree(data)
                    if ready:
                        break
                    if wm.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("real WM failed readiness")
                    time.sleep(0.02)
            finally:
                xlib.XCloseDisplay(display)
            child = subprocess.Popen(
                command, env=env, stdout=logs[2], stderr=subprocess.STDOUT
            )
            try:
                child.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=5)
                record["timedOut"] = True
            record["result"] = child.returncode
            record["child"] = {
                "pid": child.pid,
                "exit": child.returncode,
                "settled": child.poll() is not None,
            }
        finally:
            if child:
                if child.poll() is None:
                    child.terminate()
                    try:
                        child.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait(timeout=5)
                record["child"] = {
                    "pid": child.pid,
                    "exit": child.returncode,
                    "settled": child.poll() is not None,
                }
                if record["result"] is None:
                    record["result"] = 1
            if wm:
                try:
                    os.killpg(wm.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    wm.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(wm.pid, signal.SIGKILL)
                    wm.wait(timeout=5)
                record["processes"][1].update(
                    exit=wm.returncode, settled=wm.poll() is not None
                )

                def live_members():
                    members = []
                    for status in Path("/proc").glob("[0-9]*/stat"):
                        try:
                            fields = status.read_text().rsplit(")", 1)[1].split()
                            if int(fields[2]) == wm.pid and fields[0] != "Z":
                                members.append(int(status.parent.name))
                        except (OSError, ValueError, IndexError):
                            pass
                    return members

                deadline = time.monotonic() + 2
                while (live := live_members()) and time.monotonic() < deadline:
                    time.sleep(0.02)
                if live:
                    os.killpg(wm.pid, signal.SIGKILL)
                    deadline = time.monotonic() + 2
                    while (live := live_members()) and time.monotonic() < deadline:
                        time.sleep(0.02)
                record["processes"][1]["remainingLiveGroupMembers"] = live
                if live:
                    record["result"] = 1
            if server:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=5)
                record["processes"][0].update(
                    exit=server.returncode, settled=server.poll() is not None
                )
            for log in logs:
                log.close()
            record["auth"] = (
                "ephemeral local test cookie removed with owned scratch; no cookie logged"
            )
            record["display"] = env["DISPLAY"]
            (args.output / "processes.json").write_text(
                json.dumps(record, indent=2) + "\n"
            )
            os.close(reservation)
    return record["result"] if record["result"] is not None else 1


if __name__ == "__main__":
    raise SystemExit(main())
