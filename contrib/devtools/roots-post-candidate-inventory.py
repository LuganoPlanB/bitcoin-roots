#!/usr/bin/env python3
"""Build and verify the locked Roots 29.4 post-candidate atom inventory.

The inventory is intentionally derived from immutable Git objects, rather than
from a checkout or a branch name.  It is the hand-off between the historic
post-candidate work and the private Core-rooted integration line: L4.2 may
only retain atoms that this record has classified.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any


BASE_COMMIT = "dd3050a6101a071a0b642ba71ab7eaddbe4cf5b8"
HEAD_COMMIT = "e2387f0d975121869064e55eb0afb99e7639120b"
CANDIDATE_COMMIT = "cbc88cff9b35b95a549c0313e424e13093fcd6a1"
CANDIDATE_TREE = "39a5e30207a09962e78ae81c24cc65b1e478ef90"
SHA1_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^sha256:[0-9a-f]{64}$")
ID_RE = re.compile(r"^roots-post-candidate-[a-z0-9-]+-v1$")
DISPOSITIONS = frozenset({"unchanged", "adapted", "absorbed", "obsolete", "generated", "manual"})
RISKS = frozenset({"low", "medium", "high"})
CRITICAL_PREFIXES = ("src/", "depends/", "cmake/")
OBSOLETE_PREFIXES = (
    "contrib/roots/replay-29.3/",
    "contrib/roots/replay-29.3-release/",
    "contrib/roots/replay-29.4-proposal/",
    "contrib/roots/replay-core-to-knots-29.3/",
)

# These are deliberate, reviewable adaptation boundaries, not commit-message
# parsing.  Dependencies model the required replay order.
UNIT_SPECS = (
    ("ce80aede608ab49c33487f310e6b1d3ecd1868e0", "replay-engine", "high"),
    ("7c58ceb2e1a91933275887b78225d17ef4197846", "semantic-safety", "high"),
    ("392ff915b4eee5b76ebbab754678b4660d80b71f", "replay-calibration", "high"),
    ("6dad2da4c7650dce743606808dc6eba45390aaf8", "portability-methodology", "medium"),
    ("fd2b07ebefca9630cd5eeaf57a748a5b1c3c0bce", "portability-roadmap", "low"),
    ("baabfd79950f64c5d2769bd3af1f7eeda4ac1db4", "portability-operations", "high"),
    ("4d06f486d00cd60afec346b60d40f582835fe60c", "evidence-hardening", "high"),
    ("0a3408b4fe6ab68be092ae11d79d69a4b7131d3f", "portability-capacity", "medium"),
    ("affdbca9ffcf54ddc105a83a28f65ff702bd6e7d", "lint-gates", "medium"),
    ("6f85dfb728ca0976658e0fbd26101e7df5e35a14", "maintenance-typecheck", "medium"),
    ("2d19698c568f4c1162149059b86b95b6e59fa71b", "lint-provenance", "medium"),
    ("dd2e2d0e51ccd9f9b2f21eee68f4e93b4eef668a", "signal-focused-gates", "high"),
    ("af0c72997c9c108d5cb35f418b54cbd6e6fdcbb7", "signal-accounting", "medium"),
    ("4f715d1d7242796c370eeac32580e4399ef1d817", "rust-baseline-support", "medium"),
    ("e2387f0d975121869064e55eb0afb99e7639120b", "rust-baseline-accounting", "medium"),
)


class InventoryError(ValueError):
    """A deterministic inventory precondition or integrity check failed."""


def canonical(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True).encode("utf-8") + b"\n"


def digest_bytes(value: bytes) -> str:
    return "sha256:" + hashlib.sha256(value).hexdigest()


def run_git(repository: Path, *arguments: str, input_data: bytes | None = None) -> bytes:
    result = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        input=input_data,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode:
        raise InventoryError("git command failed: " + " ".join(arguments))
    return result.stdout


def git_text(repository: Path, *arguments: str) -> str:
    return run_git(repository, *arguments).decode("utf-8", "strict").strip()


def require_commit(repository: Path, value: str, expected: str) -> None:
    if value != expected or not SHA1_RE.fullmatch(value):
        raise InventoryError("endpoint must be its locked full commit identity")
    if git_text(repository, "rev-parse", "--verify", value + "^{commit}") != value:
        raise InventoryError("locked endpoint is unavailable")


def blob_digest(repository: Path, blob: str) -> str | None:
    if blob == "0" * 40:
        return None
    return digest_bytes(run_git(repository, "cat-file", "blob", blob))


def unit_id(slug: str) -> str:
    return "roots-post-candidate-" + slug + "-v1"


def is_obsolete(path: str) -> bool:
    return path.startswith(OBSOLETE_PREFIXES)


def build_inventory(repository: Path, base: str, head: str, candidate: str) -> dict[str, Any]:
    require_commit(repository, base, BASE_COMMIT)
    require_commit(repository, head, HEAD_COMMIT)
    require_commit(repository, candidate, CANDIDATE_COMMIT)
    if git_text(repository, "rev-parse", candidate + "^{tree}") != CANDIDATE_TREE:
        raise InventoryError("candidate tree differs from the accepted 29.4 tree")
    commits = git_text(repository, "rev-list", "--reverse", "--first-parent", base + ".." + head).splitlines()
    expected_commits = [commit for commit, _, _ in UNIT_SPECS]
    if commits != expected_commits or len(commits) != 15:
        raise InventoryError("post-candidate range is not the exact fifteen-commit lineage")
    if git_text(repository, "merge-base", "--is-ancestor", base, head) != "":
        # git returns an empty successful stdout for this predicate.
        raise InventoryError("unexpected merge-base predicate output")

    by_commit = {commit: (slug, risk) for commit, slug, risk in UNIT_SPECS}
    units: list[dict[str, Any]] = []
    atoms: list[dict[str, Any]] = []
    previous: str | None = None
    for index, commit in enumerate(commits, start=1):
        slug, risk = by_commit[commit]
        adaptation_id = unit_id(slug)
        raw = git_text(repository, "show", "-s", "--format=%P%x00%T%x00%s", commit).split("\x00")
        if len(raw) != 3 or raw[0] != (base if index == 1 else commits[index - 2]):
            raise InventoryError("post-candidate history must be one linear first-parent chain")
        rows = git_text(repository, "diff-tree", "--no-commit-id", "--raw", "-r", "--no-renames", commit).splitlines()
        unit_atoms: list[str] = []
        retained_paths: list[str] = []
        obsolete_paths: list[str] = []
        for row in rows:
            metadata, path = row.split("\t", 1)
            fields = metadata.split()
            if len(fields) != 5 or not fields[0].startswith(":"):
                raise InventoryError("unexpected raw atom encoding")
            old_mode, new_mode = fields[0][1:], fields[1]
            old_blob, new_blob, status = fields[2], fields[3], fields[4]
            if not SHA1_RE.fullmatch(old_blob) or not SHA1_RE.fullmatch(new_blob):
                raise InventoryError("atom blob identity is not a full SHA-1")
            disposition = "obsolete" if is_obsolete(path) else "adapted"
            if disposition == "obsolete":
                obsolete_paths.append(path)
            else:
                retained_paths.append(path)
            atom_id = commit + ":" + path
            unit_atoms.append(atom_id)
            atoms.append(
                {
                    "adaptation_id": None if disposition == "obsolete" else adaptation_id,
                    "atom_id": atom_id,
                    "commit": "sha1:" + commit,
                    "disposition": disposition,
                    "new_blob": "sha1:" + new_blob,
                    "new_blob_sha256": blob_digest(repository, new_blob),
                    "new_mode": new_mode,
                    "old_blob": "sha1:" + old_blob,
                    "old_blob_sha256": blob_digest(repository, old_blob),
                    "old_mode": old_mode,
                    "path": path,
                    "risk": "high" if path.startswith(CRITICAL_PREFIXES) else risk,
                    "status": status,
                }
            )
        if not unit_atoms:
            raise InventoryError("empty post-candidate commit")
        units.append(
            {
                "adaptation_id": adaptation_id,
                "commit": "sha1:" + commit,
                "dependencies": [] if previous is None else [previous],
                "obsolete_paths": sorted(obsolete_paths),
                "paths": sorted(retained_paths),
                "provenance": {"project": "bitcoin-roots", "commit": "sha1:" + commit, "confidence": "verified"},
                "required_tests": ["python3 -m unittest ci.test.test_roots_post_candidate_inventory"],
                "risk": risk,
                "subject": raw[2],
            }
        )
        previous = adaptation_id
    result = {
        "atoms": atoms,
        "base_commit": "sha1:" + base,
        "candidate_commit": "sha1:" + candidate,
        "candidate_tree": "sha1:" + CANDIDATE_TREE,
        "head_commit": "sha1:" + head,
        "schema_version": 1,
        "units": units,
    }
    validate_inventory(result)
    result["inventory_sha256"] = digest_bytes(canonical(result))
    return result


def validate_inventory(value: Any) -> None:
    if not isinstance(value, dict) or set(value) not in ({"atoms", "base_commit", "candidate_commit", "candidate_tree", "head_commit", "schema_version", "units"}, {"atoms", "base_commit", "candidate_commit", "candidate_tree", "head_commit", "inventory_sha256", "schema_version", "units"}):
        raise InventoryError("inventory schema is not exact")
    if value.get("schema_version") != 1:
        raise InventoryError("unsupported inventory schema")
    expected = {
        "base_commit": "sha1:" + BASE_COMMIT,
        "head_commit": "sha1:" + HEAD_COMMIT,
        "candidate_commit": "sha1:" + CANDIDATE_COMMIT,
        "candidate_tree": "sha1:" + CANDIDATE_TREE,
    }
    if any(value.get(key) != item for key, item in expected.items()):
        raise InventoryError("inventory endpoints are mutable or stale")
    atoms, units = value.get("atoms"), value.get("units")
    if not isinstance(atoms, list) or not isinstance(units, list) or len(units) != 15 or not atoms:
        raise InventoryError("inventory must contain fifteen non-empty units")
    ids = [unit.get("adaptation_id") for unit in units if isinstance(unit, dict)]
    if len(ids) != len(units) or len(set(ids)) != len(ids) or any(not isinstance(item, str) or not ID_RE.fullmatch(item) for item in ids):
        raise InventoryError("adaptation IDs are incomplete or duplicated")
    atom_ids: set[str] = set()
    owned_atoms: set[str] = set()
    for atom in atoms:
        if not isinstance(atom, dict) or set(atom) != {"adaptation_id", "atom_id", "commit", "disposition", "new_blob", "new_blob_sha256", "new_mode", "old_blob", "old_blob_sha256", "old_mode", "path", "risk", "status"}:
            raise InventoryError("atom schema is not exact")
        atom_id, path = atom["atom_id"], atom["path"]
        if not isinstance(atom_id, str) or atom_id in atom_ids or not isinstance(path, str) or not path or path.startswith("/") or ".." in path.split("/"):
            raise InventoryError("atom ownership is duplicated or unsafe")
        atom_ids.add(atom_id)
        if atom["disposition"] not in DISPOSITIONS or atom["risk"] not in RISKS:
            raise InventoryError("atom has an invalid disposition or risk")
        if not all(isinstance(atom[key], str) and atom[key].startswith("sha1:") and SHA1_RE.fullmatch(atom[key][5:]) for key in ("commit", "old_blob", "new_blob")):
            raise InventoryError("atom blob provenance is invalid")
        if not all(atom[key] is None or (isinstance(atom[key], str) and SHA256_RE.fullmatch(atom[key])) for key in ("old_blob_sha256", "new_blob_sha256")):
            raise InventoryError("atom blob digest is invalid")
        if atom["disposition"] == "obsolete":
            if atom["adaptation_id"] is not None:
                raise InventoryError("obsolete atom cannot retain an adaptation owner")
        else:
            if atom["adaptation_id"] not in ids:
                raise InventoryError("retained atom has no declared adaptation owner")
            owned_atoms.add(atom_id)
        if path.startswith(CRITICAL_PREFIXES) and not (atom["disposition"] == "manual" and atom["risk"] == "high"):
            raise InventoryError("critical runtime atom must be manual high-risk")
    if not owned_atoms:
        raise InventoryError("inventory has no retained atoms")
    previous: str | None = None
    for unit in units:
        if not isinstance(unit, dict) or set(unit) != {"adaptation_id", "commit", "dependencies", "obsolete_paths", "paths", "provenance", "required_tests", "risk", "subject"}:
            raise InventoryError("unit schema is not exact")
        if unit["risk"] not in RISKS or not isinstance(unit["paths"], list) or not isinstance(unit["obsolete_paths"], list):
            raise InventoryError("unit classification is invalid")
        if unit["dependencies"] != ([] if previous is None else [previous]):
            raise InventoryError("unit dependencies are unordered or incomplete")
        if sorted(set(unit["paths"])) != unit["paths"] or sorted(set(unit["obsolete_paths"])) != unit["obsolete_paths"]:
            raise InventoryError("unit paths are duplicated or unordered")
        previous = unit["adaptation_id"]
    if "inventory_sha256" in value:
        without_digest = dict(value)
        recorded = without_digest.pop("inventory_sha256")
        if not isinstance(recorded, str) or recorded != digest_bytes(canonical(without_digest)):
            raise InventoryError("inventory digest does not bind the record")


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8", errors="strict"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise InventoryError("cannot read inventory JSON") from error
    validate_inventory(value)
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("build", "verify"))
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument("--base", default=BASE_COMMIT)
    parser.add_argument("--head", default=HEAD_COMMIT)
    parser.add_argument("--candidate", default=CANDIDATE_COMMIT)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        expected = build_inventory(arguments.repository.resolve(), arguments.base, arguments.head, arguments.candidate)
        if arguments.command == "verify":
            actual = read_json(arguments.output)
            if canonical(actual) != canonical(expected):
                raise InventoryError("inventory does not exactly match locked Git atoms")
        else:
            if arguments.output.exists():
                raise InventoryError("refusing to overwrite inventory output")
            arguments.output.write_bytes(canonical(expected))
        print(json.dumps({"atoms": len(expected["atoms"]), "decision": "accepted", "inventory_sha256": expected["inventory_sha256"], "units": len(expected["units"])}, sort_keys=True))
        return 0
    except InventoryError as error:
        print("roots-post-candidate-inventory: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
