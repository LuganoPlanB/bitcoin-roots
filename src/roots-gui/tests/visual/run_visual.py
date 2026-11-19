# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Owned native production captures and explicit external-baseline comparison."""

import sys

sys.dont_write_bytecode = True
import argparse
from datetime import datetime, timezone
import ctypes.util
import hashlib
import json
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "platform"))
from support.runtime import evidence_directory, binary_identity, write_json
from capture_manifest import encode_capture
from pixels import Unavailable, compare, compare_files, manifest

ROOT = Path(__file__).resolve().parents[2]


def main():
    def interrupted(*_):
        raise RuntimeError("visual runner interrupted")

    signal.signal(signal.SIGTERM, interrupted)
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--native-capture", type=Path, required=True)
    p.add_argument("--native-lifecycle", type=Path, required=True)
    p.add_argument("--fonts", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--baseline", type=Path)
    p.add_argument("--capture-candidate", action="store_true")
    p.add_argument("--force-unavailable", action="store_true")
    a = p.parse_args()
    out = Path(tempfile.mkdtemp(prefix="visual-", dir=evidence_directory(a.output)))
    record = dict(
        selected=48,
        executed=0,
        passed=0,
        failed=0,
        skipped=0,
        unavailable=0,
        commands=[],
        captures=[],
        status="running",
        countsKind="positive native scenarios; comparisons and rejection probes reported separately",
    )
    checks = []

    def check(ok, id):
        checks.append(dict(id=id, passed=bool(ok)))
        if not ok:
            raise AssertionError(id)

    def run(argv, log, expected=0):
        start = datetime.now(timezone.utc).isoformat()
        with log.open("w") as f:
            child = subprocess.Popen(
                [str(x) for x in argv], stdout=f, stderr=subprocess.STDOUT
            )
            try:
                child.wait(timeout=180)
            finally:
                if child.poll() is None:
                    child.terminate()
                    try:
                        child.wait(timeout=30)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait(timeout=5)
            r = child
        record["commands"].append(
            dict(
                argv=[str(x) for x in argv],
                exit=r.returncode,
                expectedExit=expected,
                startUTC=start,
                endUTC=datetime.now(timezone.utc).isoformat(),
                log=str(log.relative_to(out)),
            )
        )
        if (
            expected == 0
            and r.returncode != 0
            and not list((out / log.stem / "captures").glob("*.ppm"))
        ):
            diagnostic = (
                (out / log.stem / "command.log").read_text()
                if (out / log.stem / "command.log").exists()
                else ""
            )
            if any(
                text in diagnostic
                for text in [
                    "GLFW initialization failed",
                    "native window/OpenGL context creation failed",
                    "OpenGL context could not be made current",
                ]
            ):
                raise Unavailable(
                    "native window/context capability unavailable; retained diagnostics"
                )
        check(r.returncode == expected, "child expected exit " + log.stem)

    try:
        if a.force_unavailable:
            raise Unavailable("injected unavailable native capability")
        missing = [
            x
            for x in ["Xvfb", "marco", "xrdb", "dbus-run-session"]
            if not shutil.which(x)
        ]
        if (
            missing
            or not ctypes.util.find_library("GL")
            or not ctypes.util.find_library("Xtst")
        ):
            raise Unavailable("required native capabilities unavailable")
        if not a.capture_candidate:
            if not a.baseline:
                raise Unavailable(
                    "explicit curated baseline required; candidate collection is separate"
                )
            manifest(a.baseline, curated=True)
        record["binaries"] = [
            binary_identity(x) for x in [a.native_capture, a.native_lifecycle]
        ]
        fonts = [
            dict(path=x.name, sha256=hashlib.sha256(x.read_bytes()).hexdigest())
            for x in sorted(a.fonts.glob("*"))
            if x.is_file()
        ]
        if not any(x["path"] == "Inter-Regular.ttf" for x in fonts):
            raise Unavailable("required licensed font assets unavailable")
        matrix = []
        native = []
        for profile, dpi, binary, mode in [
            ("bootstrap-96", 96, a.native_lifecycle, None),
            ("bootstrap-144", 144, a.native_lifecycle, None),
            ("panel-96", 96, a.native_capture, None),
            ("failure", 96, a.native_capture, "failure"),
            ("missing-framebuffer", 96, a.native_capture, "missing-framebuffer"),
        ]:
            folder = out / profile
            captures = folder / "captures"
            expected = (
                1 if mode == "failure" else 2 if mode == "missing-framebuffer" else 0
            )
            argv = [
                sys.executable,
                "-B",
                ROOT / "tests/platform/owned_display.py",
                "--output",
                folder,
                "--dpi",
                str(dpi),
                "--",
                binary,
                captures,
                a.fonts,
            ] + ([mode] if mode else [])
            run(argv, out / (profile + ".log"), expected)
            processes = json.loads((folder / "processes.json").read_text())
            check(
                all(x["settled"] for x in processes["processes"])
                and not processes["processes"][1]["remainingLiveGroupMembers"],
                "owned display group settled " + profile,
            )
            check(processes["child"]["settled"], "native child settled " + profile)
            if profile.startswith("bootstrap"):
                stdout = (folder / "command.log").read_text()
                check(
                    "selected_cases=18 started_cases=18 completed_cases=18 unstarted_cases=0"
                    in stdout
                    and "executed=241 passed=241 failed=0 skipped=0" in stdout,
                    "bootstrap production lifecycle exact counts " + profile,
                )
                record["executed"] += 18
                record["passed"] += 18
            else:
                result = json.loads((captures / "result.json").read_text())
                native.append(dict(profile=profile, result=result))
                if mode == "failure":
                    check(
                        result["executed"] == result["failed"] == 1
                        and result["assertionFailures"] == 1,
                        "intentional rendered assertion reached",
                    )
                    meta = json.loads((captures / "failure.json").read_text())
                    check(
                        meta["saveRequests"] == 1 and meta["saveRejected"],
                        "actual Save rejection visible before failure screenshot",
                    )
                elif mode == "missing-framebuffer":
                    check(
                        result["executed"] == 0 and result["unavailable"] == 1,
                        "missing framebuffer reported unavailable, never pass",
                    )
                else:
                    check(
                        result["executed"] == result["passed"] == 12
                        and result["failed"] == result["skipped"] == 0,
                        "panel exact native scenario counts",
                    )
                    record["executed"] += 12
                    record["passed"] += 12
            for raw in sorted(captures.glob("*.ppm")):
                image = encode_capture(raw)
                image.update(
                    path=str((captures / image["path"]).relative_to(out)),
                    rawPath=str(raw.relative_to(out)),
                )
                record["captures"].append(image)
                if raw.stem.startswith(
                    ("bootstrap-", "test-panel-")
                ) and not raw.stem.endswith("-frame"):
                    width, theme, font = raw.stem.rsplit("-", 3)[1:]
                    logical_width = int(width.split("x")[0])
                    meta = json.loads(raw.with_suffix(".json").read_text())
                    matrix.append(
                        dict(
                            id=profile + "/" + raw.stem,
                            rawPath=image["rawPath"],
                            rawSha256=image["rawSha256"],
                            pngPath=image["path"],
                            pngSha256=image["sha256"],
                            width=image["width"],
                            height=image["height"],
                            logicalWidth=logical_width,
                            logicalHeight=820 if logical_width == 1200 else 640,
                            dpi=meta["dpi"],
                            fontScale=int(font) / 100,
                            theme=theme,
                        )
                    )
            if mode:
                teardown = json.loads(
                    (captures / (mode + ".teardown.json")).read_text()
                )
                check(
                    teardown["cleaned"], "negative native resources settled " + profile
                )
        visual = dict(
            schema=1,
            kind="actual-native-captures",
            matrix=matrix,
            fonts=fonts,
            uiLockSha256=hashlib.sha256(
                (ROOT / "third_party/ui-lock.json").read_bytes()
            ).hexdigest(),
            native=native,
            binaries=record["binaries"],
            environment="owned authenticated Xvfb + Marco + Mesa software; physical monitor/IME/a11y/platform follow-ons unqualified",
            observedUTC=datetime.now(timezone.utc).isoformat(),
        )
        write_json(out / "visual-manifest.json", visual)
        manifest(out)
        check(len(record["captures"]) == 51, "51 actual lossless captures retained")
        wrong = compare_files(
            out / "panel-96/captures/test-panel-960x640-light-100.ppm",
            out / "panel-96/captures/test-panel-960x640-dark-100.ppm",
        )
        check(
            not wrong["passed"] and wrong["changedChannels"] > 0,
            "wrong real baseline pixels rejected",
        )
        try:
            compare_files(
                out / "missing-framebuffer/captures/absent.ppm",
                out / "panel-96/captures/test-panel-960x640-light-100.ppm",
            )
        except Unavailable:
            check(True, "missing capture comparison unavailable")
        else:
            check(False, "missing capture cannot compare successfully")
        if not a.capture_candidate:
            comparison = compare(out, a.baseline)
            write_json(out / "comparison.json", comparison)
            check(
                comparison["executed"] == 36 and comparison["failed"] == 0,
                "36 exact baseline comparisons",
            )
        record.update(
            status="candidate-captured-unreviewed" if a.capture_candidate else "passed",
            native=native,
            checks=checks,
            comparisonExecuted=0 if a.capture_candidate else 36,
        )
    except Unavailable as e:
        record.update(
            status="unavailable",
            unavailable=record["selected"] - record["executed"],
            reason=str(e),
            checks=checks,
        )
    except Exception as e:
        record.update(
            status="failed",
            runnerErrors=1,
            error=type(e).__name__ + ": " + str(e),
            checks=checks,
        )
    record["unexecuted"] = record["selected"] - record["executed"]
    write_json(out / "result.json", record)
    print(
        json.dumps(
            {
                k: record[k]
                for k in [
                    "selected",
                    "executed",
                    "passed",
                    "failed",
                    "skipped",
                    "unavailable",
                    "status",
                ]
            }
            | {"output": str(out)}
        )
    )
    return (
        0
        if record["status"] in ["passed", "candidate-captured-unreviewed"]
        and record["executed"] == 48
        else 2
        if record["status"] == "unavailable"
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
