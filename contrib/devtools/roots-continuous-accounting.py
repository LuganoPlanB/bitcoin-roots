#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Fail closed unless every changed path/hunk has a specific adaptation disposition."""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from datetime import date
from pathlib import Path


DISPOSITIONS = {"add", "update", "absorb", "exempt", "embargoed"}
ATOM_KINDS = {"add", "delete", "modify", "binary", "mode"}
SHA256_RE = re.compile(r"^sha256:[0-9a-f]{64}$")


def git(repository, *args):
    result = subprocess.run(
        ["git", "-C", str(repository), "--no-pager", *args],
        capture_output=True,
    )
    if result.returncode:
        raise ValueError("git input is invalid")
    return result.stdout


def atoms(repository, base, head):
    if (
        len(base) != 40
        or len(head) != 40
        or any(char not in "0123456789abcdef" for char in base + head)
    ):
        raise ValueError("commits must be full lowercase ids")
    if git(repository, "status", "--porcelain=v1", "-z"):
        raise ValueError("repository must be clean")
    names = git(
        repository,
        "diff",
        "--name-status",
        "-z",
        "--no-renames",
        base,
        head,
    ).split(b"\0")
    result = []
    for status, raw_path in zip(names[0::2], names[1::2]):
        if not status or not raw_path:
            continue
        try:
            path = raw_path.decode("utf-8", "strict")
        except UnicodeDecodeError as error:
            raise ValueError("non-UTF8 path is unsupported") from error
        patch = git(
            repository,
            "diff",
            "--no-ext-diff",
            "--binary",
            "--full-index",
            "--unified=0",
            base,
            head,
            "--",
            path,
        )
        kind = {b"A": "add", b"D": "delete"}.get(status[:1], "modify")
        if b"GIT binary patch" in patch:
            kind = "binary"
        if b"old mode " in patch or b"new mode " in patch:
            mode_lines = b"\n".join(
                line
                for line in patch.splitlines()
                if line.startswith((b"old mode ", b"new mode "))
            )
            result.append(
                (
                    path,
                    "mode",
                    "sha256:" + hashlib.sha256(mode_lines).hexdigest(),
                )
            )
        blocks = [b"@@ " + block for block in patch.split(b"\n@@ ")[1:]]
        if not blocks:
            if kind == "modify" and b"old mode " in patch:
                continue
            blocks = [patch]
        for block in blocks:
            result.append(
                (path, kind, "sha256:" + hashlib.sha256(block).hexdigest())
            )
    return set(result)


def validate(value, observed=None, public_release=False):
    if (
        not isinstance(value, dict)
        or set(value) != {"schema_version", "changes"}
        or not isinstance(value["schema_version"], int)
        or isinstance(value["schema_version"], bool)
        or value["schema_version"] != 1
        or not isinstance(value["changes"], list)
    ):
        raise ValueError("accounting envelope is invalid")
    seen = set()
    for item in value["changes"]:
        common = {"path", "kind", "digest", "disposition", "adaptation"}
        normal = common | {
            "risk",
            "tests",
            "dependencies",
            "provenance",
            "replay_impact",
            "scope",
            "rationale",
        }
        embargo = common | {
            "tracking_reference",
            "reconcile_by",
            "reconciliation_state",
        }
        if (
            not isinstance(item, dict)
            or (set(item) != normal and set(item) != embargo)
        ):
            raise ValueError("accounting entry is invalid")
        path, kind, hunk = item["path"], item["kind"], item["digest"]
        if (
            not isinstance(path, str)
            or not path
            or path == "."
            or any(character in path for character in "*?[]")
            or not isinstance(kind, str)
            or kind not in ATOM_KINDS
            or not isinstance(hunk, str)
            or not SHA256_RE.fullmatch(hunk)
            or (path, kind, hunk) in seen
        ):
            raise ValueError("accounting ownership must be specific")
        seen.add((path, kind, hunk))
        if (
            not isinstance(item["disposition"], str)
            or item["disposition"] not in DISPOSITIONS
            or not isinstance(item["adaptation"], str)
            or not item["adaptation"]
            or any(char in item["adaptation"] for char in "*?[]")
        ):
            raise ValueError("accounting evidence is invalid")
        if item["disposition"] == "embargoed":
            if (
                set(item) != embargo
                or not all(
                    isinstance(item[key], str) and item[key]
                    for key in ("tracking_reference", "reconcile_by")
                )
                or item["reconciliation_state"] != "pending"
            ):
                raise ValueError("embargo accounting is invalid")
            if not re.fullmatch(r"[A-Z0-9-]{3,64}", item["tracking_reference"]):
                raise ValueError("embargo tracking reference is invalid")
            try:
                date.fromisoformat(item["reconcile_by"])
            except ValueError as error:
                raise ValueError("embargo reconciliation deadline is invalid") from error
        elif (
            set(item) != normal
            or not all(
                isinstance(item[key], str) and item[key]
                for key in (
                    "risk",
                    "tests",
                    "dependencies",
                    "provenance",
                    "replay_impact",
                    "scope",
                    "rationale",
                )
            )
            or item["scope"] != path
        ):
            raise ValueError("accounting evidence is invalid")
    if observed is not None and seen != observed:
        raise ValueError("changed hunk atoms are not exactly accounted")
    if public_release and any(
        item["disposition"] == "embargoed" for item in value["changes"]
    ):
        raise ValueError("embargoed atoms require reconciliation")
    return value


if __name__ == "__main__":
    try:
        parser = argparse.ArgumentParser()
        parser.add_argument("--repository", type=Path, required=True)
        parser.add_argument("--base", required=True)
        parser.add_argument("--head", required=True)
        parser.add_argument("--record", type=Path, required=True)
        parser.add_argument("--public-release", action="store_true")
        args = parser.parse_args()
        validate(
            json.loads(args.record.read_text()),
            atoms(args.repository, args.base, args.head),
            args.public_release,
        )
    except (IndexError, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"roots-continuous-accounting: {error}", file=sys.stderr)
        raise SystemExit(1)
