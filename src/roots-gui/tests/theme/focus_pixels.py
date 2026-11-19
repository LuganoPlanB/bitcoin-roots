# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Measure an actual focused action's canvas gap and AA ring coverage."""

import argparse
import json
import math
from pathlib import Path


def measure(pixels, width, height, meta):
    scale = meta["effectiveScale"]
    dark = "-dark-" in meta["id"]
    canvas = tuple(meta.get("focusCanvas", (23, 23, 23) if dark else (255, 254, 250)))
    focus = (116, 104, 255)
    x0, y0, _, y1 = meta["primary"]
    x0, y = int(x0), int((y0 + y1) / 2)
    if not (0 < x0 < width and 0 <= y < height):
        raise ValueError("primary geometry outside framebuffer")

    def pixel(x):
        return tuple(pixels[(y * width + x) * 3 : (y * width + x) * 3 + 3])

    gap = 0
    for x in range(x0 - 1, max(0, x0 - math.ceil(12 * scale)), -1):
        if pixel(x) != canvas:
            break
        gap += 1
    axis = max(range(3), key=lambda i: abs(focus[i] - canvas[i]))
    coverage = 0.0
    samples = []
    for x in range(max(0, x0 - math.ceil(12 * scale)), x0):
        rgb = pixel(x)
        alpha = (rgb[axis] - canvas[axis]) / (focus[axis] - canvas[axis])
        mixed = tuple(
            round(canvas[i] + alpha * (focus[i] - canvas[i])) for i in range(3)
        )
        is_ring = (
            0 < alpha <= 1.01 and max(abs(mixed[i] - rgb[i]) for i in range(3)) <= 2
        )
        if is_ring:
            coverage += min(1, alpha)
        samples.append(dict(x=x, rgb=rgb, ringCoverage=alpha if is_ring else 0))

    def luminance(rgb):
        values = [x / 255 for x in rgb]
        linear = [
            x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in values
        ]
        return sum(x * y for x, y in zip(linear, [0.2126, 0.7152, 0.0722]))

    a, b = sorted([luminance(canvas), luminance(focus)])
    contrast = (b + 0.05) / (a + 0.05)
    target = bool(meta["focused"] and meta["navVisible"])
    gap_ok = gap >= math.ceil(2 * scale)
    width_ok = abs(coverage - 2 * scale) <= 0.15
    return dict(
        id=meta["id"],
        targetFocused=target,
        clearGapPixels=gap,
        minimumGapPixels=math.ceil(2 * scale),
        ringEffectivePixels=coverage,
        expectedRingPixels=2 * scale,
        contrast=contrast,
        passed=target and gap_ok and width_ok and contrast >= 3,
        failures=[
            name
            for name, ok in [
                ("wrong focus target", target),
                ("collapsed focus gap", gap_ok),
                ("incorrect ring thickness", width_ok),
                ("focus contrast below3", contrast >= 3),
            ]
            if not ok
        ],
        samples=samples,
    )


def check_file(raw, meta):
    magic, dimensions, maximum, pixels = Path(raw).read_bytes().split(b"\n", 3)
    width, height = map(int, dimensions.split())
    if magic != b"P6" or maximum != b"255" or len(pixels) != width * height * 3:
        raise ValueError("invalid native framebuffer")
    return measure(pixels, width, height, meta)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("image", type=Path)
    p.add_argument("metadata", type=Path)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    result = check_file(args.image, json.loads(args.metadata.read_text()))
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "samples"}))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
