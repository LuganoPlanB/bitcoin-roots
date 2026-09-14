#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Fail-open decision logic for the scheduled nightly workflow.

The workflow supplies the current scheduled run followed by its immediate
predecessor from GitHub's Actions API. A no-op is safe only after validating
that complete, ordered response and finding a successful predecessor at the
same commit.
"""

import argparse
import json


RUN = {"run": True, "reason": "uncertain-history"}


def should_run(event_name, current_run_id, current_sha, history):
    """Return a fail-open run decision for one Actions event."""
    if event_name != "schedule":
        return {"run": True, "reason": "non-scheduled-event"}
    if not isinstance(current_run_id, int) or not isinstance(current_sha, str) or not current_sha:
        return RUN
    if not isinstance(history, dict) or history.get("complete") is not True:
        return RUN
    runs = history.get("workflow_runs")
    # The API request deliberately asks for exactly two records. Any other
    # cardinality is missing or truncated history, never a basis for skipping.
    if not isinstance(runs, list) or len(runs) != 2:
        return RUN
    required = ("id", "event", "status", "conclusion", "head_sha")
    if any(not isinstance(run, dict) or any(key not in run for key in required) for run in runs[:2]):
        return RUN
    current, previous = runs[:2]
    if any(isinstance(run["id"], bool) or not isinstance(run["id"], int) or
           not isinstance(run["event"], str) or not run["event"] or
           not isinstance(run["status"], str) or not run["status"] or
           not isinstance(run["head_sha"], str) or not run["head_sha"]
           for run in (current, previous)):
        return RUN
    if (current["id"] != current_run_id or current["event"] != "schedule" or
            current["head_sha"] != current_sha or current["status"] not in ("queued", "in_progress") or
            current["conclusion"] is not None):
        return RUN
    if previous["id"] >= current["id"]:
        return RUN
    if (previous["event"] != "schedule" or previous["status"] != "completed" or
            previous["conclusion"] != "success"):
        return RUN
    if previous["head_sha"] == current_sha:
        return {"run": False, "reason": "unchanged-success"}
    return {"run": True, "reason": "new-commit"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--event-name", required=True)
    parser.add_argument("--current-run-id", type=int, required=True)
    parser.add_argument("--current-sha", required=True)
    parser.add_argument("--history-json", default='{"complete": false}')
    parser.add_argument("--github-output")
    args = parser.parse_args()
    try:
        history = json.loads(args.history_json)
    except json.JSONDecodeError:
        history = None
    result = should_run(args.event_name, args.current_run_id, args.current_sha, history)
    if args.github_output:
        with open(args.github_output, "a", encoding="utf-8") as output:
            output.write(f"run={'true' if result['run'] else 'false'}\n")
            output.write(f"reason={result['reason']}\n")
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
