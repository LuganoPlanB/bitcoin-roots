# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Independent real D-Bus tests of bounded synthetic packet transport, not Orca."""

import sys

sys.dont_write_bytecode = True
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
import traceback
from gi.repository import Gio, GLib


EXPECTED_CHECKS = (
    "startup-cancel cancels and settles callbacks",
    "missing-bus cancels and settles callbacks",
    "application window plus all six packet nodes exported",
    "Unicode character count is not byte length",
    "literal Unicode values survive real wire serialization",
    "character after offset is adjacent, not skipped",
    "only operational progress uses numeric Value",
    "disabled target cannot enqueue focus",
    "wrong static target cannot steal focus",
    "same-scope sort preserves native identities",
    "sorted child order reflects current packet",
    "cache indices agree with sorted actual children",
    "scope transition gives native objects new incarnations",
    "retired native reference rejects action",
    "same-scope privacy events never re-emit deleted amount",
    "invalid node-overflow removes current native content",
    "actual node-overflow overflow flag retained",
    "node-overflow recovery publishes fresh owned content",
    "invalid text-overflow removes current native content",
    "actual text-overflow overflow flag retained",
    "text-overflow recovery publishes fresh owned content",
    "invalid utf8 removes current native content",
    "actual utf8 transport error exposed",
    "utf8 recovery publishes fresh owned content",
    "invalid coordinates removes current native content",
    "actual coordinates transport error exposed",
    "coordinates recovery publishes fresh owned content",
    "invalid graph removes current native content",
    "actual graph transport error exposed",
    "graph recovery publishes fresh owned content",
    "thirty-third pending action rejected",
    "owner event pump remains live while actions await validation",
    "close cannot acknowledge unexecuted effects",
    "close settles registrations/actions without host effects",
    "transport journey completed without error",
)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    expected = set(EXPECTED_CHECKS) | {
        "native status role retains visible detail in its name",
        "status update keeps identity and changes readable content",
        "status name change event carries current content",
        "operational progress changes current numeric value",
        "operational progress event has a real double payload",
    }
    out = a.output.resolve()
    out.mkdir(parents=True)
    checks = []
    events = []
    pending = []
    seq = 0
    child = None
    stream = None
    error = None

    def check(ok, label):
        checks.append(dict(id=label, passed=bool(ok)))
        if not ok:
            raise AssertionError(label)

    def pump():
        for _ in range(32):
            if not GLib.MainContext.default().iteration(False):
                break
        time.sleep(0.005)

    def wait(predicate, label, timeout=4):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                return value
            pump()
        raise TimeoutError(label)

    def state():
        try:
            return json.loads((out / "server/state.json").read_text())
        except FileNotFoundError:
            return {}

    def command(name):
        nonlocal seq
        seq += 1
        (out / "server/command.pending").write_text(str(seq) + " " + name + "\n")
        (out / "server/command.pending").replace(out / "server/command")
        wait(lambda: state().get("command") == seq, "owner processed " + name)

    def launch(name, env=None):
        folder = out / name
        folder.mkdir()
        log = (folder / "log").open("w")
        proc = subprocess.Popen(
            [str(a.binary.resolve()), str(folder), name],
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        try:
            proc.wait(timeout=4)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=2)
            log.close()
        check(
            proc.returncode == 0
            and json.loads((folder / "result.json").read_text())["closed"],
            name + " cancels and settles callbacks",
        )

    try:
        launch("startup-cancel")
        launch(
            "missing-bus",
            os.environ
            | {
                "DBUS_SESSION_BUS_ADDRESS": "unix:path="
                + str(
                    Path(os.environ["TMPDIR"])
                    / ("missing-atspi-" + str(os.getpid()) + ".sock")
                )
            },
        )
        (out / "server").mkdir()
        stream = (out / "server/log").open("w")
        child = subprocess.Popen(
            [str(a.binary.resolve()), str(out / "server"), "serve"],
            stdout=stream,
            stderr=subprocess.STDOUT,
        )
        wait(lambda: state().get("registered"), "real accessibility registry Embed")
        session = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        session.set_exit_on_close(False)
        address = session.call_sync(
            "org.a11y.Bus",
            "/org/a11y/bus",
            "org.a11y.Bus",
            "GetAddress",
            None,
            GLib.VariantType.new("(s)"),
            Gio.DBusCallFlags.NONE,
            1000,
            None,
        ).unpack()[0]
        bus = Gio.DBusConnection.new_for_address_sync(
            address,
            Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
            | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
            None,
            None,
        )
        bus.set_exit_on_close(False)
        name = state()["bus"]
        root = "/org/a11y/atspi/accessible/root"

        def call(path, interface, method, args=None):
            return bus.call_sync(
                name,
                path,
                "org.a11y.atspi." + interface,
                method,
                args,
                None,
                Gio.DBusCallFlags.NONE,
                1200,
                None,
            ).unpack()

        def prop(path, interface, key):
            return bus.call_sync(
                name,
                path,
                "org.freedesktop.DBus.Properties",
                "Get",
                GLib.Variant("(ss)", ("org.a11y.atspi." + interface, key)),
                None,
                Gio.DBusCallFlags.NONE,
                1200,
                None,
            ).unpack()[0]

        def nodes():
            result = {}
            todo = [root]
            while todo:
                path = todo.pop(0)
                result[prop(path, "Accessible", "AccessibleId")] = path
                todo += [x[1] for x in call(path, "Accessible", "GetChildren")[0]]
                if len(result) > 260:
                    raise RuntimeError("unbounded exported graph")
            return result

        current = nodes()
        check(
            len(current) == 8, "application window plus all six packet nodes exported"
        )
        check(
            prop(current["102"], "Text", "CharacterCount")
            == len("Öffentlich##2026###"),
            "Unicode character count is not byte length",
        )
        check(
            call(current["102"], "Text", "GetText", GLib.Variant("(ii)", (0, -1)))[0]
            == "Öffentlich##2026###",
            "literal Unicode values survive real wire serialization",
        )
        check(
            call(
                current["102"],
                "Text",
                "GetTextAfterOffset",
                GLib.Variant("(iu)", (1, 0)),
            )
            == ("f", 2, 3),
            "character after offset is adjacent, not skipped",
        )
        check(
            prop(current["106"], "Value", "CurrentValue") == 425.0
            and prop(current["106"], "Value", "MaximumValue") == 1000.0,
            "only operational progress uses numeric Value",
        )
        check(
            not call(current["104"], "Component", "GrabFocus")[0],
            "disabled target cannot enqueue focus",
        )
        check(
            not call(current["105"], "Component", "GrabFocus")[0],
            "wrong static target cannot steal focus",
        )

        def event(_c, _s, path, interface, signal, args, _u):
            events.append(
                dict(
                    path=path,
                    interface=interface,
                    signal=signal,
                    data=repr(args.unpack()),
                    payloadType=type(args.unpack()[3]).__name__
                    if len(args.unpack()) > 3
                    else None,
                )
            )

        subscription = bus.signal_subscribe(
            name, None, None, None, None, Gio.DBusSignalFlags.NONE, event, None
        )
        command("sort")
        sorted_nodes = nodes()
        check(sorted_nodes == current, "same-scope sort preserves native identities")
        children = call(current["101"], "Accessible", "GetChildren")[0]
        check(
            [x[1] for x in children] == [current[str(i)] for i in range(106, 101, -1)],
            "sorted child order reflects current packet",
        )
        cached = call("/org/a11y/atspi/cache", "Cache", "GetItems")[0]
        check(
            all(
                next(x for x in cached if x[0][1] == current[str(i)])[3] == 106 - i
                for i in range(106, 101, -1)
            ),
            "cache indices agree with sorted actual children",
        )
        wait(
            lambda: any(e["signal"] == "AddAccessible" for e in events),
            "standard cache update signals after sorting",
        )
        command("normal")
        current = nodes()
        command("status")
        status_path = nodes()["105"]
        check(
            "Stale / last success 12:00 UTC" in prop(status_path, "Accessible", "Name"),
            "native status role retains visible detail in its name",
        )
        events.clear()
        command("status-error")
        check(
            nodes()["105"] == status_path
            and "Error / fixture offline" in prop(status_path, "Accessible", "Name"),
            "status update keeps identity and changes readable content",
        )
        wait(
            lambda: any(
                e["signal"] == "PropertyChange"
                and "accessible-name" in e["data"]
                and "Error / fixture offline" in e["data"]
                for e in events
            ),
            "actual status name signal",
        )
        check(True, "status name change event carries current content")
        command("normal")
        current = nodes()
        events.clear()
        command("progress")
        check(
            prop(nodes()["106"], "Value", "CurrentValue") == 500.0,
            "operational progress changes current numeric value",
        )
        wait(
            lambda: any(
                e["signal"] == "PropertyChange"
                and "accessible-value" in e["data"]
                and "500.0" in e["data"]
                and e["payloadType"] == "float"
                for e in events
            ),
            "real numeric progress change event",
        )
        check(True, "operational progress event has a real double payload")
        command("normal")
        current = nodes()
        stale = current["103"]
        command("scope")
        fresh = nodes()
        check(
            fresh["103"] != stale,
            "scope transition gives native objects new incarnations",
        )
        try:
            call(stale, "Action", "DoAction", GLib.Variant("(i)", (0,)))
        except GLib.Error as e:
            check(
                "Unknown" in str(e) or "Defunct" in str(e),
                "retired native reference rejects action",
            )
        else:
            check(False, "retired native reference rejects action")
        command("normal")
        wait(lambda: len(nodes()) == 8, "normal restored")
        events.clear()
        command("hidden")
        wait(
            lambda: (
                call(nodes()["105"], "Text", "GetText", GLib.Variant("(ii)", (0, -1)))[
                    0
                ]
                == "Hidden"
            ),
            "privacy text projection",
        )
        pump()
        check(
            not any("0.01000000" in e["data"] for e in events),
            "same-scope privacy events never re-emit deleted amount",
        )
        for mode, overflow in [
            ("node-overflow", True),
            ("text-overflow", True),
            ("utf8", False),
            ("coordinates", False),
            ("graph", False),
        ]:
            command(mode)
            check(
                len(nodes()) == 2, "invalid " + mode + " removes current native content"
            )
            if overflow:
                check(state()["overflow"], "actual " + mode + " overflow flag retained")
            else:
                check(state()["error"], "actual " + mode + " transport error exposed")
            command("normal")
            check(len(nodes()) == 8, mode + " recovery publishes fresh owned content")
        ticks = state()["ticks"]
        current = nodes()
        for _ in range(33):

            def done(connection, result, _data):
                try:
                    pending.append(
                        dict(applied=bool(connection.call_finish(result).unpack()[0]))
                    )
                except GLib.Error as e:
                    pending.append(dict(applied=False, error=str(e)))

            bus.call(
                name,
                current["103"],
                "org.a11y.atspi.Action",
                "DoAction",
                GLib.Variant("(i)", (0,)),
                GLib.VariantType.new("(b)"),
                Gio.DBusCallFlags.NONE,
                4000,
                None,
                done,
                None,
            )
        wait(lambda: state().get("pending") == 32, "bounded pending native methods")
        wait(lambda: len(pending) == 1, "overflow method returns explicit rejection")
        check(not pending[0]["applied"], "thirty-third pending action rejected")
        check(
            state()["ticks"] > ticks,
            "owner event pump remains live while actions await validation",
        )
        # This publisher deliberately never consumes actions: closing must cancel
        # them, not invent acknowledgements or apply a synthetic host command.
        seq += 1
        (out / "server/command.pending").write_text(str(seq) + " close\n")
        (out / "server/command.pending").replace(out / "server/command")
        child.wait(timeout=4)
        wait(
            lambda: len(pending) == 33, "every pending client receives terminal result"
        )
        check(
            not any(x["applied"] for x in pending),
            "close cannot acknowledge unexecuted effects",
        )
        result = json.loads((out / "server/result.json").read_text())
        check(
            child.returncode == 0 and result["closed"] and not result["hostEffects"],
            "close settles registrations/actions without host effects",
        )
        bus.signal_unsubscribe(subscription)
        bus.close_sync(None)
        session.close_sync(None)
    except Exception as e:
        error = str(e)
        traceback.print_exc()
    finally:
        if child is not None and child.poll() is None:
            child.terminate()
            child.wait(timeout=3)
        if stream:
            stream.close()
        checks.append(
            dict(id="transport journey completed without error", passed=error is None)
        )
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
                    qualification="Real bus with synthetic immutable packets; not native rendering/Orca or application-command equivalence.",
                ),
                indent=2,
            )
            + "\n"
        )
        (out / "events.json").write_text(json.dumps(events, indent=2) + "\n")
        (out / "pending.json").write_text(json.dumps(pending, indent=2) + "\n")
    return int(
        error is not None
        or {x["id"] for x in checks} != expected
        or not all(x["passed"] for x in checks)
    )


if __name__ == "__main__":
    raise SystemExit(main())
