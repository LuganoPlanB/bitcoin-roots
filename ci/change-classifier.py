#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Classify a complete pull-request file list into additive CI selections.

The caller must treat a missing, malformed, truncated, or ambiguous file list
as incomplete. Incomplete input deliberately selects broad coverage.
"""

import argparse
import json
import os


POLICY_PATH = os.path.join(os.path.dirname(__file__), "change-classifier-policy.json")
KNOWN_CATEGORIES = ("docs-only", "branding", "gui", "wallet", "build", "critical", "cpp", "unknown")
KNOWN_LABELS = ("ci:full", "ci:sanitizers", "ci:fuzz", "ci:compat", "ci:platforms")
BROAD_SELECTION = ("gui", "wallet", "sanitizers", "fuzz", "compat", "platforms")
OPTIONAL_SELECTION = ("nightly_sanitizers", "nightly_fuzz", "nightly_platforms", "nightly_full")
SAFE_POLICY = {"version": 1, "defaults": {"unknown": "broad"}}


def load_json(value, name):
    try:
        parsed = json.loads(value)
    except (TypeError, json.JSONDecodeError) as error:
        raise ValueError(f"{name} is not valid JSON") from error
    return parsed


def path_is_ambiguous(path):
    if not isinstance(path, str) or not path or "\\" in path or path.startswith("/"):
        return True
    # Inspect the raw spelling: PurePosixPath normalizes repeated separators
    # and dot segments, which would turn an ambiguous API value into a match.
    parts = path.split("/")
    return any(part in ("", ".", "..") for part in parts)


def matches(path, rule):
    return any(path.startswith(prefix) for prefix in rule["prefixes"]) or any(path.endswith(suffix) for suffix in rule["suffixes"])


def classify_path(path, policy):
    if path_is_ambiguous(path):
        return "unknown"
    for rule in policy["path_rules"]:
        if matches(path, rule):
            return rule["category"]
    return "unknown"


def result_for(files, labels, truncated, error, policy):
    complete = (isinstance(files, list) and isinstance(labels, list) and
                all(isinstance(label, str) for label in labels) and
                not truncated and not error)
    categories = set()
    if complete:
        for path in files:
            categories.add(classify_path(path, policy))
        if not categories:
            complete = False
    if not complete or "unknown" in categories:
        categories.add("unknown")

    labels = sorted({label for label in labels if isinstance(label, str) and label in KNOWN_LABELS}) if isinstance(labels, list) else []
    broad = any(policy["defaults"][category] == "broad" for category in categories)
    selected = {"baseline": True, "docs": categories == {"docs-only"}}
    selected.update({name: False for name in BROAD_SELECTION})
    selected.update({name: False for name in OPTIONAL_SELECTION})
    for category in categories:
        defaults = policy["defaults"][category]
        for name in BROAD_SELECTION if defaults == "broad" else defaults:
            selected[name] = True
    for label in labels:
        if label == "ci:full":
            selected.update({name: True for name in BROAD_SELECTION})
            selected["nightly_full"] = True
        elif label == "ci:sanitizers":
            selected["sanitizers"] = True
            selected["nightly_sanitizers"] = True
        elif label == "ci:fuzz":
            selected["fuzz"] = True
            selected["nightly_fuzz"] = True
        elif label == "ci:platforms":
            selected["platforms"] = True
            selected["nightly_platforms"] = True
        else:
            selected[label.removeprefix("ci:")] = True
    if selected["nightly_full"]:
        selected.update({name: False for name in OPTIONAL_SELECTION if name != "nightly_full"})
    return {
        "version": policy["version"],
        "categories": [category for category in KNOWN_CATEGORIES if category in categories],
        "labels": labels,
        "broad": broad,
        "complete": complete and "unknown" not in categories,
        "selected": selected,
    }


def write_github_output(result, output_path):
    lines = [f"{key}={'true' if value else 'false'}" for key, value in sorted(result["selected"].items())]
    lines.extend((
        f"broad={'true' if result['broad'] else 'false'}",
        f"complete={'true' if result['complete'] else 'false'}",
        "categories=" + json.dumps(result["categories"], separators=(",", ":")),
        "labels=" + json.dumps(result["labels"], separators=(",", ":")),
        "result=" + json.dumps(result, sort_keys=True, separators=(",", ":")),
    ))
    with open(output_path, "a", encoding="utf-8") as output:
        output.write("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--files-json", required=True, help="JSON array of repository-relative changed paths")
    parser.add_argument("--labels-json", default="[]", help="JSON array of pull-request labels")
    parser.add_argument("--truncated", action="store_true", help="Fail open because the file list is incomplete")
    parser.add_argument("--error", action="store_true", help="Fail open because file retrieval failed")
    parser.add_argument("--github-output", help="Path to append GitHub Actions outputs")
    args = parser.parse_args()
    try:
        files = load_json(args.files_json, "--files-json")
        labels = load_json(args.labels_json, "--labels-json")
        with open(POLICY_PATH, encoding="utf-8") as policy_file:
            policy = json.load(policy_file)
        if (tuple(policy["categories"]) != KNOWN_CATEGORIES or
                tuple(policy["labels"]) != KNOWN_LABELS or
                set(policy["defaults"]) != {"baseline", *KNOWN_CATEGORIES}):
            raise ValueError("policy has an unsupported schema")
        result = result_for(files, labels, args.truncated, args.error, policy)
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError):
        # The script itself must fail open if its API or policy interface changes.
        result = result_for(None, None, True, True, SAFE_POLICY)
    if args.github_output:
        write_github_output(result, args.github_output)
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))


if __name__ == "__main__":
    main()
