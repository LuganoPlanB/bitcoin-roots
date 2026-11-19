# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Inspect actual production amount presentation/annotation snapshots."""

from pathlib import Path
import json

# Exact fixture amounts, totals, fee rate, and integer encodings. The native
# inputs deliberately use these known public values; arbitrary labels aren't
# classified by guessing that every numeral is financial data.
SENSITIVE = (
    "0.01000000",
    "0.02000000",
    "0.03000000",
    "0.06000000",
    "1000000",
    "2000000",
    "3000000",
    "6000000",
    "1.25",
)


def check_snapshot(rendered, semantic, metadata):
    hidden = metadata["privacyHidden"]
    leak = [value for value in SENSITIVE if value in rendered or value in semantic]
    errors = []
    if hidden:
        if leak:
            errors.append("hidden amount reached rendered/tooltip/annotation output")
        if (
            "Hidden" not in rendered
            or "Hidden" not in semantic
            or "Amount chart hidden" not in semantic
        ):
            errors.append("missing real hidden presentation coverage")
        if metadata["chartSamples"] != 0:
            errors.append("hidden chart received numeric samples")
    elif "0.01000000" not in rendered or "0.01000000" not in semantic:
        errors.append("visible positive control missing")
    if metadata.get("reviewSurface"):
        destination = "bcrt1qfull-destination###literal-%s-000000000000000000000001"
        if destination not in rendered or destination not in semantic:
            errors.append("full review destination missing")
        amount = "Hidden" if hidden else "0.01000000"
        if (
            "Review amount (BTC): " + amount not in rendered
            or "Review amount (BTC) | " + amount not in semantic
        ):
            errors.append("review amount did not follow current privacy policy")
        if hidden and "Turn off Hide amounts" not in rendered:
            errors.append("explicit reveal instruction missing")
    if metadata["identityAccents"] > 1:
        errors.append("exceptional identity accent budget exceeded")
    if metadata["reducedMotion"] and metadata["privacyFeedback"] != int(hidden):
        errors.append("reduced-motion transition did not snap")
    return dict(hidden=hidden, passed=not errors, errors=errors)


def check_folder(folder):
    rows = []
    for p in sorted(Path(folder).glob("component-gallery-*.json")):
        if p.name.endswith(".teardown.json"):
            continue
        metadata = json.loads(p.read_text())
        row = check_snapshot(
            p.with_suffix(".presentation.txt").read_text(),
            p.with_suffix(".semantics.txt").read_text(),
            metadata,
        )
        row["id"] = metadata["id"]
        rows.append(row)
    failed = sum(not row["passed"] for row in rows)
    return dict(
        selected=132,
        executed=len(rows),
        passed=len(rows) - failed,
        failed=failed,
        skipped=0,
        hidden=sum(row["hidden"] for row in rows),
        cases=rows,
    )
