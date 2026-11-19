# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
import sys

sys.dont_write_bytecode = True
from shell_checks import check

m = dict(
    width=960,
    height=640,
    effectiveScale=2,
    accountWidth=0,
    railX=0,
    railWidth=192,
    contentX=192,
    tabsHeight=72,
    statusHeight=72,
    primaryVisible=True,
    primary=[240, 480, 500, 544],
    section=0,
    expectedSection=0,
    targetAccepted=True,
    privacyHidden=True,
    hasDocuments=True,
    tabButtonHeight=68,
)
assert check(m, "Hidden", "Hidden")["passed"]
for delta in [
    dict(accountWidth=48),
    dict(contentX=240),
    dict(railWidth=96),
    dict(tabsHeight=36),
    dict(statusHeight=36),
    dict(primaryVisible=False),
    dict(primary=[240, 600, 500, 664]),
    dict(section=1, targetAccepted=False),
]:
    assert not check(m | delta, "Hidden", "Hidden")["passed"], delta
assert not check(m, "tooltip 0.01000000", "Hidden")["passed"]
assert not check(m, "Hidden", "amount 0.01000000")["passed"]
assert not check(m, "tooltip 1000000", "Hidden")["passed"]
assert not check(m, "Hidden", "amount 1000000")["passed"]
assert not check(m | dict(tabButtonHeight=48), "Hidden", "Hidden")["passed"]
print("14 shell checker positive/negative cases passed")
