# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Reject clipped, collapsed and wrong-target status focus receipts."""

import unittest
from status_focus import measure


class StatusFocus(unittest.TestCase):
    def fixture(self):
        w, h = 80, 36
        canvas = (255, 254, 250)
        focus = (116, 104, 255)
        data = bytearray(canvas * (w * h))
        meta = dict(
            id="status-light-fixture",
            effectiveScale=1,
            statusVisual=[20, 8, 60, 28],
            statusHit=[14, 2, 66, 34],
            statusHeight=36,
            statusFocused=True,
            navVisible=True,
        )
        # Independent crisp test edges:2px ring,3px gap on every side.
        for y in range(3, 33):
            for x in range(15, 65):
                if x < 17 or x >= 63 or y < 5 or y >= 31:
                    data[(y * w + x) * 3 : (y * w + x) * 3 + 3] = bytes(focus)
        return data, w, h, meta

    def test_complete(self):
        self.assertTrue(measure(*self.fixture())["passed"])

    def test_wrong_target(self):
        d, w, h, m = self.fixture()
        m["statusFocused"] = False
        self.assertFalse(measure(d, w, h, m)["passed"])

    def test_clipped_bottom(self):
        d, w, h, m = self.fixture()
        d[31 * w * 3 : 33 * w * 3] = bytes((255, 254, 250)) * (2 * w)
        self.assertFalse(measure(d, w, h, m)["passed"])

    def test_collapsed_gap(self):
        d, w, h, m = self.fixture()
        for y in range(8, 28):
            d[(y * w + 19) * 3 : (y * w + 20) * 3] = bytes((116, 104, 255))
        self.assertFalse(measure(d, w, h, m)["passed"])

    def test_wrong_hit_metric(self):
        d, w, h, m = self.fixture()
        m["statusHit"][3] = 35
        self.assertFalse(measure(d, w, h, m)["passed"])


if __name__ == "__main__":
    unittest.main()
