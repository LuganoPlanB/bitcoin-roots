#!/usr/bin/env python3
"""Validate the closed, path-free local Roots 29.4 baseline evidence."""
import json
import sys
from pathlib import Path

EXPECTED = {"core_commit":"3fc0865963a38b871e9f7d94e6151c4953563516","core_tag_object":"4e70eab99b60f7718b78e2158de9fb82726f3cec","canonical_commit":"cbc88cff9b35b95a549c0313e424e13093fcd6a1","tree":"39a5e30207a09962e78ae81c24cc65b1e478ef90","commit_count":16,"integration_ref":"refs/heads/integration/roots-29.4","mbox_sha256":"be1c2657f74f0da7792ba0d02a5c623ec4e97e633bb0a5d8da77dd5672595725","apply_mode":"git am --3way --keep-cr","round_trip_tree":"39a5e30207a09962e78ae81c24cc65b1e478ef90","round_trip_clean":True,"status":"pass"}

def fail(message):
    print(f"invalid local baseline evidence: {message}", file=sys.stderr)
    raise SystemExit(1)

def safe(value):
    if isinstance(value, str): return not value.startswith("/")
    if isinstance(value, dict): return all(safe(item) for item in value.values())
    if isinstance(value, list): return all(safe(item) for item in value)
    return True

def main():
    if len(sys.argv) != 2: fail("exactly one record path is required")
    path = Path(sys.argv[1])
    if path.is_symlink() or not path.is_file(): fail("record must be a regular non-symlink file")
    try: record = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError): fail("record is not readable JSON")
    if not isinstance(record, dict) or set(record) != set(EXPECTED) | {"schema_version", "export_flags"}: fail("closed schema mismatch")
    if record["schema_version"] != 1: fail("schema version mismatch")
    if record["export_flags"] != ["--no-renames", "--binary", "--full-index"]: fail("export flags mismatch")
    for key, expected in EXPECTED.items():
        if record[key] != expected: fail(f"locked field mismatch: {key}")
    if not safe(record): fail("absolute path is forbidden")

if __name__ == "__main__": main()
