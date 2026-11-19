# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Actual X11 clipboard, locale compose and installed Zenity through NativeHost."""

import sys

sys.dont_write_bytecode = True
import argparse
import ctypes
import json
import os
import signal
import subprocess
import time
import traceback
from pathlib import Path
from datetime import datetime, timezone
import gi

gi.require_version("Atspi", "2.0")
gi.require_version("Gtk", "3.0")
from gi.repository import Atspi, GLib, Gtk, Gdk

EXPECTED = (
    "clipboard adapter available",
    "compact native 150%",
    "real keyboard opens fixtures",
    "keyboard-only compact editor navigation",
    "external clipboard paste through native X selection",
    "paste preserves insertion point",
    "paste does not acknowledge draft",
    "native Enter commits exact text",
    "native copy exports exact text",
    "large clipboard retains draft",
    "clipboard timeout is bounded",
    "paste target change cancels request",
    "native sensitive copy exact",
    "hide retires owned sensitive clipboard",
    "hidden native sensitive copy rejected",
    "hide retains foreign clipboard",
    "native public copy while hidden",
    "native owner thread enforced",
    "native reduced motion signal",
    "native motion restoration",
    "native local dead key is uncommitted",
    "native local compose commits Unicode",
    "compose exact host acknowledgement",
    "native global Xft signal rescales",
    "global scale returns",
    "native import route opens dialog",
    "native chooser present",
    "actual chooser framebuffer",
    "actual import result framebuffer",
    "render continues while chooser pending",
    "parent semantic editor disabled while modal",
    "parent action rejected while modal",
    "native chooser cancellation retains path",
    "focus returns to Browse",
    "native chooser exact public path",
    "import remains dependency unavailable",
    "native close cancels chooser",
    "all native resources settled",
)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--binary", type=Path, required=True)
    p.add_argument("--fonts", type=Path, required=True)
    a = p.parse_args()
    out = a.output
    out.mkdir(parents=True)
    (out / "fixture").mkdir()
    checks = []
    keys = []
    records = []
    app = None
    log = None
    error = None
    began = time.monotonic()

    def check(ok, name):
        checks.append(dict(id=name, passed=bool(ok)))
        if not ok:
            raise AssertionError(name)

    def pump(seconds=0.08):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            for _ in range(32):
                if not GLib.MainContext.default().iteration(False):
                    break
            if time.monotonic() - began > 110:
                raise TimeoutError("native input journey watchdog")
            time.sleep(0.005)

    def wait(fn, name, timeout=8):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if app and app.poll() is not None:
                raise RuntimeError("fixture exited during " + name)
            result = fn()
            if result:
                return result
            pump()
        raise TimeoutError(name)

    def state():
        try:
            return json.loads((out / "fixture/state.json").read_text())
        except FileNotFoundError:
            return {}

    x = ctypes.CDLL("libX11.so.6")
    xt = ctypes.CDLL("libXtst.so.6")
    x.XOpenDisplay.restype = ctypes.c_void_p
    x.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
    x.XDefaultRootWindow.restype = ctypes.c_ulong
    x.XCreateSimpleWindow.argtypes = [
        ctypes.c_void_p,
        ctypes.c_ulong,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_uint,
        ctypes.c_uint,
        ctypes.c_uint,
        ctypes.c_ulong,
        ctypes.c_ulong,
    ]
    x.XCreateSimpleWindow.restype = ctypes.c_ulong
    x.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    x.XInternAtom.restype = ctypes.c_ulong
    x.XSetSelectionOwner.argtypes = [
        ctypes.c_void_p,
        ctypes.c_ulong,
        ctypes.c_ulong,
        ctypes.c_ulong,
    ]
    x.XDestroyWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    x.XStringToKeysym.argtypes = [ctypes.c_char_p]
    x.XStringToKeysym.restype = ctypes.c_ulong
    x.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    x.XFlush.argtypes = [ctypes.c_void_p]
    x.XCloseDisplay.argtypes = [ctypes.c_void_p]
    xt.XTestFakeKeyEvent.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint,
        ctypes.c_int,
        ctypes.c_ulong,
    ]
    display = x.XOpenDisplay(None)
    silent = 0

    def key(name, mods=()):
        keys.append(dict(name=name, mods=list(mods)))
        for n, down in (
            [(m, 1) for m in mods]
            + [(name, 1), (name, 0)]
            + [(m, 0) for m in reversed(mods)]
        ):
            code = x.XKeysymToKeycode(display, x.XStringToKeysym(n.encode()))
            if not code:
                raise RuntimeError("missing native keysym " + n)
            xt.XTestFakeKeyEvent(display, code, down, 0)
            x.XFlush(display)
            pump(0.025)

    def named(name):
        todo = [application]
        while todo:
            node = todo.pop(0)
            if node.get_name() == name:
                return node
            todo.extend(
                node.get_child_at_index(i) for i in range(node.get_child_count())
            )

    def focus(name):
        node = wait(lambda: named(name), name)
        if not node.get_component_iface().grab_focus():
            raise AssertionError("focus queue " + name)
        wait(
            lambda: node.get_state_set().contains(Atspi.StateType.FOCUSED),
            "actual focus " + name,
        )
        pump(0.1)
        return node

    native_sequence = 0

    def native(operation):
        nonlocal native_sequence
        native_sequence += 1
        pending = out / "fixture/native-request.pending"
        pending.write_text(str(native_sequence) + "\n" + operation + "\n")
        pending.replace(out / "fixture/native-request")
        path = out / ("fixture/native-receipt-" + str(native_sequence) + ".json")
        wait(path.exists, "native fixture receipt")
        return json.loads(path.read_text())["accepted"]

    def clipboard_text():
        results = []
        clipboard.request_text(lambda _, text, *unused: results.append(text))
        wait(lambda: results, "actual clipboard result")
        return results[0]

    def chooser_ready():
        x.XGetWindowProperty.argtypes = [
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
        x.XFetchName.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_void_p),
        ]
        x.XFree.argtypes = [ctypes.c_void_p]
        kind = ctypes.c_ulong()
        fmt = ctypes.c_int()
        count = ctypes.c_ulong()
        remaining = ctypes.c_ulong()
        data = ctypes.c_void_p()
        x.XGetWindowProperty(
            display,
            x.XDefaultRootWindow(display),
            x.XInternAtom(display, b"_NET_ACTIVE_WINDOW", 0),
            0,
            1,
            0,
            0,
            ctypes.byref(kind),
            ctypes.byref(fmt),
            ctypes.byref(count),
            ctypes.byref(remaining),
            ctypes.byref(data),
        )
        window = (
            ctypes.cast(data, ctypes.POINTER(ctypes.c_ulong))[0] if count.value else 0
        )
        if data.value:
            x.XFree(data)
        if not window:
            return False
        title = ctypes.c_void_p()
        x.XFetchName(display, window, ctypes.byref(title))
        name = ctypes.string_at(title).decode(errors="replace") if title.value else ""
        if title.value:
            x.XFree(title)
        return name == "Choose public wallet or transaction file"

    def capture(tag):
        def rendered():
            window = Gdk.Screen.get_default().get_active_window()
            rect = window.get_frame_extents()
            origin = (rect.x, rect.y)
            width = rect.width
            height = rect.height
            root = Gdk.get_default_root_window()
            pix = Gdk.pixbuf_get_from_window(root, origin[0], origin[1], width, height)
            if pix is None:
                return None
            body = bytes(pix.get_pixels())[
                pix.get_rowstride() * 45 : pix.get_rowstride() * max(45, height - 5)
            ]
            black = body.count(0) / len(body) if body else 1
            return (pix, origin, width, height, black) if black < 0.9 else None

        pix, origin, width, height, black = wait(
            rendered, "actual painted native framebuffer " + tag
        )
        pix.savev(str(out / (tag + ".png")), "png", [], [])
        (out / (tag + ".json")).write_text(
            json.dumps(
                dict(
                    rect=[origin[0], origin[1], width, height],
                    blackBodyByteFraction=black,
                    source="actual authenticated X11 root framebuffer, active native window bounds",
                )
            )
        )

    def press(name):
        node = wait(lambda: named(name), name)
        action = node.get_action_iface()
        if not action or not action.do_action(0):
            raise AssertionError("action " + name)
        pump(0.2)

    try:
        assert os.environ["DISPLAY"] == os.environ["ROOTS_GUI_OWNED_TEST_DISPLAY"]
        Gtk.init([])
        clipboard = Gtk.Clipboard.get(Gdk.SELECTION_CLIPBOARD)
        subprocess.run(
            ["setxkbmap", "-layout", "us", "-variant", "intl"],
            check=True,
            capture_output=True,
            timeout=5,
        )
        env = os.environ | {"XMODIFIERS": "@im=none", "NO_AT_BRIDGE": "0"}
        argv = [str(a.binary), str(out / "fixture"), str(a.fonts), "input"]
        log = (out / "app.log").open("w")
        app = subprocess.Popen(
            argv, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True
        )
        records.append(
            dict(
                kind="application",
                pid=app.pid,
                pgid=app.pid,
                argv=argv,
                startUTC=datetime.now(timezone.utc).isoformat(),
            )
        )
        (out / "processes-live.json").write_text(json.dumps(records))
        wait(lambda: state().get("registered"), "actual native accessibility")
        Atspi.init()
        desktop = Atspi.get_desktop(0)
        application = wait(
            lambda: next(
                (
                    desktop.get_child_at_index(i)
                    for i in range(desktop.get_child_count())
                    if desktop.get_child_at_index(i).get_name() == "Bitcoin Roots"
                ),
                None,
            ),
            "production app",
        )
        check(state()["input"]["clipboard"], "clipboard adapter available")
        check(
            state()["width"] == 960
            and state()["height"] == 640
            and state()["scale"] == 1.5,
            "compact native 150%",
        )
        key("o", ("Control_L",))
        wait(lambda: state().get("documents") == 3, "open fixture")
        check(True, "real keyboard opens fixtures")
        for _ in range(32):
            label = named("Label")
            if label and label.get_state_set().contains(Atspi.StateType.FOCUSED):
                break
            key("Tab")
        else:
            raise AssertionError("compact keyboard traversal did not reach Label")
        check(True, "keyboard-only compact editor navigation")
        key("a", ("Control_L",))
        clipboard.set_text("Public é##2026", -1)
        pump()
        key("v", ("Control_L",))
        wait(lambda: state().get("note") == "Public é##2026", "external paste")
        check(
            state()["input"]["reads"] > 0 and state()["input"]["pastes"] == 1,
            "external clipboard paste through native X selection",
        )
        key("Left")
        clipboard.set_text("X", -1)
        pump()
        key("v", ("Control_L",))
        wait(lambda: state().get("note") == "Public é##202X6", "insertion")
        check(True, "paste preserves insertion point")
        check(state()["saved"] != "Public é##202X6", "paste does not acknowledge draft")
        key("Return")
        wait(lambda: state().get("saved") == "Public é##202X6", "commit paste")
        check(True, "native Enter commits exact text")
        focus("Label")
        key("a", ("Control_L",))
        key("c", ("Control_L",))
        value = []
        clipboard.request_text(lambda _, text, *unused: value.append(text))
        wait(lambda: value, "clipboard consumer")
        check(
            value[0] == "Public é##202X6" and state()["input"]["writes"] > 0,
            "native copy exports exact text",
        )
        clipboard.set_text("z" * 70000, -1)
        pump()
        key("v", ("Control_L",))
        wait(
            lambda: "limit" in state().get("input", {}).get("notice", ""),
            "large transfer",
        )
        check(state()["note"] == "Public é##202X6", "large clipboard retains draft")
        silent = x.XCreateSimpleWindow(
            display, x.XDefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0
        )
        x.XSetSelectionOwner(
            display, x.XInternAtom(display, b"CLIPBOARD", 0), silent, 0
        )
        x.XFlush(display)
        before = state()["frames"]
        start = time.monotonic()
        key("v", ("Control_L",))
        wait(
            lambda: "timed out" in state().get("input", {}).get("notice", ""),
            "clipboard timeout",
            4,
        )
        check(
            time.monotonic() - start < 3.5
            and state()["frames"] > before + 3
            and state()["note"] == "Public é##202X6",
            "clipboard timeout is bounded",
        )
        before_cancel = state()["input"]["cancelled"]
        key("v", ("Control_L",))
        key("2", ("Control_L",))
        wait(
            lambda: (
                state()["input"]["callbacks"] == 0
                and state()["input"]["cancelled"] > before_cancel
            ),
            "retired paste",
        )
        key("1", ("Control_L",))
        wait(lambda: state()["note"] == "Public é##202X6", "section draft restored")
        check(True, "paste target change cancels request")
        x.XDestroyWindow(display, silent)
        silent = 0
        focus("Label")
        key("a", ("Control_L",))
        key("BackSpace")
        wait(lambda: state().get("note") == "", "clear draft")
        key("dead_acute")
        check(state()["note"] == "", "native local dead key is uncommitted")
        key("e")
        wait(lambda: state().get("note") == "é", "XIM commit")
        check(True, "native local compose commits Unicode")
        key("Return")
        wait(lambda: state().get("saved") == "é", "exact compose acknowledgement")
        check(True, "compose exact host acknowledgement")
        subprocess.run(
            ["xrdb", "-load"],
            input="Xft.dpi: 144\n",
            text=True,
            capture_output=True,
            check=True,
            timeout=5,
        )
        wait(
            lambda: (
                state().get("input", {}).get("scale") == 1.5
                and state().get("scale") == 2.25
            ),
            "real X resource scale",
        )
        check(True, "native global Xft signal rescales")
        subprocess.run(
            ["xrdb", "-load"],
            input="Xft.dpi: 96\n",
            text=True,
            capture_output=True,
            check=True,
            timeout=5,
        )
        wait(lambda: state().get("scale") == 1.5, "scale restore")
        check(True, "global scale returns")
        check(
            native("copy-sensitive") and clipboard_text() == "0.12345678",
            "native sensitive copy exact",
        )
        key("h", ("Control_L", "Shift_L"))
        wait(lambda: state()["hidden"], "hide native")
        check(clipboard_text() is None, "hide retires owned sensitive clipboard")
        check(
            not native("copy-sensitive") and clipboard_text() is None,
            "hidden native sensitive copy rejected",
        )
        clipboard.set_text("Foreign public clipboard", -1)
        pump()
        key("h", ("Control_L", "Shift_L"))
        wait(lambda: not state()["hidden"], "show native")
        key("h", ("Control_L", "Shift_L"))
        wait(lambda: state()["hidden"], "hide again")
        check(
            clipboard_text() == "Foreign public clipboard",
            "hide retains foreign clipboard",
        )
        check(
            native("copy-public") and clipboard_text() == "Public fixture identifier",
            "native public copy while hidden",
        )
        check(native("wrong-thread"), "native owner thread enforced")
        assert native("motion-off")
        wait(lambda: state()["input"]["reduced"], "actual GSettings reduced motion")
        check(True, "native reduced motion signal")
        assert native("motion-on")
        wait(
            lambda: not state()["input"]["reduced"],
            "actual GSettings animation restoration",
        )
        check(True, "native motion restoration")
        key("h", ("Control_L", "Shift_L"))
        wait(lambda: not state()["hidden"], "show after adapter checks")
        key("Alt_L")
        key("Down")
        key("Down")
        key("Down")
        key("Return")
        wait(lambda: named("Public import path"), "import dialog")
        check(True, "native import route opens dialog")
        focus("Choose public file")
        key("Return")
        wait(lambda: state().get("input", {}).get("active"), "native chooser")
        wait(chooser_ready, "actual native chooser focus")
        check(bool(state()["input"]["pid"]), "native chooser present")
        capture("native-chooser")
        check(True, "actual chooser framebuffer")
        before = state()["frames"]
        pump(0.3)
        check(state()["frames"] > before + 2, "render continues while chooser pending")
        editor = wait(lambda: named("Public import path"), "parent editor")
        check(
            not editor.get_state_set().contains(Atspi.StateType.ENABLED),
            "parent semantic editor disabled while modal",
        )
        editable = editor.get_editable_text_iface()
        check(
            (editable is None or not editable.set_text_contents("wrong-target.psbt"))
            and state()["importPath"] == "",
            "parent action rejected while modal",
        )
        key("Escape")
        wait(lambda: not state()["input"]["active"], "native cancel")
        check(state()["importPath"] == "", "native chooser cancellation retains path")
        focusnode = wait(lambda: named("Choose public file"), "returned Browse")
        wait(
            lambda: focusnode.get_state_set().contains(Atspi.StateType.FOCUSED),
            "returned focus",
        )
        check(True, "focus returns to Browse")
        # Literal path is selected by GTK. Shell classifies it but cannot import/read it.
        chosen = out / "public ## $(literal).psbt"
        chosen.write_text("public non-secret fixture\n")
        key("Return")
        wait(lambda: state()["input"]["active"], "second chooser")
        wait(chooser_ready, "actual second chooser focus")
        key("l", ("Control_L",))
        pump(0.1)
        # Native GTK clipboard, used by GTK's actual path entry rather than ImGui.
        clipboard.set_text(str(chosen), -1)
        pump()
        key("v", ("Control_L",))
        key("Return")
        wait(lambda: not state()["input"]["active"], "native chosen", 10)
        wait(lambda: state()["importPath"] == str(chosen), "exact choice")
        check(True, "native chooser exact public path")
        pump(0.15)
        capture("native-import-result")
        check(True, "actual import result framebuffer")
        node = wait(lambda: named("Request Import"), "request import")
        check(
            not node.get_state_set().contains(Atspi.StateType.ENABLED),
            "import remains dependency unavailable",
        )
        focus("Choose public file")
        key("Return")
        wait(lambda: state()["input"]["active"], "close pending chooser")
        child_pid = int(state()["input"]["pid"])
        # Real WM protocol request reaches GLFW's native close callback even while a
        # transient child owns keyboard focus. No fixture-only close flag is used.
        x.XQueryTree.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
            ctypes.POINTER(ctypes.c_uint),
        ]
        x.XFetchName.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_void_p),
        ]
        x.XFree.argtypes = [ctypes.c_void_p]

        def find_window(window):
            title = ctypes.c_void_p()
            x.XFetchName(display, window, ctypes.byref(title))
            name = (
                ctypes.string_at(title).decode(errors="replace") if title.value else ""
            )
            if title.value:
                x.XFree(title)
            if name == "Bitcoin Roots":
                return window
            root = ctypes.c_ulong()
            parent = ctypes.c_ulong()
            children = ctypes.POINTER(ctypes.c_ulong)()
            count = ctypes.c_uint()
            x.XQueryTree(
                display,
                window,
                ctypes.byref(root),
                ctypes.byref(parent),
                ctypes.byref(children),
                ctypes.byref(count),
            )
            ids = [children[i] for i in range(count.value)]
            if children:
                x.XFree(children)
            return next(
                (result for child in ids if (result := find_window(child))), None
            )

        window = find_window(x.XDefaultRootWindow(display))
        if not window:
            raise RuntimeError("actual GLFW native window missing")

        class Client(ctypes.Structure):
            _fields_ = [
                ("type", ctypes.c_int),
                ("serial", ctypes.c_ulong),
                ("send_event", ctypes.c_int),
                ("display", ctypes.c_void_p),
                ("window", ctypes.c_ulong),
                ("message_type", ctypes.c_ulong),
                ("format", ctypes.c_int),
                ("data", ctypes.c_long * 5),
            ]

        class Event(ctypes.Union):
            _fields_ = [("client", Client), ("pad", ctypes.c_long * 24)]

        event = Event()
        event.client = Client(
            33,
            0,
            1,
            display,
            window,
            x.XInternAtom(display, b"WM_PROTOCOLS", 0),
            32,
            (ctypes.c_long * 5)(
                x.XInternAtom(display, b"WM_DELETE_WINDOW", 0), 0, 0, 0, 0
            ),
        )
        x.XSendEvent.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.c_int,
            ctypes.c_long,
            ctypes.POINTER(Event),
        ]
        x.XSendEvent(display, window, 0, 0, ctypes.byref(event))
        x.XFlush(display)
        app.wait(timeout=5)
        check(
            app.returncode == 0 and not Path("/proc/" + str(child_pid)).exists(),
            "native close cancels chooser",
        )
        check(
            json.loads((out / "fixture/teardown.json").read_text())["cleaned"],
            "all native resources settled",
        )
    except Exception as exc:
        error = str(exc)
        (out / "exception.txt").write_text(traceback.format_exc())
        try:
            todo = [Atspi.get_desktop(0)]
            nodes = []
            while todo:
                n = todo.pop(0)
                nodes.append(
                    dict(
                        name=n.get_name(),
                        role=n.get_role_name(),
                        focused=n.get_state_set().contains(Atspi.StateType.FOCUSED),
                    )
                )
                todo.extend(n.get_child_at_index(i) for i in range(n.get_child_count()))
            (out / "failure-desktop.json").write_text(json.dumps(nodes, indent=2))
            root = Gdk.get_default_root_window()
            pix = Gdk.pixbuf_get_from_window(
                root, 0, 0, root.get_width(), root.get_height()
            )
            pix.savev(str(out / "failure-desktop.png"), "png", [], [])
        except Exception as detail:
            (out / "capture-error.txt").write_text(str(detail))
    finally:
        if app and app.poll() is None:
            os.killpg(app.pid, signal.SIGTERM)
            try:
                app.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(app.pid, signal.SIGKILL)
                app.wait(timeout=3)
        if log:
            log.close()
        if silent:
            x.XDestroyWindow(display, silent)
        if display:
            x.XCloseDisplay(display)
        if records:
            records[0].update(
                exit=app.returncode,
                endUTC=datetime.now(timezone.utc).isoformat(),
                settled=app.poll() is not None,
            )
        (out / "processes.json").write_text(json.dumps(records, indent=2) + "\n")
        observed = {c["id"] for c in checks}
        (out / "result.json").write_text(
            json.dumps(
                dict(
                    selected=len(EXPECTED),
                    executed=len(checks),
                    passed=sum(c["passed"] for c in checks),
                    failed=sum(not c["passed"] for c in checks),
                    skipped=0,
                    unexecuted=sorted(set(EXPECTED) - observed),
                    unexpected=sorted(observed - set(EXPECTED)),
                    checks=checks,
                    error=error,
                    keys=keys,
                    qualification="Real native locale dead-key composition and global Xft resource change only. External preedit/candidate positioning and true mixed-monitor DPI movement remain unexecuted.",
                ),
                indent=2,
                ensure_ascii=False,
            )
            + "\n"
        )
    return int(error is not None)


if __name__ == "__main__":
    raise SystemExit(main())
