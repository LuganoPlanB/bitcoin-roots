# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Check actual shell geometry and amount presentation receipts."""

import math
import re


def check(metadata, rendered="", semantic=""):
    errors = []
    m = metadata
    scale = m["effectiveScale"]
    if m["accountWidth"] != 0 or m["contentX"] != m["railX"] + m["railWidth"]:
        errors.append("account strip or unexplained gap")
    if m["railWidth"] != (96 if m["width"] < 1100 else 180) * scale:
        errors.append("rail contract")
    if m["tabsHeight"] != 36 * scale or m["statusHeight"] != 36 * scale:
        errors.append("region height contract")
    if m.get("hasDocuments") and not 34 * scale <= m["tabButtonHeight"] <= 38 * scale:
        errors.append("actual tab hit height contract")
    p = m["primary"]
    if (
        not m["primaryVisible"]
        or not all(math.isfinite(v) for v in p)
        or p[2] <= p[0]
        or p[3] <= p[1]
        or p[0] < 0
        or p[1] < 0
        or p[2] > m["width"]
        or p[3] > m["height"]
    ):
        errors.append("primary not reachable")
    if m["section"] != m["expectedSection"] or not m["targetAccepted"]:
        errors.append("wrong navigation target")
    if m["privacyHidden"] and (
        re.search(r"\d+\.\d{8}", rendered)
        or re.search(r"\d+\.\d{8}", semantic)
        or any(
            str(1000000 + i) in rendered or str(1000000 + i) in semantic
            for i in range(40)
        )
    ):
        errors.append("hidden financial value leaked")
    return dict(passed=not errors, errors=errors)
