# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Capture the production wallet workspace at the native release profiles."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from xml.etree import ElementTree as ET

TESTS = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path[:0] = [str(TESTS), str(TESTS / "platform")]
from capture_manifest import encode_capture
from protocol.signature_exchange import make_fixtures
from support.runtime import binary_identity, evidence_directory, write_json


PROFILES = {
    "reference-light": (1200, "light", 1),
    "reference-dark": (1200, "dark", 1),
    "compact-light": (960, "light", 1.5),
    "compact-dark": (960, "dark", 1.5),
    "stress-light": (960, "light", 2),
    "stress-dark": (960, "dark", 2),
}


def now():
    return datetime.now(timezone.utc).isoformat()


def identity(path):
    return {
        "path": str(path),
        "bytes": path.stat().st_size,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }


def retain_pixels(path):
    raw = path.read_bytes()
    record = encode_capture(path)
    png = path.with_suffix(".png").read_bytes()
    offset, compressed = 8, b""
    while offset < len(png):
        size, kind = struct.unpack_from("!I4s", png, offset)
        if kind == b"IDAT":
            compressed += png[offset + 8 : offset + 8 + size]
        offset += size + 12
    data = zlib.decompress(compressed)
    stride = record["width"] * 3 + 1
    if len(data) != stride * record["height"] or any(data[::stride]):
        raise ValueError(f"unexpected PNG filter/extent for {path}")
    rgb = b"".join(data[y + 1 : y + stride] for y in range(0, len(data), stride))
    if raw.split(b"\n", 3)[3] != rgb:
        raise ValueError(f"lossless framebuffer reconstruction failed for {path}")
    record["rgbSha256"] = hashlib.sha256(rgb).hexdigest()
    record["rawReconstruction"] = (
        "P6 ASCII header with recorded dimensions and max 255, followed by top-down RGB PNG bytes"
    )
    path.unlink()
    return record


def run_profile(args, out, fixture, profile):
    width, theme, scale = PROFILES[profile]
    directory = out / profile
    captures = directory / "captures"
    directory.mkdir()
    argv = [
        sys.executable,
        "-B",
        str(TESTS / "platform/owned_display.py"),
        "--output",
        str(directory),
        "--timeout",
        "120",
        "--",
        str(args.binary),
        str(captures),
        str(args.fonts),
        str(fixture),
        str(width),
        theme,
        str(scale),
        args.case,
    ]
    result = {
        "profile": profile,
        "argv": argv,
        "cwd": str(Path.cwd()),
        "startUTC": now(),
    }
    with (directory / "outer.log").open("wb") as log:
        process = subprocess.run(
            argv, stdout=log, stderr=subprocess.STDOUT, timeout=145
        )
    result.update(exit=process.returncode, endUTC=now())
    process_record = json.loads((directory / "processes.json").read_text())
    if not process_record["child"]["settled"] or not all(
        item["settled"] and not item.get("remainingLiveGroupMembers")
        for item in process_record["processes"]
    ):
        raise RuntimeError(f"{profile} left owned processes")
    results_path = captures / "result.jsonl"
    rows = [json.loads(row) for row in results_path.read_text().splitlines()]
    expected = [
        row["case"]
        for row in args.inventory
        if (scale != 2 or row["stress"])
        and (args.case == "all" or row["case"] == args.case)
    ]
    if [row["case"] for row in rows] != expected or not expected:
        raise RuntimeError(f"{profile} did not execute the exact selected cases")
    images = []
    for ppm in sorted(captures.glob("*.ppm")):
        stem = ppm.stem
        metadata = json.loads(ppm.with_suffix(".json").read_text())
        for key, value in {
            "width": width,
            "height": 820 if width == 1200 else 640,
            "dark": theme == "dark",
            "scale": scale,
        }.items():
            if metadata[key] != value:
                raise ValueError(f"{profile}/{stem} observed {key} differs")
        image = retain_pixels(ppm)
        image.update(
            meta=metadata,
            semantics=identity(captures / (stem + ".semantics.json")),
            presentation=identity(captures / (stem + ".presentation.txt")),
        )
        images.append(image)
    result.update(
        cases=rows, images=images, owned=identity(directory / "processes.json")
    )
    write_json(directory / "capture-manifest.json", result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ("binary", "fonts", "openssl", "output"):
        parser.add_argument("--" + field, type=Path, required=True)
    parser.add_argument("--profile", choices=PROFILES, action="append")
    parser.add_argument("--case", default="all")
    args = parser.parse_args()
    output = Path(
        tempfile.mkdtemp(prefix="release-visual-", dir=evidence_directory(args.output))
    )
    args.inventory = json.loads(
        subprocess.check_output([args.binary, "--list"], timeout=10)
    )
    fixtures = output / "public-fixtures"
    fixtures.mkdir()
    make_fixtures(fixtures, args.openssl)
    suite = ET.Element("testsuite", name="roots_gui.visual.release_workspace")
    results = []
    counts = dict(selected=0, executed=0, passed=0, failed=0, skipped=0)
    for profile in args.profile or PROFILES:
        scale = PROFILES[profile][2]
        counts["selected"] += sum(
            (scale != 2 or row["stress"])
            and (args.case == "all" or row["case"] == args.case)
            for row in args.inventory
        )
        try:
            result = run_profile(args, output, fixtures / "v2", profile)
            results.append(result)
            for row in result["cases"]:
                entry = ET.SubElement(
                    suite, "testcase", classname=profile, name=row["case"]
                )
                counts["executed"] += 1
                passed = row["passed"] and row["cleaned"]
                counts["passed" if passed else "failed"] += 1
                if not passed:
                    ET.SubElement(entry, "failure", message=row["failure"])
            if bool(result["exit"]) != any(
                not row["passed"] for row in result["cases"]
            ):
                raise RuntimeError("native process exit differs from case outcomes")
        except (
            OSError,
            KeyError,
            ValueError,
            RuntimeError,
            subprocess.TimeoutExpired,
        ) as error:
            results.append({"profile": profile, "error": str(error)})
            entry = ET.SubElement(suite, "testcase", classname=profile, name="harness")
            ET.SubElement(entry, "error", message=str(error))
    for key, value in (
        ("tests", len(suite)),
        ("errors", len(suite.findall("./testcase/error"))),
        ("failures", counts["failed"]),
    ):
        suite.set(key, str(value))
    ET.ElementTree(suite).write(
        output / "junit.xml", encoding="utf-8", xml_declaration=True
    )
    gates = (
        counts["selected"] > 0
        and counts["passed"] == counts["selected"]
        and all("error" not in item for item in results)
    )
    manifest = {
        "counts": counts,
        "passed": gates,
        "profiles": results,
        "binary": binary_identity(args.binary),
        "openssl": binary_identity(args.openssl),
        "fonts": [
            identity(font) for font in sorted(args.fonts.glob("*")) if font.is_file()
        ],
        "uiPins": identity(TESTS.parent / "third_party/ui-lock.json"),
        "fixtureSource": identity(TESTS / "protocol/signature_exchange.py"),
        "publicFixtures": [identity(path) for path in sorted(fixtures.glob("*/*"))],
        "environment": "Actual X11/GLFW/OpenGL3 framebuffer on owned Xvfb with Marco and software Mesa",
        "limits": "Public deterministic state and local signature exchange only. No physical device, external service or installed delivery qualification.",
    }
    write_json(output / "result.json", manifest)
    print(json.dumps(dict(**counts, passedAll=gates, output=str(output))))
    return 0 if gates else 1


if __name__ == "__main__":
    raise SystemExit(main())
