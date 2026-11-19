# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Bounded native theme evidence using the existing owned desktop lifecycle."""

import sys

sys.dont_write_bytecode = True
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "platform"))
from support.runtime import evidence_directory, binary_identity, write_json
from capture_manifest import encode_capture
from focus_pixels import check_file
from privacy_snapshots import check_folder
from shell_checks import check as check_shell
from status_focus import check as check_status_focus


def run_command(argv, log_path):
    start = datetime.now(timezone.utc).isoformat()
    with log_path.open("w") as log:
        child = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT)
        try:
            child.wait(timeout=150)
        finally:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=5)
    return dict(
        argv=argv,
        cwd=str(Path.cwd()),
        startUTC=start,
        endUTC=datetime.now(timezone.utc).isoformat(),
        exit=child.returncode,
    )


def processes_settled(folder):
    processes = json.loads((folder / "processes.json").read_text())
    return processes["child"]["settled"] and all(
        x["settled"] and not x.get("remainingLiveGroupMembers")
        for x in processes["processes"]
    )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--kind",
        choices=[
            "gallery",
            "appearance",
            "components",
            "shell",
            "shell_appearance",
            "documents",
            "commands",
            "semantics",
        ],
        required=True,
    )
    p.add_argument("--binary", type=Path, required=True)
    p.add_argument("--fonts", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument(
        "--semantic-variant",
        choices=[
            "focus",
            "pointer",
            "keyboard",
            "external",
            "failedAck",
            "hidden",
            "locked",
            "long",
        ],
    )
    a = p.parse_args()
    if a.semantic_variant and a.kind != "semantics":
        p.error("--semantic-variant requires semantics")
    out = Path(tempfile.mkdtemp(prefix=a.kind + "-", dir=evidence_directory(a.output)))
    selected = {
        "gallery": 48,
        "appearance": 3,
        "components": 132,
        "shell": 204,
        "shell_appearance": 6,
        "documents": 168,
        "commands": 276,
        "semantics": 96,
    }[a.kind]
    argv = [
        sys.executable,
        "-B",
        str(Path(__file__).resolve().parents[1] / "platform/owned_display.py"),
        "--output",
        str(out),
        "--",
        str(a.binary.resolve()),
        str(out / "captures"),
        str(a.fonts.resolve()),
    ]
    if a.semantic_variant:
        argv.append(a.semantic_variant)
        selected = 12
    record = run_command(argv, out / "runner.log")
    record["binary"] = binary_identity(a.binary)
    try:
        result = json.loads((out / "captures/result.json").read_text())
        settled = processes_settled(out)
        captures = [encode_capture(x) for x in sorted((out / "captures").glob("*.ppm"))]
        record.update(result=result, processesSettled=settled, captures=captures)
        record["fonts"] = [
            dict(name=x.name, sha256=hashlib.sha256(x.read_bytes()).hexdigest())
            for x in sorted(a.fonts.glob("*"))
            if x.is_file()
        ]
        source = Path(__file__).resolve().parents[2]
        record["source"] = [
            dict(
                path=str(x.relative_to(source)),
                sha256=hashlib.sha256(x.read_bytes()).hexdigest(),
            )
            for folder in ["ui", "platform", "assets", "app"]
            for x in sorted((source / folder).rglob("*"))
            if x.is_file()
        ]
        ok = (
            record["exit"] == 0
            and settled
            and result["selected"] == result["executed"] == result["passed"] == selected
            and result["failed"] == result["skipped"] == 0
        )
        if a.kind == "semantics":
            record["variants"] = {
                name: sum(
                    json.loads(
                        (out / "captures" / x["rawPath"])
                        .with_suffix(".json")
                        .read_text()
                    )["variant"]
                    == name
                    for x in captures
                )
                for name in [
                    "focus",
                    "pointer",
                    "keyboard",
                    "external",
                    "failedAck",
                    "hidden",
                    "locked",
                    "long",
                ]
            }
            focused = [
                check_file(
                    out / "captures" / x["rawPath"],
                    json.loads(
                        (out / "captures" / x["rawPath"])
                        .with_suffix(".json")
                        .read_text()
                    ),
                )
                for x in captures
                if x["rawPath"].endswith("-focus.ppm")
            ]
            record["focusGeometryChecks"] = focused
            expected_variants = {
                name: (
                    12 if not a.semantic_variant or a.semantic_variant == name else 0
                )
                for name in record["variants"]
            }
            ok = (
                ok
                and len(captures) == selected
                and record["variants"] == expected_variants
                and len(focused)
                == (
                    12 if not a.semantic_variant or a.semantic_variant == "focus" else 0
                )
                and all(x["passed"] for x in focused)
            )
            ok = ok and all(
                json.loads(x.read_text())["cleaned"]
                for x in (out / "captures").glob("*.teardown.json")
            )
            negative = out / "wrong-target"
            negative.mkdir()
            probe = run_command(
                [
                    sys.executable,
                    "-B",
                    str(
                        Path(__file__).resolve().parents[1]
                        / "platform/owned_display.py"
                    ),
                    "--output",
                    str(negative),
                    "--",
                    str(a.binary.resolve()),
                    str(negative / "captures"),
                    str(a.fonts.resolve()),
                    "wrong-target",
                ],
                negative / "runner.log",
            )
            probe["result"] = json.loads(
                (negative / "captures/result.json").read_text()
            )
            probe["processesSettled"] = processes_settled(negative)
            probe["expectedRejectionObserved"] = (
                probe["exit"] == 1
                and probe["result"]["assertionFailures"] == 1
                and probe["result"]["failed"] == 1
                and probe["result"]["executed"] == 1
                and probe["processesSettled"]
            )
            record["wrongTargetProbe"] = probe
            ok = ok and probe["expectedRejectionObserved"]
        if a.kind == "components":
            variants = {}
            clocks = []
            for capture in captures:
                variant = capture["rawPath"].rsplit("-", 1)[1].removesuffix(".ppm")
                variants[variant] = variants.get(variant, 0) + 1
                metadata = json.loads(
                    (out / "captures" / capture["rawPath"])
                    .with_suffix(".json")
                    .read_text()
                )
                clocks.append(metadata.get("fixedDeltaSeconds"))
            record["variants"] = variants
            record["presentationClockSeconds"] = clocks
            ok = (
                ok
                and len(captures) == 132
                and variants
                == {
                    name: 12
                    for name in [
                        "public",
                        "invalid",
                        "disabled",
                        "dialog",
                        "empty",
                        "table",
                        "hidden",
                        "reduced",
                        "hiddenReduced",
                        "reviewHidden",
                        "reviewShown",
                    ]
                }
            )
            ok = ok and all(
                value is not None and abs(value - 1 / 60) < 0.000001 for value in clocks
            )
            ok = ok and all(
                json.loads(x.read_text())["cleaned"]
                for x in (out / "captures").glob("*.teardown.json")
            )
            privacy = check_folder(out / "captures")
            record["privacyChecks"] = privacy
            ok = (
                ok
                and privacy["executed"] == privacy["passed"] == 132
                and privacy["hidden"] == 36
                and not privacy["failed"]
            )
        if a.kind == "commands":
            variants = {}
            status_focus = []
            for capture in captures:
                raw = out / "captures" / capture["rawPath"]
                m = json.loads(raw.with_suffix(".json").read_text())
                variants[m["variant"]] = variants.get(m["variant"], 0) + 1
                if m["variant"] == "activityFocus":
                    m["id"] = raw.stem
                    status_focus.append(check_status_focus(raw, m))
                ok = ok and m["accountWidth"] == 0 and m["workspaceDocuments"] == 3
            expected = [
                "menuFile",
                "menuView",
                "menuTools",
                "menuHelp",
                "menuSelected",
                "statusUnavailable",
                "statusLoading",
                "statusError",
                "statusStale",
                "statusOffline",
                "statusLocked",
                "statusLong",
                "activityLoading",
                "activityEmpty",
                "activityError",
                "activityStale",
                "activityHidden",
                "activityHover",
                "activityPressed",
                "activityFocus",
                "gridEscape",
                "unavailableRoute",
                "failedAck",
            ]
            record["variants"] = variants
            record["statusFocusGeometryChecks"] = status_focus
            ok = (
                ok
                and len(status_focus) == 12
                and all(x["passed"] for x in status_focus)
            )
            ok = (
                ok
                and len(captures) == 276
                and variants == {name: 12 for name in expected}
                and all(
                    json.loads(x.read_text())["cleaned"]
                    for x in (out / "captures").glob("*.teardown.json")
                )
            )
            negative = out / "wrong-target"
            negative.mkdir()
            argv = [
                sys.executable,
                "-B",
                str(Path(__file__).resolve().parents[1] / "platform/owned_display.py"),
                "--output",
                str(negative),
                "--",
                str(a.binary.resolve()),
                str(negative / "captures"),
                str(a.fonts.resolve()),
                "wrong-target",
            ]
            probe = run_command(argv, negative / "runner.log")
            probe["result"] = json.loads(
                (negative / "captures/result.json").read_text()
            )
            probe["processesSettled"] = processes_settled(negative)
            probe["expectedRejectionObserved"] = (
                probe["exit"] == 1
                and probe["result"]["assertionFailures"] == 1
                and probe["result"]["selected"]
                == probe["result"]["executed"]
                == probe["result"]["failed"]
                == 1
                and probe["processesSettled"]
            )
            record["wrongTargetProbe"] = probe
            ok = ok and probe["expectedRejectionObserved"]
        if a.kind == "documents":
            variants = {}
            for capture in captures:
                raw = out / "captures" / capture["rawPath"]
                m = json.loads(raw.with_suffix(".json").read_text())
                variants[m["variant"]] = variants.get(m["variant"], 0) + 1
                ok = ok and m["accountsWidth"] == 0 and m["nativeWindowOwners"] == 1
            expected = [
                "empty",
                "tabs",
                "long",
                "hover",
                "pressed",
                "focus",
                "close",
                "rename",
                "delete",
                "import",
                "metadataError",
                "drop",
                "locked",
                "hidden",
            ]
            record["variants"] = variants
            ok = (
                ok
                and len(captures) == 168
                and variants == {name: 12 for name in expected}
                and all(
                    json.loads(x.read_text())["cleaned"]
                    for x in (out / "captures").glob("*.teardown.json")
                )
            )
            negative = out / "wrong-target"
            negative.mkdir()
            argv = [
                sys.executable,
                "-B",
                str(Path(__file__).resolve().parents[1] / "platform/owned_display.py"),
                "--output",
                str(negative),
                "--",
                str(a.binary.resolve()),
                str(negative / "captures"),
                str(a.fonts.resolve()),
                "wrong-target",
            ]
            probe = run_command(argv, negative / "runner.log")
            probe["result"] = json.loads(
                (negative / "captures/result.json").read_text()
            )
            probe["processesSettled"] = processes_settled(negative)
            probe["expectedRejectionObserved"] = (
                probe["exit"] == 1
                and probe["result"]["assertionFailures"] == 1
                and probe["result"]["selected"]
                == probe["result"]["executed"]
                == probe["result"]["failed"]
                == 1
                and probe["processesSettled"]
            )
            record["wrongTargetProbe"] = probe
            ok = ok and probe["expectedRejectionObserved"]
        if a.kind == "shell_appearance":
            ok = (
                ok
                and len(captures) == 13
                and result.get("captures") == 13
                and all(
                    json.loads(p.read_text())["cleaned"]
                    for p in (out / "captures").glob("*.teardown.json")
                )
            )
        if a.kind == "shell":
            cases = []
            variants = {}
            for capture in captures:
                raw = out / "captures" / capture["rawPath"]
                metadata = json.loads(raw.with_suffix(".json").read_text())
                case = check_shell(
                    metadata,
                    raw.with_suffix(".presentation.txt").read_text(),
                    raw.with_suffix(".semantics.txt").read_text(),
                )
                case["id"] = metadata["id"]
                cases.append(case)
                variant = capture["rawPath"].rsplit("-", 1)[1].removesuffix(".ppm")
                variants[variant] = variants.get(variant, 0) + 1
            record["shellChecks"] = cases
            record["variants"] = variants
            tab_focus = []
            for capture in captures:
                if capture["rawPath"].endswith("-transaction.ppm"):
                    raw = out / "captures" / capture["rawPath"]
                    m = json.loads(raw.with_suffix(".json").read_text())
                    m.update(primary=m["tabFocusPrimary"], focused=m["tabFocused"])
                    tab_focus.append(check_file(raw, m))
            record["tabFocusGeometryChecks"] = tab_focus
            ok = ok and len(tab_focus) == 12 and all(x["passed"] for x in tab_focus)
            expected = ["section" + str(i) for i in range(6)] + [
                "loading",
                "empty",
                "error",
                "stale",
                "locked",
                "hidden",
                "long",
                "incomplete",
                "incompleteSend",
                "transaction",
                "workspaceEmpty",
            ]
            ok = (
                ok
                and len(cases) == 204
                and variants == {name: 12 for name in expected}
                and all(x["passed"] for x in cases)
            )
            negative = out / "wrong-target"
            negative.mkdir()
            argv = [
                sys.executable,
                "-B",
                str(Path(__file__).resolve().parents[1] / "platform/owned_display.py"),
                "--output",
                str(negative),
                "--",
                str(a.binary.resolve()),
                str(negative / "captures"),
                str(a.fonts.resolve()),
                "wrong-target",
            ]
            probe = run_command(argv, negative / "runner.log")
            probe["result"] = json.loads(
                (negative / "captures/result.json").read_text()
            )
            probe["processesSettled"] = processes_settled(negative)
            snapshot = json.loads(
                next(
                    p
                    for p in (negative / "captures").glob("shell-*.json")
                    if not p.name.endswith(".teardown.json")
                ).read_text()
            )
            probe["snapshot"] = snapshot
            rejected = (
                probe["exit"] == 1
                and probe["result"]["assertionFailures"] == 1
                and probe["result"]["selected"]
                == probe["result"]["executed"]
                == probe["result"]["failed"]
                == 1
                and not snapshot["targetAccepted"]
                and snapshot["section"] != snapshot["expectedSection"]
                and probe["processesSettled"]
            )
            probe["expectedRejectionObserved"] = rejected
            record["wrongTargetProbe"] = probe
            ok = ok and rejected
        if a.kind == "gallery":
            ok = ok and len(captures) == 48
            ok = ok and all(
                json.loads(x.read_text())["cleaned"]
                for x in (out / "captures").glob("*.teardown.json")
            )
            # Read real pixels in the unlabelled primary fill, distinct from
            # checking a semantic token pair or trusting a successful draw call.
            samples = []
            for capture in captures:
                raw = out / "captures" / capture["rawPath"]
                meta = json.loads(raw.with_suffix(".json").read_text())
                _, dimensions, _, pixels = raw.read_bytes().split(b"\n", 3)
                width, height = map(int, dimensions.split())
                x0, y0, x1, y1 = meta["primary"]
                x = int(x0 + 12 * meta["effectiveScale"])
                y = int((y0 + y1) / 2)
                color = tuple(pixels[(y * width + x) * 3 : (y * width + x) * 3 + 3])
                dark = "-dark-" in meta["id"]
                state = meta["id"].rsplit("-", 1)[1]
                expected = (
                    (0xE0, 0xDA, 0xDA)
                    if dark and state == "pressed"
                    else (0xF2, 0xEE, 0xEE)
                    if dark and state == "hover"
                    else (255, 255, 255)
                    if dark
                    else (6, 31, 63)
                    if state == "pressed"
                    else (16, 59, 106)
                    if state == "hover"
                    else (8, 41, 82)
                )
                # Focus uses a real Tab action; the pointer may remain over the
                # primary, so use the observed hover flag for its fill.
                if state == "focus" and meta["hovered"]:
                    expected = (242, 238, 238) if dark else (16, 59, 106)
                samples.append(
                    dict(
                        id=meta["id"],
                        x=x,
                        y=y,
                        actual=color,
                        expected=expected,
                        passed=color == expected,
                    )
                )
            record["primaryPixelChecks"] = samples
            ok = ok and all(x["passed"] for x in samples)
            focus = [
                check_file(
                    out / "captures" / c["rawPath"],
                    json.loads(
                        (out / "captures" / c["rawPath"])
                        .with_suffix(".json")
                        .read_text()
                    ),
                )
                for c in captures
                if c["rawPath"].endswith("-focus.ppm")
            ]
            record["focusGeometryChecks"] = focus
            ok = ok and len(focus) == 12 and all(x["passed"] for x in focus)
            negative = out / "wrong-target"
            negative.mkdir()
            probe_argv = [
                sys.executable,
                "-B",
                str(Path(__file__).resolve().parents[1] / "platform/owned_display.py"),
                "--output",
                str(negative),
                "--",
                str(a.binary.resolve()),
                str(negative / "captures"),
                str(a.fonts.resolve()),
                "wrong-target",
            ]
            probe = run_command(probe_argv, negative / "runner.log")
            probe["expectedExit"] = 1
            probe["result"] = json.loads(
                (negative / "captures/result.json").read_text()
            )
            probe["snapshot"] = json.loads(
                (
                    negative / "captures/theme-gallery-960-dark-200-focus.json"
                ).read_text()
            )
            probe["processesSettled"] = processes_settled(negative)
            probe["captures"] = [
                encode_capture(x) for x in (negative / "captures").glob("*.ppm")
            ]
            rejected = (
                probe["exit"] == 1
                and probe["result"]["selected"]
                == probe["result"]["executed"]
                == probe["result"]["failed"]
                == 1
            )
            rejected = (
                rejected
                and probe["result"]["assertionFailures"] == 1
                and not probe["snapshot"]["focused"]
                and probe["snapshot"]["navVisible"]
            )
            rejected = (
                rejected
                and not probe["snapshot"]["targetFocusAccepted"]
                and probe["processesSettled"]
            )
            probe["expectedRejectionObserved"] = rejected
            record["wrongTargetProbe"] = probe
            ok = ok and rejected
        record["passed"] = bool(ok)
    except Exception as error:
        record.update(passed=False, error=str(error))
    write_json(out / "manifest.json", record)
    print(
        json.dumps(
            dict(
                kind=a.kind,
                output=str(out),
                exit=record["exit"],
                passed=record["passed"],
                result=record.get("result"),
            )
        )
    )
    return 0 if record["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
