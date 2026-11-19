# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Synthetic signal fixtures validate the checker, never native qualification."""

from focus_pixels import measure
import json

checks = []
for scale in [1, 1.5, 2]:
    width, height, x0 = 80, 3, 40
    canvas, violet = bytes([23, 23, 23]), bytes([116, 104, 255])
    meta = dict(
        id="checker-dark-fixture",
        effectiveScale=scale,
        primary=[x0, 0, 60, 2],
        focused=True,
        navVisible=True,
    )
    gap, stroke = int(2 * scale), int(2 * scale)
    data = bytearray(canvas * width * height)
    for x in range(x0 - gap - stroke, x0 - gap):
        data[(width + x) * 3 : (width + x) * 3 + 3] = violet
    checks.append(
        (
            "valid signal scale " + str(scale),
            measure(data, width, height, meta)["passed"],
        )
    )
    collapsed = bytearray(canvas * width * height)
    for x in range(x0 - stroke, x0):
        collapsed[(width + x) * 3 : (width + x) * 3 + 3] = violet
    result = measure(collapsed, width, height, meta)
    checks.append(
        (
            "collapsed gap rejected " + str(scale),
            not result["passed"] and "collapsed focus gap" in result["failures"],
        )
    )
    wrong = measure(data, width, height, meta | dict(focused=False))
    checks.append(
        (
            "wrong target rejected despite visible navigation " + str(scale),
            not wrong["passed"] and "wrong focus target" in wrong["failures"],
        )
    )
failed = [name for name, ok in checks if not ok]
print(
    json.dumps(
        dict(
            selected=len(checks),
            executed=len(checks),
            passed=len(checks) - len(failed),
            failed=len(failed),
            skipped=0,
            failures=failed,
        )
    )
)
raise SystemExit(bool(failed))
