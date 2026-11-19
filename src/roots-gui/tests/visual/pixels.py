# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Exact real-framebuffer comparison; no image synthesis or baseline adoption."""

import hashlib
import json
from pathlib import Path


class Unavailable(RuntimeError):
    pass


def expected_ids():
    return {
        f"{profile}/{prefix}-{width}x{820 if width == 1200 else 640}-{theme}-{scale}"
        for profile, prefix in [
            ("bootstrap-96", "bootstrap"),
            ("bootstrap-144", "bootstrap"),
            ("panel-96", "test-panel"),
        ]
        for width in [1200, 960]
        for theme in ["light", "dark"]
        for scale in [100, 150, 200]
    }


def ppm(path):
    if not path.is_file():
        raise Unavailable("required actual framebuffer missing")
    magic, size, maximum, pixels = path.read_bytes().split(b"\n", 3)
    width, height = map(int, size.split())
    if (
        magic != b"P6"
        or maximum != b"255"
        or width <= 0
        or height <= 0
        or len(pixels) != width * height * 3
    ):
        raise ValueError("invalid actual framebuffer format")
    return width, height, pixels


def bounded_file(folder, relative):
    candidate = (folder / relative).resolve()
    if folder.resolve() not in candidate.parents:
        raise ValueError("capture path leaves artifact directory")
    return candidate


def manifest(folder, curated=False):
    folder = Path(folder).resolve()
    path = folder / "visual-manifest.json"
    if not path.is_file():
        raise Unavailable("visual manifest unavailable")
    raw = path.read_bytes()
    data = json.loads(raw)
    if (
        data.get("kind") != "actual-native-captures"
        or {x["id"] for x in data.get("matrix", [])} != expected_ids()
        or len(data["matrix"]) != 36
    ):
        raise ValueError("visual selection must contain36 exact unique identities")
    if curated:
        review = folder / "curation.json"
        if not review.is_file():
            raise Unavailable("explicitly inspected baseline required")
        c = json.loads(review.read_text())
        if (
            c.get("manifestSha256") != hashlib.sha256(raw).hexdigest()
            or c.get("method") != "manual-inspection"
        ):
            raise ValueError("baseline curation identity mismatch")
    for record in data["matrix"]:
        image = bounded_file(folder, record["rawPath"])
        ppm(image)
        if hashlib.sha256(image.read_bytes()).hexdigest() != record["rawSha256"]:
            raise ValueError("actual capture identity mismatch")
    return data


def compare_files(actual, baseline):
    aw, ah, a = ppm(actual)
    bw, bh, b = ppm(baseline)
    if (aw, ah) != (bw, bh):
        raise ValueError("baseline framebuffer dimensions differ")
    changed = 0 if a == b else sum(x != y for x, y in zip(a, b))
    return dict(
        width=aw,
        height=ah,
        changedChannels=changed,
        totalChannels=len(a),
        fraction=changed / len(a),
        passed=changed == 0,
    )


def compare(current, baseline):
    current = Path(current).resolve()
    baseline = Path(baseline).resolve()
    a = manifest(current)
    b = manifest(baseline, curated=True)
    if a["fonts"] != b["fonts"] or a["uiLockSha256"] != b["uiLockSha256"]:
        raise ValueError("baseline font/source input identity differs")
    indexed = {x["id"]: x for x in b["matrix"]}
    rows = []
    for record in a["matrix"]:
        old = indexed[record["id"]]
        if any(
            record[k] != old[k]
            for k in ["width", "height", "dpi", "fontScale", "theme"]
        ):
            raise ValueError("baseline scenario metadata differs")
        row = compare_files(
            bounded_file(current, record["rawPath"]),
            bounded_file(baseline, old["rawPath"]),
        )
        row["id"] = record["id"]
        rows.append(row)
    failed = sum(not r["passed"] for r in rows)
    return dict(
        selected=36,
        executed=36,
        passed=36 - failed,
        failed=failed,
        skipped=0,
        policy="exact RGB channels; no tolerance or ignored regions",
        cases=rows,
    )
