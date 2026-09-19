#!/usr/bin/env python3
"""Require an authorized, exact Roots 29.4 production source without mutation."""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
CONTRACT_TOOL = ROOT / "contrib/devtools/roots-promotion-contract.py"
spec = importlib.util.spec_from_file_location("roots_promotion_contract", CONTRACT_TOOL)
CONTRACT = importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(CONTRACT)


def locked_json(path: Path, name: str) -> dict:
    if not path.is_file() or path.is_symlink():
        raise CONTRACT.ContractError(f"{name} is unavailable")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise CONTRACT.ContractError(f"{name} is invalid") from error
    if not isinstance(value, dict):
        raise CONTRACT.ContractError(f"{name} is invalid")
    return value


def validate(contract: Path, repository: Path, expected_commit: str, tag: str = "v29.4-roots.1") -> None:
    record = CONTRACT.validate(contract, repository, require_unauthorized=False)
    if record["authorization"] is not True:
        raise CONTRACT.ContractError("promotion record is not authorized")
    expected = record["production"]["head"].removeprefix("sha1:")
    if expected_commit != expected:
        raise CONTRACT.ContractError("workflow expected commit differs from production record")
    if CONTRACT.git(repository, "rev-parse", "HEAD^{commit}") != expected:
        raise CONTRACT.ContractError("checked-out source differs from production record")
    if tag != "v29.4-roots.1":
        raise CONTRACT.ContractError("release tag differs from the locked Roots version")
    cmake = repository / "CMakeLists.txt"
    notes = repository / "doc/release-notes.md"
    if not cmake.is_file() or not notes.is_file() or re.search(r"set\(CLIENT_VERSION_MAJOR 29\).*set\(CLIENT_VERSION_MINOR 4\)", cmake.read_text(encoding="utf-8"), re.DOTALL) is None:
        raise CONTRACT.ContractError("client version metadata is not Roots 29.4")
    if "29.4" not in notes.read_text(encoding="utf-8"):
        raise CONTRACT.ContractError("Roots 29.4 release notes are unavailable")
    evidence = locked_json(repository / "contrib/roots/replay-29.4-proposal/acceptance-evidence.json", "candidate evidence")
    if evidence.get("candidate", {}).get("tree") != record["candidate"]["tree"]:
        raise CONTRACT.ContractError("candidate evidence differs from the promotion record")
    accounting = locked_json(repository / "contrib/roots/release-accounting.json", "production accounting")
    if accounting.get("schema_version") != 2 or not isinstance(accounting.get("changes"), list):
        raise CONTRACT.ContractError("production accounting is invalid")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--contract", type=Path, required=True)
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--tag", required=True)
    args = parser.parse_args()
    try:
        validate(args.contract, args.repository, args.expected_commit, args.tag)
    except CONTRACT.ContractError as error:
        print(f"validate-promotion-source: {error}", file=sys.stderr)
        return 1
    print("promotion source: valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
