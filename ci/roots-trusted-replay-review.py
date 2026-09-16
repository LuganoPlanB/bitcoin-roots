#!/usr/bin/env python3
"""Verify replay CI artifacts against the frozen methodology, never producer data."""
import argparse, hashlib, json
from pathlib import Path

NAMES = {"state":"replay-state.json", "report_json":"replay-review.json", "report_text":"replay-review.txt", "generated_export":"replay-generated-series.patch"}
CAPS = {"replay-state.json": 2_000_000, "replay-review.json": 262_144, "replay-review.txt": 262_144, "replay-generated-series.patch": 10_000_000}
MAX_TOTAL_BYTES = 36_000_000

def sha(path): return "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()

def verify(methodology, artifacts):
    method=json.loads(methodology.read_text())
    total = 0
    for role, expected in method["reconstructions"].items():
        expected_tree=expected["target_tree"].split(":",1)[1]
        for run in ("a","b"):
            directory=artifacts/role/run
            if not directory.is_dir() or directory.is_symlink() or {p.name for p in directory.iterdir()} != set(NAMES.values()): raise ValueError("artifact layout is invalid")
            if any(not p.is_file() or p.is_symlink() or p.stat().st_size > CAPS[p.name] for p in directory.iterdir()): raise ValueError("artifact bounds are invalid")
            total += sum(p.stat().st_size for p in directory.iterdir())
            state=json.loads((directory/NAMES["state"]).read_text())
            if state.get("candidate_tree") != expected_tree: raise ValueError("candidate tree differs")
            outcomes="sha256:"+hashlib.sha256(json.dumps(state.get("completed_units"),sort_keys=True,separators=(",",":")).encode()).hexdigest()
            if outcomes != expected["artifact_digests"]["outcome_set"]: raise ValueError("outcome set differs")
            for kind,name in NAMES.items():
                if sha(directory/name) != expected["artifact_digests"][kind]: raise ValueError("frozen artifact digest differs")
        if any((artifacts/role/"a"/name).read_bytes() != (artifacts/role/"b"/name).read_bytes() for name in NAMES.values()): raise ValueError("clean runs differ")
    if total > MAX_TOTAL_BYTES: raise ValueError("artifact aggregate is too large")

if __name__ == "__main__":
    p=argparse.ArgumentParser(); p.add_argument("--methodology",type=Path,required=True); p.add_argument("--artifacts",type=Path,required=True); a=p.parse_args()
    try: verify(a.methodology,a.artifacts)
    except (OSError,ValueError,json.JSONDecodeError) as e: raise SystemExit(f"roots-trusted-replay-review: {e}")
