# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Non-vacuous negative checks for the actual snapshot leak checker."""

import sys

sys.dont_write_bytecode = True
from privacy_snapshots import check_snapshot

base = dict(
    privacyHidden=True,
    chartSamples=0,
    identityAccents=1,
    reducedMotion=True,
    privacyFeedback=1,
)
assert check_snapshot("Hidden", "Hidden / Amount chart hidden", base)["passed"]
for channel in ["rendered", "semantic"]:
    for value in ["0.01000000", "1000000", "0.06000000", "1.25"]:
        rendered = "Hidden"
        semantic = "Hidden / Amount chart hidden"
        if channel == "rendered":
            rendered += " tooltip " + value
        else:
            semantic += " " + value
        assert not check_snapshot(rendered, semantic, base)["passed"], (channel, value)
for key, value in [
    ("chartSamples", 1),
    ("identityAccents", 2),
    ("privacyFeedback", 0.5),
]:
    assert not check_snapshot(
        "Hidden", "Hidden / Amount chart hidden", base | {key: value}
    )["passed"], key
assert not check_snapshot("", "", base)["passed"]
assert check_snapshot(
    "0.01000000",
    "0.01000000",
    base | dict(privacyHidden=False, chartSamples=3, privacyFeedback=0),
)["passed"]
destination = "bcrt1qfull-destination###literal-%s-000000000000000000000001"
rendered = "Review amount (BTC): Hidden / Turn off Hide amounts / " + destination
semantic = "Review amount (BTC) | Hidden / Amount chart hidden / " + destination
review = base | dict(reviewSurface=True)
assert check_snapshot(rendered, semantic, review)["passed"]
assert not check_snapshot(rendered.replace("Hidden", "0.01000000"), semantic, review)[
    "passed"
]
assert not check_snapshot(rendered, semantic.replace("Hidden", "0.01000000"), review)[
    "passed"
]
assert not check_snapshot(rendered.replace(destination, ""), semantic, review)["passed"]
assert not check_snapshot(
    rendered.replace("Turn off Hide amounts", ""), semantic, review
)["passed"]
assert check_snapshot(
    rendered.replace("Hidden", "0.01000000"),
    semantic.replace("Hidden", "0.01000000"),
    review | dict(privacyHidden=False, privacyFeedback=0, chartSamples=3),
)["passed"]
print("20 snapshot checker positive/negative cases passed")
