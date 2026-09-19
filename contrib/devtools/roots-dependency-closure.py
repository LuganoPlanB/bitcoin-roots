#!/usr/bin/env python3
"""Build and verify source-locked Roots dependency-closure evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


SOURCE = "e2387f0d975121869064e55eb0afb99e7639120b"
OBSOLETE_PATHS = (
    "ci/test/test_roots_hardening_handoff.py",
    "contrib/roots/replay-29.4-proposal/incremental-oracle.bash",
)
OBSOLETE_METHODS = (
    "ci.test.test_roots_llm_contract.RootsLlmContractTest._historical_two_isolated_clean_repositories_produce_identical_evidence",
    "ci.test.test_roots_maintainer_runbook.MaintainerRunbookTest._historical_two_fresh_clones_execute_complete_deterministic_rehearsal",
    "ci.test.test_roots_lineage.RootsLineageTest._historical_checked_in_ledger_fork_start_matches_non_ancestral_object",
    "ci.test.test_roots_replay.RootsReplayTest._historical_l7_incremental_comparison_accounts_for_every_difference",
    "ci.test.test_roots_replay.RootsReplayTest._historical_l7_core_knots_material_is_locked_and_complete",
    "ci.test.test_roots_replay.RootsReplayTest._historical_release_specific_29_3_calibration_reconstructs_roots_release",
)


class ClosureError(ValueError):
    pass


def git(repository: Path, *args: str) -> bytes:
    result = subprocess.run(["git", "-C", str(repository), *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode:
        raise ClosureError("required source-locked Git object is unavailable")
    return result.stdout


def canonical(value: dict) -> bytes:
    return json.dumps(value, indent=2, sort_keys=True).encode() + b"\n"


def owner(path: str) -> str:
    if path.startswith("ci/test/"):
        return "ci-roots"
    if path.startswith("contrib/devtools/"):
        return "roots-tooling"
    if path.startswith("contrib/roots/"):
        return "roots-records"
    return "release-infrastructure"


def expected(repository: Path, manifest_path: Path) -> dict:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    paths = manifest.get("handler", {}).get("paths")
    overlay = manifest.get("handler", {}).get("overlay", {})
    adapted = overlay.get("paths")
    if not isinstance(paths, list) or paths != sorted(set(paths)) or len(paths) != 153:
        raise ClosureError("manifest closure is not the exact 153-path lock")
    if not isinstance(adapted, list) or adapted != sorted(set(adapted)) or any(path not in paths for path in adapted):
        raise ClosureError("manifest adapted overlay paths are not exact")
    entries = []
    for path in sorted(set(paths) | set(OBSOLETE_PATHS)):
        if not isinstance(path, str) or not path or path.startswith("/") or ".." in path.split("/"):
            raise ClosureError("manifest contains unsafe closure path")
        line = git(repository, "ls-tree", SOURCE, "--", path).decode().strip()
        if not line:
            if path not in adapted or not isinstance(overlay.get("result_tree"), str) or not overlay["result_tree"].startswith("sha1:"):
                raise ClosureError("closure path is absent from source")
            final = overlay["result_tree"][5:]
            final_line = git(repository, "ls-tree", final, "--", path).decode().strip()
            if not final_line:
                raise ClosureError("overlay-only closure path is absent from final tree")
            mode, kind, blob_and_path = final_line.split(None, 2)
            blob, final_path = blob_and_path.split("\t", 1)
            if mode not in {"100644", "100755"} or kind != "blob" or final_path != path:
                raise ClosureError("overlay-only closure entry is not a regular exact blob")
            payload = git(repository, "show", final + ":" + path)
            source_blob = None
            provenance = "overlay:" + overlay["patch_sha256"]
        else:
            mode, kind, blob_and_path = line.split(None, 2)
            blob, source_path = blob_and_path.split("\t", 1)
            if mode not in {"100644", "100755"} or kind != "blob" or source_path != path:
                raise ClosureError("closure source entry is not a regular exact blob")
            payload = git(repository, "show", SOURCE + ":" + path)
            source_blob = "sha1:" + blob
            provenance = "sha1:" + SOURCE
        disposition = "obsolete" if path in OBSOLETE_PATHS else ("adapted" if path in adapted else "retained")
        entries.append({
            "adaptation_digest": overlay["patch_sha256"] if disposition == "adapted" else None,
            "dependencies": [],
            "disposition": disposition,
            "owner": owner(path),
            "path": path,
            "provenance": provenance,
            "sha256": "sha256:" + hashlib.sha256(payload).hexdigest(),
            "source_blob": source_blob,
        })
    return {
        "closure": entries,
        "manifest_sha256": "sha256:" + hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
        "module_matrix": {
            "excluded": [{"module": OBSOLETE_PATHS[0], "rationale": "exclusive obsolete proposal/Org and remote-ref rehearsal"}],
            "excluded_methods": [
                {"method": method, "rationale": "requires non-ancestral historical reconstruction object"}
                for method in OBSOLETE_METHODS
            ],
            "retained_glob": "ci/test/test_roots_*.py",
        },
        "schema_version": 1,
        "source_commit": "sha1:" + SOURCE,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("build", "verify"))
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--replace", action="store_true")
    args = parser.parse_args()
    try:
        value = expected(args.repository, args.manifest)
        payload = canonical(value)
        if args.command == "build":
            if args.output.exists() and not args.replace:
                raise ClosureError("refusing to overwrite closure evidence")
            args.output.write_bytes(payload)
        elif args.output.read_bytes() != payload:
            raise ClosureError("closure evidence differs from source-locked manifest")
        print(json.dumps({"decision": "accepted", "paths": len(value["closure"])}, sort_keys=True))
        return 0
    except (ClosureError, OSError, json.JSONDecodeError) as error:
        print("roots-dependency-closure: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
