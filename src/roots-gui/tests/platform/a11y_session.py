# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Real private AT-SPI bus + independent libatspi client and optional native Orca."""

import sys

sys.dont_write_bytecode = True
import argparse
from datetime import datetime, timezone
import ctypes
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import tempfile
import time
import traceback
import pty
import threading
import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, Gio, GLib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import validated_scratch


EXPECTED_CHECKS = (
    "real libatspi event registration",
    "real native-window event registration",
    "actual Application toolkit contract",
    "empty workspace exported",
    "real menu header action posts File",
    "published platform shortcut matches native Ctrl+O route",
    "wallet and transaction tabs exported",
    "actual production table rows",
    "literal table label is available through AT-SPI",
    "exact financial text through actual table cell",
    "native Tab reaches intended text field",
    "screen-reader row focus enters actual table",
    "native Enter acknowledges actual table selection",
    "external focus returns from table to editor",
    "independent client observes native committed text",
    "actual native caret offset exported",
    "real D-Bus edit returns actual host acknowledgement",
    "one external edit produces one typed host effect",
    "Unicode insertion accepts byte length at character position",
    "external focus request from active editor to tab acknowledged",
    "external focus returns to actual editor",
    "invalid external edit rejected by production validator",
    "invalid draft retained without host effect",
    "valid external retry recovers same editor",
    "external focus converges on native field",
    "modal state is exported",
    "actual focus returns to stable document tab",
    "actual status action opens retained details",
    "status change is readable through native text",
    "hidden amount absent from current exported name/description/text",
    "privacy transition events do not republish old amount",
    "retained object path rejects access after lock",
    "failure injection targets only owned private accessibility bus",
    "native render owner remains live after bus failure",
    "native bridge closes before ImGui and window teardown",
    "real focus-change event delivered",
    "real text-change event delivered",
    "journey completed without harness error",
)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", type=Path, required=True)
    p.add_argument("--fonts", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--orca", action="store_true")
    p.add_argument("--send-fixture", nargs=3, metavar=("PUBLIC", "PORT", "COOKIE"))
    p.add_argument(
        "--orca-output", choices=["speech", "braille-monitor"], default="speech"
    )
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True)
    expected = set(EXPECTED_CHECKS) | {"actual native table cell roles and positions"}
    if a.orca:
        expected -= {
            "failure injection targets only owned private accessibility bus",
            "native render owner remains live after bus failure",
        }
        expected |= {
            "actual Orca remains running before application launch",
            "actual Orca survived complete native journey",
            "actual Orca generated spoken field content",
            "actual Orca read committed keyboard edit",
        }
        if a.orca_output == "braille-monitor":
            expected -= {
                "actual Orca generated spoken field content",
                "actual Orca read committed keyboard edit",
            }
            expected |= {
                "actual Orca generated braille field content",
                "actual Orca braille read committed keyboard edit",
                "actual Orca braille monitor is visible",
            }
            expected |= {
                "actual Orca braille keyboard-edit",
                "actual monitor capture edit",
                "actual Orca braille table-amount",
                "actual Orca braille dialog-focus",
                "actual Orca braille status-detail",
                "actual Orca braille hidden-amount",
                "actual monitor capture hidden",
                "actual Orca braille locked-focus",
                "actual Orca native navigator command",
                "reader row focus for native navigation",
                "reader returns to editor after table inspection",
                "hidden row focus for reader",
                "pre-lock actual editor focus",
            }
    if a.send_fixture:
        if not a.orca or a.orca_output != "braille-monitor":
            p.error("--send-fixture requires the actual Orca braille monitor")
        expected = {
            "real libatspi event registration",
            "real native-window event registration",
            "actual Orca remains running before application launch",
            "actual Application toolkit contract",
            "actual native application publishes German locale",
            "native reviewed submission and monitor settled",
            "actual Orca survived complete native journey",
            "actual monitor capture review-keyboard",
            "real focus-change event delivered",
            "journey completed without harness error",
        }
        expected.update(f"actual Orca braille keyboard-{step}" for step in range(14))
        expected.update(
            f"actual Orca braille keyboard-{step}-entered" for step in (1, 2)
        )
        expected.update(
            f"actual enabled focus and keyboard action {step}" for step in range(14)
        )
    records = []
    checks = []
    events = []
    keys = []
    processes = []
    streams = []
    scratch_root = validated_scratch()
    literal = os.environ.get("TMPDIR")
    if literal and Path(literal).resolve() == scratch_root:
        scratch_root = literal
    scratch = tempfile.TemporaryDirectory(prefix="roots-atspi-", dir=scratch_root)
    folder = Path(scratch.name)
    env = os.environ.copy()
    env.pop("AT_SPI_BUS_ADDRESS", None)
    env.update(NO_AT_BRIDGE="0", GSETTINGS_BACKEND="memory")
    started = time.monotonic()
    phase = "initial"
    app = None
    reader = None
    speech_ready = False
    reader_pty = None
    reader_collector = None
    collector_errors = []

    def live_records():
        (out / "processes-live.pending").write_text(
            json.dumps(records, indent=2) + "\n"
        )
        (out / "processes-live.pending").replace(out / "processes-live.json")

    def launch(kind, argv, *, environment=None, **kwargs):
        log = (out / (kind + ".log")).open("w")
        streams.append(log)
        proc = subprocess.Popen(
            list(map(str, argv)),
            stdout=log,
            stderr=subprocess.STDOUT,
            env=env if environment is None else environment,
            start_new_session=True,
            **kwargs,
        )
        processes.append((kind, proc))
        records.append(
            dict(
                kind=kind,
                pid=proc.pid,
                pgid=proc.pid,
                argv=list(map(str, argv)),
                startUTC=datetime.now(timezone.utc).isoformat(),
            )
        )
        live_records()
        return proc

    def check(ok, label):
        checks.append(dict(id=label, passed=bool(ok)))
        if not ok:
            raise AssertionError(label)

    def pump(seconds=0.05):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            for _ in range(32):
                if not GLib.MainContext.default().iteration(False):
                    break
            if time.monotonic() - started > (
                205 if a.send_fixture else 230 if a.orca else 180
            ):
                raise TimeoutError("real accessibility journey deadline")
            time.sleep(0.005)

    def wait(predicate, label, timeout=6):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if app is not None and app.poll() is not None:
                raise RuntimeError("fixture exited during " + label)
            value = predicate()
            if value:
                return value
            pump()
        raise TimeoutError(label)

    def state():
        try:
            return json.loads((out / "fixture/state.json").read_text())
        except FileNotFoundError:
            return {}

    x = ctypes.CDLL("libX11.so.6")
    xt = ctypes.CDLL("libXtst.so.6")
    x.XOpenDisplay.restype = ctypes.c_void_p
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

    def key(name, modifiers=()):
        keys.append(dict(phase=phase, key=name, modifiers=list(modifiers)))

        def send(n, down):
            code = x.XKeysymToKeycode(display, x.XStringToKeysym(n.encode()))
            if not code:
                raise RuntimeError("Unknown native keysym " + n)
            if not xt.XTestFakeKeyEvent(display, code, down, 0):
                raise RuntimeError("native input injection failed")

        for m in modifiers:
            send(m, 1)
        send(name, 1)
        x.XFlush(display)
        if a.send_fixture:
            # D-Bus dispatch can be busy with reader events. Release the
            # physical key on time even when reader callbacks are delayed.
            time.sleep(0.025)
        else:
            pump(0.035)
        send(name, 0)
        for m in reversed(modifiers):
            send(m, 0)
        x.XFlush(display)
        if not a.send_fixture:
            pump(0.10)

    def type_text(text):
        for ch in text:
            key("space" if ch == " " else ch)

    def read_tree(root):
        result = []
        queue = [root]
        while queue:
            n = queue.pop(0)
            result.append(n)
            if len(result) > 260:
                raise RuntimeError("unexpected exported tree capacity")
            queue.extend(n.get_child_at_index(i) for i in range(n.get_child_count()))
        return result

    def tree(root):
        for attempt in range(4):
            try:
                return read_tree(root)
            except GLib.Error as e:
                events.append(
                    dict(phase=phase, transientTreeRead=str(e), attempt=attempt)
                )
                pump(0.08)
        return read_tree(root)

    def named(name, role=None):
        for n in tree(application):
            if n.get_name() == name and (role is None or n.get_role() == role):
                return n

    def focused():
        return [
            n
            for n in tree(application)
            if n.get_state_set().contains(Atspi.StateType.FOCUSED)
        ]

    def event(event, _):
        try:
            events.append(
                dict(
                    phase=phase,
                    type=event.type,
                    name=event.source.get_name(),
                    detail1=event.detail1,
                    detail2=event.detail2,
                    data=str(event.any_data),
                )
            )
        except GLib.Error:
            events.append(dict(phase=phase, type=event.type, defunct=True))

    def reader_debug_transport():
        nonlocal reader_pty, reader_collector
        master, slave = pty.openpty()
        reader_pty = (master, slave)

        def collect():
            try:
                with (out / "orca-debug.log").open("wb") as output:
                    while True:
                        try:
                            data = os.read(master, 65536)
                        except OSError as exc:
                            if exc.errno == 5:
                                break  # Actual reader closed its PTY slave.
                            raise
                        if not data:
                            break
                        output.write(data)
                        output.flush()
            except Exception as exc:
                collector_errors.append(str(exc))

        reader_collector = threading.Thread(
            target=collect, name="owned-orca-debug-collector"
        )
        reader_collector.start()
        return os.ttyname(slave)

    reader_receipts = []

    def reader_mark():
        return (
            len((out / "orca-debug.log").read_text(errors="replace")) if a.orca else 0
        )

    def reader_checkpoint(tag, token, mark, forbidden=None):
        if not a.orca:
            return

        def output():
            text = (out / "orca-debug.log").read_text(errors="replace")[mark:]
            return [line for line in text.splitlines() if "BRAILLE LINE:" in line]

        lines = wait(
            lambda: (
                (values if any(token in line for line in values) else None)
                if (values := output())
                else None
            ),
            "actual Orca braille " + tag,
            15 if a.send_fixture else 8,
        )
        if forbidden and any(forbidden in line for line in lines):
            raise AssertionError("reader privacy output leaked " + tag)
        reader_receipts.append(
            dict(tag=tag, token=token, lines=lines, forbidden=forbidden)
        )
        check(True, "actual Orca braille " + tag)

    def monitor_capture(tag, token):
        if not a.orca:
            return
        apps = [desktop.get_child_at_index(i) for i in range(desktop.get_child_count())]
        monitor = wait(
            lambda: next(
                (
                    app.get_child_at_index(i)
                    for app in apps
                    for i in range(app.get_child_count())
                    if app.get_child_at_index(i).get_name() == "Braille Monitor"
                ),
                None,
            ),
            "actual monitor window",
        )

        def cell_text():
            queue = [monitor]
            chars = []
            count = 0
            nodes = []
            while queue:
                node = queue.pop(0)
                count += 1
                if count > 1500:
                    raise RuntimeError("monitor traversal bound")
                role = node.get_role()
                name = node.get_name()
                nodes.append(
                    dict(role=str(role), name=name, children=node.get_child_count())
                )
                if role == Atspi.Role.LABEL and name not in ["○", "●"]:
                    chars.append(name)
                queue.extend(
                    node.get_child_at_index(i) for i in range(node.get_child_count())
                )
            (out / ("monitor-" + tag + "-tree.json")).write_text(
                json.dumps(nodes, indent=2, ensure_ascii=False) + "\n"
            )
            return "".join(chars)

        rect = monitor.get_component_iface().get_extents(Atspi.CoordType.SCREEN)
        gi.require_version("Gdk", "3.0")
        from gi.repository import Gdk

        Gdk.init([])
        pixbuf = Gdk.pixbuf_get_from_window(
            Gdk.get_default_root_window(), rect.x, rect.y, rect.width, rect.height
        )
        if pixbuf is None:
            raise RuntimeError("native monitor capture unavailable")
        pixbuf.savev(str(out / ("monitor-" + tag + ".png")), "png", [], [])
        cell_text()  # Installed Gtk BrlCell exposes only decorative dot labels.
        lines = [
            line
            for line in (out / "orca-debug.log")
            .read_text(errors="replace")
            .splitlines()
            if "VISIBLE:" in line
        ]
        text = lines[-1] if lines else ""
        if token not in text:
            raise AssertionError(
                "reader visible output differs from requested capture phase"
            )
        reader_receipts.append(
            dict(
                tag=tag + " visible monitor",
                readerVisible=text,
                qualification="Actual native PNG requires bounded visual inspection; Gtk monitor button names expose decorative dots, not cell text.",
                rect=[rect.x, rect.y, rect.width, rect.height],
            )
        )
        check(True, "actual monitor capture " + tag)

    application = desktop = None

    def baseline_journey():
        nonlocal app, application, desktop, phase
        (out / "fixture").mkdir()
        app = launch(
            "application", [a.binary.resolve(), out / "fixture", a.fonts.resolve()]
        )
        wait(lambda: state().get("registered"), "actual production bridge registration")
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
            "registry discovery by independent libatspi client",
        )
        check(
            application.get_toolkit_name() == "Roots ImGui",
            "actual Application toolkit contract",
        )
        check(named("Open wallet") is not None, "empty workspace exported")
        check(
            named("File").get_action_iface().do_action(0),
            "real menu header action posts File",
        )
        menu_open = wait(
            lambda: named("Open Wallet", Atspi.Role.MENU_ITEM),
            "actual File command publication",
        )
        check(
            Atspi.Action.get_key_binding(menu_open, 0) == ";;<Control>o",
            "published platform shortcut matches native Ctrl+O route",
        )
        key("Escape")
        wait(
            lambda: named("Open Wallet", Atspi.Role.MENU_ITEM) is None,
            "native menu dismissal",
        )
        phase = "keyboard-open"
        key("o", ("Control_L",))
        wait(
            lambda: state().get("documents") == 3,
            "native keyboard opens fixture documents",
        )
        wait(
            lambda: named("Label", Atspi.Role.ENTRY), "production text field after open"
        )
        tabs = [n for n in tree(application) if n.get_role() == Atspi.Role.PAGE_TAB]
        check(len(tabs) == 3, "wallet and transaction tabs exported")
        # Query the native IDs even though this check only needs their availability.
        [n.get_accessible_id() for n in tabs]
        table = next(n for n in tree(application) if n.is_table())
        check(
            table.get_table_iface().get_n_rows() == 40, "actual production table rows"
        )
        check(
            all(
                table.get_table_iface().get_accessible_at(0, col).get_role()
                == Atspi.Role.TABLE_CELL
                and tuple(
                    Atspi.TableCell.get_position(
                        table.get_table_iface().get_accessible_at(0, col)
                    )
                )[-2:]
                == (0, col)
                for col in range(3)
            ),
            "actual native table cell roles and positions",
        )
        check(
            "Invoice##2026"
            in Atspi.Text.get_text(
                table.get_table_iface().get_accessible_at(0, 0), 0, -1
            ),
            "literal table label is available through AT-SPI",
        )
        check(
            Atspi.Text.get_text(table.get_table_iface().get_accessible_at(0, 1), 0, -1)
            == "0.01000000",
            "exact financial text through actual table cell",
        )
        phase = "keyboard-edit"
        for _ in range(32):
            fields = focused()
            if any(
                n.get_role() == Atspi.Role.ENTRY and n.get_name() == "Label"
                for n in fields
            ):
                break
            key("Tab")
        else:
            raise AssertionError("keyboard traversal did not reach actual label editor")
        check(True, "native Tab reaches intended text field")
        label = next(n for n in focused() if n.get_name() == "Label")
        edit_mark = reader_mark()
        key("a", ("Control_L",))
        type_text("orca fixture label2026")
        key("Return")
        wait(
            lambda: state().get("saved") == "orca fixture label2026",
            "native Enter acknowledged exact label",
        )
        if a.orca and a.orca_output == "braille-monitor":
            reader_checkpoint("keyboard-edit", "orca fixture label2026", edit_mark)
            monitor_capture("edit", "orca fixture label2026")
        row = table.get_table_iface().get_accessible_at(0, 0).get_parent()
        check(
            row.get_component_iface().grab_focus(),
            "screen-reader row focus enters actual table",
        )
        wait(
            lambda: any(n.get_role() == Atspi.Role.TABLE_ROW for n in focused()),
            "actual row focus realization",
        )
        key("Return")
        wait(
            lambda: any(
                n.get_state_set().contains(Atspi.StateType.SELECTED) for n in focused()
            ),
            "native Enter selection publication",
        )
        check(
            any(
                n.get_state_set().contains(Atspi.StateType.SELECTED) for n in focused()
            ),
            "native Enter acknowledges actual table selection",
        )
        check(
            named("Label", Atspi.Role.ENTRY).get_component_iface().grab_focus(),
            "external focus returns from table to editor",
        )
        wait(
            lambda: any(n.get_name() == "Label" for n in focused()),
            "actual editor focus after table selection",
        )
        if a.orca and a.orca_output == "braille-monitor":
            table_mark = reader_mark()
            check(
                row.get_component_iface().grab_focus(),
                "reader row focus for native navigation",
            )
            wait(
                lambda: any(n.get_role() == Atspi.Role.TABLE_ROW for n in focused()),
                "actual row for reader navigation",
            )
            key("Down", ("Insert", "Control_L"))
            key("Right", ("Insert", "Control_L"))
            reader_checkpoint("table-amount", "0.01000000", table_mark)
            check(
                "OBJECT NAVIGATOR: Presenting"
                in (out / "orca-debug.log").read_text(errors="replace")[table_mark:],
                "actual Orca native navigator command",
            )
            check(
                named("Label", Atspi.Role.ENTRY).get_component_iface().grab_focus(),
                "reader returns to editor after table inspection",
            )
        label = named("Label", Atspi.Role.ENTRY)
        check(
            Atspi.Text.get_text(label, 0, -1) == "orca fixture label2026",
            "independent client observes native committed text",
        )
        check(
            Atspi.Text.get_caret_offset(label) >= 0,
            "actual native caret offset exported",
        )
        phase = "external-edit"
        before = state()["requests"]
        check(
            label.get_editable_text_iface().set_text_contents("Öffentlich##2026###"),
            "real D-Bus edit returns actual host acknowledgement",
        )
        wait(
            lambda: state().get("saved") == "Öffentlich##2026###",
            "external literal Unicode label committed",
        )
        check(
            state()["requests"] == before + 1,
            "one external edit produces one typed host effect",
        )
        label = named("Label", Atspi.Role.ENTRY)
        check(
            Atspi.EditableText.insert_text(label, 1, "éé", 2),
            "Unicode insertion accepts byte length at character position",
        )
        wait(
            lambda: state().get("saved") == "Öéffentlich##2026###",
            "exact Unicode byte-count insertion outcome",
        )
        check(
            named("Public watch-only wallet", Atspi.Role.PAGE_TAB)
            .get_component_iface()
            .grab_focus(),
            "external focus request from active editor to tab acknowledged",
        )
        wait(
            lambda: any(n.get_role() == Atspi.Role.PAGE_TAB for n in focused()),
            "actual external tab focus realization",
        )
        check(
            label.get_component_iface().grab_focus(),
            "external focus returns to actual editor",
        )
        wait(
            lambda: any(n.get_name() == "Label" for n in focused()),
            "actual external editor focus realization",
        )
        saved = state()["saved"]
        before = state()["requests"]
        check(
            not label.get_editable_text_iface().set_text_contents("invalid\nlabel"),
            "invalid external edit rejected by production validator",
        )
        wait(
            lambda: state().get("note") == "invalid\nlabel",
            "rejected draft view publication",
        )
        check(
            state()["saved"] == saved
            and state()["note"] == "invalid\nlabel"
            and state()["requests"] == before,
            "invalid draft retained without host effect",
        )
        check(
            label.get_editable_text_iface().set_text_contents("recover fixture"),
            "valid external retry recovers same editor",
        )
        phase = "dialog"
        dialog_mark = reader_mark()
        key("w", ("Control_L",))
        # Make a real dirty draft first when a clean close was legitimate.
        if not any(n.get_role() == Atspi.Role.DIALOG for n in tree(application)):
            key("o", ("Control_L",))
            wait(lambda: named("Label", Atspi.Role.ENTRY), "reopen fixture")
            label = named("Label", Atspi.Role.ENTRY)
            check(
                label.get_component_iface().grab_focus(),
                "external focus converges on native field",
            )
            key("a", ("Control_L",))
            type_text("dirty draft")
            key("w", ("Control_L",))
        wait(
            lambda: any(n.get_role() == Atspi.Role.DIALOG for n in tree(application)),
            "real dirty-close modal",
        )
        check(
            any(
                n.get_state_set().contains(Atspi.StateType.MODAL)
                for n in tree(application)
            ),
            "modal state is exported",
        )
        if a.orca and a.orca_output == "braille-monitor":
            reader_checkpoint("dialog-focus", "Cancel", dialog_mark)
        key("Escape")
        wait(
            lambda: (
                not any(n.get_role() == Atspi.Role.DIALOG for n in tree(application))
            ),
            "native Escape dismisses cancellable dialog",
        )
        wait(
            lambda: any(n.get_role() == Atspi.Role.PAGE_TAB for n in focused()),
            "actual dialog return focus realization",
        )
        check(
            any(n.get_role() == Atspi.Role.PAGE_TAB for n in focused()),
            "actual focus returns to stable document tab",
        )
        phase = "status"
        key("r", ("Control_L",))
        wait(lambda: state().get("refresh") == 1, "fixture status observation changes")
        check(
            named("Connection status").get_action_iface().do_action(0),
            "actual status action opens retained details",
        )
        wait(
            lambda: any(
                n.get_role() == Atspi.Role.STATUS_BAR
                and n.get_name().startswith("Connection")
                for n in tree(application)
            ),
            "current status detail publication",
        )
        pump(0.3)
        check(
            any(
                "Fixture refresh stale" in Atspi.Text.get_text(n, 0, -1)
                for n in tree(application)
                if n.is_text()
            ),
            "status change is readable through native text",
        )
        if a.orca and a.orca_output == "braille-monitor":
            status_mark = reader_mark()
            for _ in range(5):
                key("Right", ("Insert", "Control_L"))
            reader_checkpoint("status-detail", "Fixture refresh stale", status_mark)
        key("Escape")
        wait(
            lambda: named("Close drawer") is None,
            "native status drawer closes without replacing workspace",
        )
        phase = "hidden"
        hidden_mark = reader_mark()
        key("h", ("Control_L", "Shift_L"))
        wait(lambda: state().get("hidden"), "native explicit Hide Amounts choice")
        pump(0.3)
        current = []
        for n in tree(application):
            current += [n.get_name(), n.get_description()]
            if n.is_text():
                current.append(Atspi.Text.get_text(n, 0, -1))
        check(
            not any("0.01000000" in s for s in current),
            "hidden amount absent from current exported name/description/text",
        )
        check(
            not any(
                "0.01000000" in e.get("data", "")
                for e in events
                if e["phase"] == "hidden"
            ),
            "privacy transition events do not republish old amount",
        )
        if a.orca and a.orca_output == "braille-monitor":
            current_table = next(n for n in tree(application) if n.is_table())
            current_row = (
                current_table.get_table_iface().get_accessible_at(0, 0).get_parent()
            )
            check(
                current_row.get_component_iface().grab_focus(),
                "hidden row focus for reader",
            )
            wait(
                lambda: any(n.get_role() == Atspi.Role.TABLE_ROW for n in focused()),
                "actual hidden row focus",
            )
            key("Down", ("Insert", "Control_L"))
            key("Right", ("Insert", "Control_L"))
            reader_checkpoint("hidden-amount", "Hidden", hidden_mark, "0.01000000")
            monitor_capture("hidden", "Hidden")
        phase = "locked"
        old_label = named("Label", Atspi.Role.ENTRY)
        if a.orca and a.orca_output == "braille-monitor":
            check(
                old_label.get_component_iface().grab_focus(),
                "pre-lock actual editor focus",
            )
            wait(
                lambda: any(n.get_name() == "Label" for n in focused()),
                "actual pre-lock field focus",
            )
            pump(0.3)
        lock_mark = reader_mark()
        key("l", ("Control_L",))
        wait(lambda: state().get("locked"), "native lock host observation")
        wait(
            lambda: named("Label", Atspi.Role.ENTRY) is None,
            "locked body removed from native tree",
        )
        if a.orca and a.orca_output == "braille-monitor":
            reader_checkpoint("locked-focus", "page tab", lock_mark)
        try:
            Atspi.Text.get_text(old_label, 0, -1)
        except GLib.Error:
            check(True, "retained object path rejects access after lock")
        else:
            check(False, "retained object path rejects access after lock")
        if not a.orca:
            failure_bus = Gio.DBusConnection.new_for_address_sync(
                env["AT_SPI_BUS_ADDRESS"],
                Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
                | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
                None,
                None,
            )
            failure_bus.set_exit_on_close(False)
            daemon_pid = failure_bus.call_sync(
                "org.freedesktop.DBus",
                "/org/freedesktop/DBus",
                "org.freedesktop.DBus",
                "GetConnectionUnixProcessID",
                GLib.Variant("(s)", ("org.freedesktop.DBus",)),
                GLib.VariantType.new("(u)"),
                Gio.DBusCallFlags.NONE,
                1000,
                None,
            ).unpack()[0]
            check(
                daemon_pid != os.getpid() and os.getpgid(daemon_pid) == bus.pid,
                "failure injection targets only owned private accessibility bus",
            )
            before_frames = state()["frames"]
            os.kill(daemon_pid, signal.SIGTERM)
            wait(
                lambda: "disconnected" in state().get("error", ""),
                "actual production bus failure diagnostic",
            )
            key("h", ("Control_L", "Shift_L"))
            wait(
                lambda: not state().get("hidden"),
                "native keyboard remains usable after accessibility bus failure",
            )
            check(
                state()["frames"] > before_frames,
                "native render owner remains live after bus failure",
            )
            if not failure_bus.is_closed():
                failure_bus.close_sync(None)
        phase = "close"
        key("F4", ("Alt_L",))
        app.wait(timeout=5)
        check(
            app.returncode == 0
            and json.loads((out / "fixture/teardown.json").read_text())["cleaned"],
            "native bridge closes before ImGui and window teardown",
        )
        pump(0.3)
        if a.orca:
            check(reader.poll() is None, "actual Orca survived complete native journey")
            debug = (out / "orca-debug.log").read_text(errors="replace")
            if a.orca_output == "speech":
                check(
                    "SPEECH OUTPUT:" in debug and "Public fixture note" in debug,
                    "actual Orca generated spoken field content",
                )
                check(
                    "orca fixture label2026" in debug,
                    "actual Orca read committed keyboard edit",
                )
            else:
                lines = [line for line in debug.splitlines() if "BRAILLE LINE:" in line]
                (out / "braille-lines.txt").write_text("\n".join(lines) + "\n")
                check(
                    any("Public fixture note" in line for line in lines),
                    "actual Orca generated braille field content",
                )
                check(
                    any("orca fixture label2026" in line for line in lines),
                    "actual Orca braille read committed keyboard edit",
                )
                windows = [
                    desktop.get_child_at_index(i)
                    for i in range(desktop.get_child_count())
                ]
                monitor = next(
                    (
                        app.get_child_at_index(i)
                        for app in windows
                        for i in range(app.get_child_count())
                        if app.get_child_at_index(i).get_name() == "Braille Monitor"
                    ),
                    None,
                )
                check(
                    monitor is not None
                    and monitor.get_state_set().contains(Atspi.StateType.SHOWING),
                    "actual Orca braille monitor is visible",
                )
        check(
            any(e["type"].startswith("object:state-changed:focused") for e in events),
            "real focus-change event delivered",
        )
        check(
            any(e["type"].startswith("object:text-changed") for e in events),
            "real text-change event delivered",
        )

    error = None
    try:
        # A separately owned session bus, not the desktop/user session bus.
        # Readiness channel uses a separate descriptor, so logs retain real stderr.
        read_fd, write_fd = os.pipe()
        log = (out / "session-bus.log").open("w")
        streams.append(log)
        argv = [
            "dbus-daemon",
            "--session",
            "--nofork",
            "--address=unix:path=" + str(folder / "session.sock"),
            "--print-address=" + str(write_fd),
        ]
        bus = subprocess.Popen(
            argv,
            stdout=log,
            stderr=subprocess.STDOUT,
            env=env,
            start_new_session=True,
            pass_fds=(write_fd,),
        )
        os.close(write_fd)
        processes.append(("session-bus", bus))
        records.append(
            dict(
                kind="session-bus",
                pid=bus.pid,
                pgid=bus.pid,
                argv=argv,
                startUTC=datetime.now(timezone.utc).isoformat(),
            )
        )
        live_records()
        selector = selectors.DefaultSelector()
        selector.register(read_fd, selectors.EVENT_READ)
        if not selector.select(3):
            raise TimeoutError("private bus readiness")
        address = os.read(read_fd, 4096).decode().strip()
        os.close(read_fd)
        selector.close()
        if not address:
            raise RuntimeError(
                "private bus exited before readiness; see session-bus.log"
            )
        env["DBUS_SESSION_BUS_ADDRESS"] = address
        os.environ["DBUS_SESSION_BUS_ADDRESS"] = address
        session = Gio.DBusConnection.new_for_address_sync(
            address,
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
            | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
            None,
            None,
        )
        session.set_exit_on_close(False)
        reply = session.call_sync(
            "org.a11y.Bus",
            "/org/a11y/bus",
            "org.a11y.Bus",
            "GetAddress",
            None,
            GLib.VariantType.new("(s)"),
            Gio.DBusCallFlags.NONE,
            1500,
            None,
        )
        env["AT_SPI_BUS_ADDRESS"] = reply.unpack()[0]
        os.environ["AT_SPI_BUS_ADDRESS"] = reply.unpack()[0]
        listener = Atspi.EventListener.new(event, None)
        event_type = "object:state-changed:focused" if a.send_fixture else "object:"
        check(listener.register(event_type), "real libatspi event registration")
        check(listener.register("window:"), "real native-window event registration")
        if a.orca:
            debug_path = reader_debug_transport()
            if a.orca_output == "speech":
                (out / "speech-logs").mkdir()
                env["SPEECHD_ADDRESS"] = "unix_socket:" + str(folder / "speech.sock")
                env["SPEECHD_CMD"] = str(folder / "no-unowned-autospawn")
                launch(
                    "speech-dispatcher",
                    [
                        "speech-dispatcher",
                        "--run-single",
                        "--communication-method",
                        "unix_socket",
                        "--socket-path",
                        folder / "speech.sock",
                        "--pid-file",
                        folder / "speech.pid",
                        "--log-dir",
                        out / "speech-logs",
                        "--timeout",
                        "0",
                    ],
                )
                wait(
                    lambda: (folder / "speech.sock").exists(),
                    "owned foreground speech service readiness",
                    3,
                )
                speech_ready = True
                reader = launch(
                    "orca",
                    [
                        "orca",
                        "--user-prefs",
                        folder / "orca",
                        "--disable=braille",
                        "--debug",
                        "--debug-file",
                        debug_path,
                    ],
                )
            else:
                reader = launch(
                    "orca",
                    [
                        "orca",
                        "--user-prefs",
                        folder / "orca",
                        "--disable=speech,braille",
                        "--enable=braille-monitor",
                        "--debug",
                        "--debug-file",
                        debug_path,
                    ],
                )
            pump(2)
            check(
                reader.poll() is None,
                "actual Orca remains running before application launch",
            )
        if a.send_fixture:
            source, port, cookie = a.send_fixture
            native = out / "fixture"
            native.mkdir()
            fixture_env = env.copy()
            fixture_env["LC_ALL"] = env["ROOTS_GUI_READER_LOCALE"]
            app = launch(
                "application",
                [
                    a.binary.resolve(),
                    native,
                    a.fonts.resolve(),
                    source,
                    port,
                    cookie,
                    "reader",
                ],
                environment=fixture_env,
            )
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
                "registered production send application",
                20,
            )
            check(
                application.get_toolkit_name() == "Roots ImGui",
                "actual Application toolkit contract",
            )
            check(
                application.get_object_locale() == "de",
                "actual native application publishes German locale",
            )
            for step in range(14):
                phase = f"keyboard-{step}"
                path = native / (phase + ".json")
                instruction = wait(
                    lambda: json.loads(path.read_text()) if path.exists() else None,
                    phase,
                    12,
                )
                label, role, text = (
                    instruction[key] for key in ("name", "role", "text")
                )

                def enabled_target():
                    try:
                        return next(
                            (
                                node
                                for node in tree(application)
                                if node.get_name() == label
                                and node.get_role_name() == role
                                and node.get_state_set().contains(
                                    Atspi.StateType.ENABLED
                                )
                            ),
                            None,
                        )
                    except GLib.Error:
                        return None

                try:
                    target = wait(enabled_target, "enabled " + label, 10)
                except TimeoutError:
                    (out / (phase + "-tree.json")).write_text(
                        json.dumps(
                            [
                                {
                                    "name": node.get_name(),
                                    "role": node.get_role_name(),
                                    "states": [
                                        str(state)
                                        for state in node.get_state_set().get_states()
                                    ],
                                }
                                for node in tree(application)
                            ],
                            ensure_ascii=False,
                            indent=2,
                        )
                        + "\n"
                    )
                    raise
                mark = reader_mark()

                def focus_target():
                    nonlocal target
                    try:
                        if target is None:
                            target = enabled_target()
                        component = (
                            target.get_component_iface() if target is not None else None
                        )
                        if component is not None and component.grab_focus():
                            return True
                    except GLib.Error:
                        pass
                    target = None
                    return False

                def visible_focus():
                    try:
                        state_set = target.get_state_set()
                        return (
                            state_set is not None
                            and state_set.contains(Atspi.StateType.FOCUSED)
                            and state_set.contains(Atspi.StateType.SHOWING)
                        )
                    except GLib.Error:
                        return False

                # A new active document can replace the native object after
                # the queue acknowledges focus. Reacquire focus only. The
                # action key is sent once, after the actual reader checkpoint.
                for attempt in range(4):
                    wait(
                        focus_target,
                        "AT-SPI keyboard focus acknowledgement " + label,
                        8,
                    )
                    try:
                        wait(visible_focus, "actual visible keyboard focus " + label, 2)
                        break
                    except TimeoutError:
                        target = None
                        if attempt == 3:
                            raise
                reader_checkpoint(phase, label, mark)
                if step == 5:
                    monitor_capture("review-keyboard", label)
                if text:
                    mark = reader_mark()
                    for char in text:
                        key({",": "comma", ".": "period"}.get(char, char))
                    reader_checkpoint(phase + "-entered", text, mark)
                key("Return")
                if step == 2:
                    (native / "keyboard-2-enter").touch()
                check(True, f"actual enabled focus and keyboard action {step}")
            check(
                app.wait(timeout=8) == 0
                and json.loads((native / "native-result.json").read_text())["settled"],
                "native reviewed submission and monitor settled",
            )
            check(reader.poll() is None, "actual Orca survived complete native journey")
            check(
                any(
                    e["type"].startswith("object:state-changed:focused") for e in events
                ),
                "real focus-change event delivered",
            )
        else:
            baseline_journey()
    except Exception as exc:
        error = str(exc)
        traceback.print_exc()
    finally:
        if display and app is not None and app.poll() is None:
            try:
                key("F4", ("Alt_L",))
                app.wait(timeout=3)
            except Exception:
                pass
        for kind, proc in reversed(processes):
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait(timeout=3)
            for record in records:
                if record["pid"] == proc.pid:
                    record.update(
                        exit=proc.returncode,
                        settled=proc.poll() is not None,
                        endUTC=datetime.now(timezone.utc).isoformat(),
                    )

            def members():
                result = []
                for f in Path("/proc").glob("[0-9]*/stat"):
                    try:
                        fields = f.read_text().rsplit(")", 1)[1].split()
                        if int(fields[2]) == proc.pid and fields[0] != "Z":
                            result.append(int(f.parent.name))
                    except (OSError, ValueError, IndexError):
                        pass
                return result

            deadline = time.monotonic() + 1
            while members() and time.monotonic() < deadline:
                time.sleep(0.02)
            live = members()
            if live:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                deadline = time.monotonic() + 1
                while members() and time.monotonic() < deadline:
                    time.sleep(0.02)
            for record in records:
                if record["pid"] == proc.pid:
                    record["remainingLiveGroupMembers"] = members()
            if members():
                error = error or "owned process group did not settle: " + kind
        if reader_pty:
            os.close(reader_pty[1])
            reader_collector.join(timeout=3)
            if reader_collector.is_alive():
                error = error or "reader debug collector did not settle"
            os.close(reader_pty[0])
            reader_collector.join(timeout=1)
            if collector_errors:
                error = error or "reader debug collector: " + repr(collector_errors)
            (out / "reader-debug-transport.json").write_text(
                json.dumps(
                    dict(
                        kind="actual Orca CLI debug file on owned PTY; Python line buffering; transparent byte collector",
                        threadSettled=not reader_collector.is_alive(),
                        errors=collector_errors,
                        readerModified=False,
                    ),
                    indent=2,
                )
                + "\n"
            )
        for stream in streams:
            stream.close()
        if display:
            x.XCloseDisplay(display)
        scratch.cleanup()
        checks.append(
            dict(id="journey completed without harness error", passed=error is None)
        )
        (out / "reader-receipts.json").write_text(
            json.dumps(reader_receipts, indent=2, ensure_ascii=False) + "\n"
        )
        (out / "events.json").write_text(
            json.dumps(events, indent=2, ensure_ascii=False) + "\n"
        )
        (out / "keys.json").write_text(json.dumps(keys, indent=2) + "\n")
        (out / "processes.json").write_text(json.dumps(records, indent=2) + "\n")
        (out / "result.json").write_text(
            json.dumps(
                dict(
                    selected=len(expected),
                    unexecuted=sorted(expected - {x["id"] for x in checks}),
                    unexpected=sorted({x["id"] for x in checks} - expected),
                    executed=len(checks),
                    passed=sum(x["passed"] for x in checks),
                    failed=sum(not x["passed"] for x in checks),
                    skipped=0,
                    error=error,
                    checks=checks,
                    orcaRequested=a.orca,
                    orcaExecuted=reader is not None,
                    speechBackendReady=speech_ready,
                    orcaOutput=a.orca_output,
                    readerOutputVerified=a.orca and error is None,
                ),
                indent=2,
            )
            + "\n"
        )
    return int(
        error is not None
        or {x["id"] for x in checks} != expected
        or not all(x["passed"] for x in checks)
    )


if __name__ == "__main__":
    raise SystemExit(main())
