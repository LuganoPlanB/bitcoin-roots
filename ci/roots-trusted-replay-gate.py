#!/usr/bin/env python3
"""Create a bounded, deterministic input lock for trusted Roots replay CI."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


MAX_REPORT_BYTES = 65_536
SHA1 = re.compile(r"^[0-9a-f]{40}$")
ALLOWED_LATER_BASES = {"none"}
LOCKS = {
    "v29.3": ("99003bed87333f1be51bf3070235591b3a72f007", "d7910bd5e9335128932f1f848a767d773895c4a4"),
    "v29.4": ("3fc0865963a38b871e9f7d94e6151c4953563516", "38ad59b187f59647eb90ad1347bc481485ef4d01"),
}
LOCKED_DIGESTS = {
    "manifest": "sha256:04a713696ce91615fb8a28e2283b8d47b5c37df48a32afd45774954d206c2adb",
    "methodology": "sha256:e9f5d5406e5fbd33880bb5ff250e1fb519a3b76bc1844980af954a556702b118",
}
EXPECTED_ARTIFACTS = frozenset({"replay-review.json", "replay-review.txt", "replay-generated-series.patch", "comparison.json"})


class GateError(ValueError):
    pass


def decision(mode: str, changed: str, manual_run: str = "auto") -> str:
    """Schedules always detect drift; only unchanged manual auto may no-op."""
    if mode not in {"schedule", "manual"} or changed not in {"true", "false"} or manual_run not in {"auto", "force"}:
        raise GateError("trusted replay event is invalid")
    return "no-op" if mode == "manual" and manual_run == "auto" and changed == "false" else "replay"


def verify_artifacts(directory: Path, expected: dict[str, str]) -> None:
    if set(expected) != EXPECTED_ARTIFACTS or not directory.is_dir() or directory.is_symlink():
        raise GateError("replay artifact manifest is invalid")
    entries = list(directory.iterdir())
    if {entry.name for entry in entries} != EXPECTED_ARTIFACTS or len(entries) != len(EXPECTED_ARTIFACTS):
        raise GateError("replay artifact names are invalid")
    total = 0
    for entry in entries:
        if not entry.is_file() or entry.is_symlink() or entry.stat().st_size > MAX_REPORT_BYTES:
            raise GateError("replay artifact bounds are invalid")
        total += entry.stat().st_size
        if digest(entry) != expected[entry.name]:
            raise GateError("replay artifact digest differs")
    if total > MAX_REPORT_BYTES:
        raise GateError("replay artifact aggregate is too large")


def read_json(path: Path, name: str) -> dict:
    if not path.is_file() or path.is_symlink() or path.stat().st_size > MAX_REPORT_BYTES:
        raise GateError(f"{name} is unavailable")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise GateError(f"{name} is invalid") from error
    if not isinstance(value, dict):
        raise GateError(f"{name} is invalid")
    return value


def digest(path: Path) -> str:
    return "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()


def report(mode: str, changed: str, later_base: str, manifest: Path, methodology: Path, fixture: Path, manual_run: str = "auto") -> dict:
    if mode not in {"schedule", "manual"} or changed not in {"true", "false"}:
        raise GateError("trusted replay event is invalid")
    if later_base not in ALLOWED_LATER_BASES:
        raise GateError("later base is not allowlisted")
    fixture_value = read_json(fixture, "migration fixture")
    versions = fixture_value.get("core_inputs")
    if not isinstance(versions, dict) or not all(isinstance(versions.get(version), dict) for version in ("v29.3", "v29.4")):
        raise GateError("migration fixture lock is invalid")
    for version in ("v29.3", "v29.4"):
        lock = versions[version]
        if not SHA1.fullmatch(lock.get("commit", "")) or not SHA1.fullmatch(lock.get("tree", "")):
            raise GateError("migration fixture immutable lock is invalid")
        if (lock["commit"], lock["tree"]) != LOCKS[version]:
            raise GateError("migration fixture differs from accepted immutable locks")
    root = Path(__file__).resolve().parents[1]
    for validator, value in ((root / "contrib/devtools/roots-adaptation-manifest.py", manifest), (root / "contrib/devtools/roots-methodology.py", methodology)):
        if subprocess.run([sys.executable, str(validator), str(value)], check=False, capture_output=True).returncode:
            raise GateError("trusted locked-material validator rejected input")
    read_json(manifest, "adaptation manifest")
    methodology_value = read_json(methodology, "methodology")
    if not isinstance(methodology_value.get("digest"), str):
        raise GateError("methodology lock is invalid")
    if digest(manifest) != LOCKED_DIGESTS["manifest"] or digest(methodology) != LOCKED_DIGESTS["methodology"]:
        raise GateError("accepted material digest differs")
    return {
        "schema_version": 1,
        "mode": mode,
        "decision": decision(mode, changed, manual_run),
        "later_base": later_base,
        "locks": {"v29.3": versions["v29.3"], "v29.4": versions["v29.4"]},
        "manifest_digest": digest(manifest),
        "methodology_digest": digest(methodology),
        "trust": "scheduled-or-manual-read-only",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", required=True)
    parser.add_argument("--changed", required=True)
    parser.add_argument("--later-base", required=True)
    parser.add_argument("--manual-run", choices=("auto", "force"), default="auto")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--methodology", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        value = report(args.mode, args.changed, args.later_base, args.manifest, args.methodology, args.fixture, args.manual_run)
        payload = json.dumps(value, sort_keys=True, separators=(",", ":")).encode() + b"\n"
        if len(payload) > MAX_REPORT_BYTES or args.output.name != "trusted-replay-report.json" or not args.output.parent.is_dir() or args.output.exists() or args.output.is_symlink():
            raise GateError("bounded report output is invalid")
        args.output.write_bytes(payload)
        print(value["decision"])
    except (GateError, OSError) as error:
        print(f"roots-trusted-replay-gate: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
