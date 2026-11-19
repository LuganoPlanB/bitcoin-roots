# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Audit all four rendered focus edges inside a fixed-height status strip."""

import math
from pathlib import Path


def measure(pixels, width, height, m):
    scale = m["effectiveScale"]
    dark = "-dark-" in m["id"]
    canvas = (23, 23, 23) if dark else (255, 254, 250)
    focus = (116, 104, 255)
    x0, y0, x1, y1 = m["statusVisual"]
    hit = m["statusHit"]
    axis = max(range(3), key=lambda i: abs(focus[i] - canvas[i]))
    edges = []
    samples = {
        "left": (int(x0) - 1, int((y0 + y1) / 2), -1, 0),
        "right": (int(x1), int((y0 + y1) / 2), 1, 0),
        "top": (int((x0 + x1) / 2), int(y0) - 1, 0, -1),
        "bottom": (int((x0 + x1) / 2), int(y1), 0, 1),
    }
    for side, (sx, sy, dx, dy) in samples.items():
        gap = 0
        coverage = 0.0
        clear = True
        for i in range(math.ceil(12 * scale)):
            x, y = sx + dx * i, sy + dy * i
            if not (0 <= x < width and 0 <= y < height):
                break
            rgb = tuple(pixels[(y * width + x) * 3 : (y * width + x) * 3 + 3])
            if clear and rgb == canvas:
                gap += 1
            else:
                clear = False
            alpha = (rgb[axis] - canvas[axis]) / (focus[axis] - canvas[axis])
            mixed = tuple(
                round(canvas[j] + alpha * (focus[j] - canvas[j])) for j in range(3)
            )
            if 0 < alpha <= 1.01 and max(abs(mixed[j] - rgb[j]) for j in range(3)) <= 2:
                coverage += min(1, alpha)
        edges.append(
            dict(
                side=side,
                clearGapPixels=gap,
                ringEffectivePixels=coverage,
                passed=gap >= math.ceil(2 * scale)
                and abs(coverage - 2 * scale) <= 0.15,
            )
        )

    def luminance(rgb):
        linear = [
            x / 255 / 12.92
            if x / 255 <= 0.04045
            else ((x / 255 + 0.055) / 1.055) ** 2.4
            for x in rgb
        ]
        return sum(x * y for x, y in zip(linear, [0.2126, 0.7152, 0.0722]))

    lo, hi = sorted([luminance(canvas), luminance(focus)])
    contrast = (hi + 0.05) / (lo + 0.05)
    target = m["statusFocused"] and m["navVisible"]
    geometry = (
        abs(hit[3] - hit[1] - 32 * scale) < 0.01
        and abs(m["statusHeight"] - 36 * scale) < 0.01
        and hit[0] <= x0 < x1 <= hit[2]
        and hit[1] <= y0 < y1 <= hit[3]
    )
    return dict(
        id=m["id"],
        targetFocused=target,
        hitAndStripGeometry=geometry,
        contrast=contrast,
        edges=edges,
        passed=bool(
            target and geometry and contrast >= 3 and all(x["passed"] for x in edges)
        ),
    )


def check(raw, m):
    _, dims, _, pixels = Path(raw).read_bytes().split(b"\n", 3)
    w, h = map(int, dims.split())
    return measure(pixels, w, h, m)
