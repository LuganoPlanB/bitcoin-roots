#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Validate and regenerate the Bitcoin Roots immutable lineage ledger.

The command deliberately never fetches.  A caller must explicitly prepare each
repository and pass it with --repository; this makes a changed remote tag an
observable validation failure instead of an implicit network side effect.
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Any
from urllib.parse import urlsplit


OID_RE = re.compile(r"^(sha1:[0-9a-f]{40}|sha256:[0-9a-f]{64})$")
TAG_RE = re.compile(r"^v[0-9][A-Za-z0-9.+_-]*$")
ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]*$")
LEVELS = {"tag_object", "commit", "complete_tree", "maintained_source",
          "generated_release_artifact", "build_input", "reproducible_binary",
          "consensus", "policy", "api_config"}
STATES = {"core_base", "knots_layer", "roots_layer", "absorbed_upstream",
          "obsolete", "rejected", "manual"}
MAX_LEDGER_BYTES = 1_000_000
MAX_JSON_DEPTH = 32
MAX_JSON_STRING = 16_384


class LedgerError(ValueError):
    pass


def unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise LedgerError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def validate_json_limits(value: Any, depth: int = 0) -> None:
    if depth > MAX_JSON_DEPTH:
        raise LedgerError("JSON nesting exceeds the ledger limit")
    if isinstance(value, str):
        if len(value) > MAX_JSON_STRING:
            raise LedgerError("JSON string exceeds the ledger limit")
    elif isinstance(value, dict):
        for key, child in value.items():
            validate_json_limits(key, depth + 1)
            validate_json_limits(child, depth + 1)
    elif isinstance(value, list):
        for child in value:
            validate_json_limits(child, depth + 1)


def load_json(path: Path) -> dict[str, Any]:
    try:
        if path.stat().st_size > MAX_LEDGER_BYTES:
            raise LedgerError("ledger exceeds the byte limit")
        value = json.loads(path.read_text(encoding="utf-8", errors="strict"), object_pairs_hook=unique_object)
        validate_json_limits(value)
    except LedgerError:
        raise
    except OSError as error:
        raise LedgerError("cannot read ledger JSON") from error
    except (UnicodeDecodeError, json.JSONDecodeError, RecursionError) as error:
        raise LedgerError("invalid ledger JSON") from error
    if not isinstance(value, dict):
        raise LedgerError("ledger must be a JSON object")
    return value


def oid(value: Any, object_format: str, field: str) -> None:
    if not isinstance(value, str) or not OID_RE.fullmatch(value):
        raise LedgerError(f"{field} must be an algorithm-qualified object ID")
    if not value.startswith(f"{object_format}:"):
        raise LedgerError(f"{field} uses a different object format")


def safe_text(value: Any, field: str) -> None:
    if not isinstance(value, str) or "\x00" in value:
        raise LedgerError(f"{field} must be a non-NUL string")
    if any(0xD800 <= ord(character) <= 0xDFFF for character in value):
        raise LedgerError(f"{field} contains malformed Unicode")
    if os.path.isabs(value):
        raise LedgerError(f"{field} must not be an absolute path")


def canonical_https_repository(value: Any, field: str) -> None:
    if not isinstance(value, str):
        raise LedgerError(f"{field} must be a canonical HTTPS Git URL")
    parsed = urlsplit(value)
    path_parts = parsed.path.split("/")[1:]
    try:
        port = parsed.port
    except ValueError as error:
        raise LedgerError(f"{field} must be a canonical HTTPS Git URL") from error
    if (parsed.scheme != "https" or parsed.username is not None or parsed.password is not None
            or port is not None or not parsed.hostname or parsed.hostname != parsed.hostname.lower()
            or parsed.query or parsed.fragment or not parsed.path.endswith(".git")
            or "//" in parsed.path or any(part in {"", ".", ".."} for part in path_parts)):
        raise LedgerError(f"{field} must be a canonical HTTPS Git URL")


def reject_timestamps(value: Any, path: str = "ledger") -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            if key.lower() in {"date", "timestamp", "retrieved_at", "generated_at", "updated_at"}:
                raise LedgerError(f"{path}.{key} is a non-deterministic timestamp")
            reject_timestamps(child, f"{path}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            reject_timestamps(child, f"{path}[{index}]")


def validate(ledger: dict[str, Any]) -> None:
    if set(ledger) != {"schema_version", "object_format", "releases", "fork_starts", "claims", "exceptions"}:
        raise LedgerError("ledger has unknown or missing top-level fields")
    if ledger["schema_version"] != 1:
        raise LedgerError("unsupported schema_version")
    object_format = ledger["object_format"]
    if object_format not in {"sha1", "sha256"}:
        raise LedgerError("unsupported object_format")
    reject_timestamps(ledger)
    releases = ledger["releases"]
    fork_starts = ledger["fork_starts"]
    claims = ledger["claims"]
    exceptions = ledger["exceptions"]
    if not all(isinstance(group, list) for group in (releases, fork_starts, claims, exceptions)):
        raise LedgerError("releases, fork_starts, claims, and exceptions must be arrays")

    fork_ids: set[str] = set()
    for fork_start in fork_starts:
        required = {"id", "project", "commit", "parent", "tree", "relation", "scope", "command", "path_count", "status_counts", "evidence_digest"}
        if not isinstance(fork_start, dict) or set(fork_start) != required:
            raise LedgerError("fork start has unknown or missing fields")
        if not isinstance(fork_start["id"], str) or not ID_RE.fullmatch(fork_start["id"]) or fork_start["id"] in fork_ids:
            raise LedgerError("duplicate or malformed fork start identity")
        fork_ids.add(fork_start["id"])
        if fork_start["project"] != "roots" or fork_start["relation"] != "direct-parent":
            raise LedgerError(f"{fork_start['id']}: invalid fork start relation")
        for field in ("commit", "parent", "tree"):
            oid(fork_start[field], object_format, f"{fork_start['id']}.{field}")
        if fork_start["scope"] != "raw Git tree entries and blob/mode changes; no release identity or equivalence claim":
            raise LedgerError(f"{fork_start['id']}: invalid fork boundary scope")
        if not isinstance(fork_start["command"], list) or not fork_start["command"] or not all(isinstance(item, str) and item for item in fork_start["command"]):
            raise LedgerError(f"{fork_start['id']}: fork boundary command must be argv")
        if not isinstance(fork_start["path_count"], int) or fork_start["path_count"] < 0:
            raise LedgerError(f"{fork_start['id']}: invalid fork boundary path count")
        if not isinstance(fork_start["status_counts"], dict) or set(fork_start["status_counts"]) != {"added", "deleted", "modified"} or any(not isinstance(count, int) or count < 0 for count in fork_start["status_counts"].values()) or sum(fork_start["status_counts"].values()) != fork_start["path_count"]:
            raise LedgerError(f"{fork_start['id']}: invalid fork boundary status counts")
        oid(fork_start["evidence_digest"], object_format, f"{fork_start['id']}.evidence_digest")

    release_ids: set[str] = set()
    dependency_graph: dict[str, list[str]] = {}
    for release in releases:
        allowed = {"id", "project", "release_label", "repository", "tag", "tag_ref", "resolution_status", "retrieval", "refspec", "object_format", "depends_on", "tag_object", "peeled_commit", "tree", "signature", "trust_status", "trusted_key_fingerprint", "parents"}
        if not isinstance(release, dict) or set(release) - allowed:
            raise LedgerError("release has unknown fields")
        for field in ("id", "project", "release_label", "repository", "tag", "tag_ref", "resolution_status", "retrieval", "refspec"):
            if field not in release:
                raise LedgerError(f"release missing {field}")
        release_id = release["id"]
        if not isinstance(release_id, str) or not ID_RE.fullmatch(release_id) or release_id in release_ids:
            raise LedgerError("duplicate or malformed release identity")
        release_ids.add(release_id)
        if release["project"] not in {"core", "knots", "roots"}:
            raise LedgerError(f"{release_id}: unknown project")
        if not isinstance(release["release_label"], str) or not re.fullmatch(r"[0-9][A-Za-z0-9.+_-]*", release["release_label"]):
            raise LedgerError(f"{release_id}: malformed release label")
        if release_id != f"{release['project']}-{release['release_label']}":
            raise LedgerError(f"{release_id}: identity must bind project and release label")
        canonical_https_repository(release["repository"], f"{release_id}.repository")
        if not isinstance(release["tag"], str) or not TAG_RE.fullmatch(release["tag"]):
            raise LedgerError(f"{release_id}: mutable or malformed tag")
        if release["tag"] != f"v{release['release_label']}":
            raise LedgerError(f"{release_id}: tag must bind its release label")
        expected_tag_ref = f"refs/tags/{release['tag']}"
        if release["tag_ref"] != expected_tag_ref:
            raise LedgerError(f"{release_id}: tag_ref must name only its exact tag")
        expected_refspec = f"{expected_tag_ref}:{expected_tag_ref}"
        if release["refspec"] != expected_refspec:
            raise LedgerError(f"{release_id}: refspec must fetch only its exact tag")
        if release["retrieval"] not in {"explicit-fetch-only", "local-fixture"}:
            raise LedgerError(f"{release_id}: implicit retrieval is forbidden")
        if release.get("object_format", object_format) != object_format:
            raise LedgerError(f"{release_id}: object format mismatch")
        status = release["resolution_status"]
        if status not in {"resolved", "unavailable"}:
            raise LedgerError(f"{release_id}: invalid resolution status")
        identity_fields = ("tag_object", "peeled_commit", "tree")
        if status == "resolved":
            for field in identity_fields:
                if field not in release:
                    raise LedgerError(f"{release_id}: resolved release lacks {field}")
                oid(release[field], object_format, f"{release_id}.{field}")
        elif any(field in release for field in identity_fields):
            raise LedgerError(f"{release_id}: unavailable release must not invent object IDs")
        if release.get("signature", "unverified") not in {"verified", "unsigned", "unverified", "not-available"}:
            raise LedgerError(f"{release_id}: invalid signature state")
        if release.get("trust_status", "unverified") not in {"verified", "unverified", "revoked", "expired", "not-available"}:
            raise LedgerError(f"{release_id}: invalid trust status")
        if release.get("signature") == "not-available" and release.get("trust_status") != "not-available":
            raise LedgerError(f"{release_id}: unavailable signature requires unavailable trust status")
        if release.get("trust_status") == "verified" and release.get("signature") != "verified":
            raise LedgerError(f"{release_id}: verified trust requires a verified signature")
        if release.get("signature") == "verified":
            fingerprint = release.get("trusted_key_fingerprint")
            if not isinstance(fingerprint, str) or not re.fullmatch(r"[A-F0-9]{40}", fingerprint):
                raise LedgerError(f"{release_id}: verified tag needs a trusted fingerprint")
            if release.get("trust_status") != "verified":
                raise LedgerError(f"{release_id}: verified tag requires a verified trusted key")
        parents = release.get("parents", [])
        if not isinstance(parents, list):
            raise LedgerError(f"{release_id}: malformed parents")
        for index, parent in enumerate(parents):
            oid(parent, object_format, f"{release_id}.parents[{index}]")
        dependencies = release.get("depends_on", [])
        if not isinstance(dependencies, list) or not all(isinstance(item, str) for item in dependencies):
            raise LedgerError(f"{release_id}: malformed dependencies")
        dependency_graph[release_id] = dependencies

    def visit(node: str, visiting: set[str], seen: set[str]) -> None:
        if node in visiting:
            raise LedgerError("release dependency cycle")
        if node in seen:
            return
        visiting.add(node)
        for parent in dependency_graph[node]:
            if parent not in dependency_graph:
                raise LedgerError(f"unknown release dependency: {parent}")
            visit(parent, visiting, seen)
        visiting.remove(node)
        seen.add(node)
    seen: set[str] = set()
    for node in dependency_graph:
        visit(node, set(), seen)

    claim_keys: dict[tuple[Any, ...], str] = {}
    claim_ids: set[str] = set()
    for claim in claims:
        required = {"id", "left", "right", "level", "status", "scope", "exclusions", "command", "evidence_digest"}
        if not isinstance(claim, dict) or set(claim) != required:
            raise LedgerError("claim has unknown or missing fields")
        if not isinstance(claim["id"], str) or not ID_RE.fullmatch(claim["id"]) or claim["id"] in claim_ids:
            raise LedgerError("duplicate or malformed claim identity")
        claim_ids.add(claim["id"])
        if claim["left"] not in release_ids or claim["right"] not in release_ids or claim["left"] == claim["right"]:
            raise LedgerError(f"{claim['id']}: unknown or identical claim endpoints")
        if claim["level"] not in LEVELS or claim["status"] not in {"same", "different", "blocked", "manual"}:
            raise LedgerError(f"{claim['id']}: invalid identity level or status")
        if not isinstance(claim["scope"], list) or not claim["scope"] or not all(isinstance(item, str) and item for item in claim["scope"]):
            raise LedgerError(f"{claim['id']}: equivalence claim requires scope")
        for index, item in enumerate(claim["scope"]):
            safe_text(item, f"{claim['id']}.scope[{index}]")
        if not isinstance(claim["exclusions"], list) or not all(isinstance(item, str) for item in claim["exclusions"]):
            raise LedgerError(f"{claim['id']}: malformed exclusions")
        for index, item in enumerate(claim["exclusions"]):
            safe_text(item, f"{claim['id']}.exclusions[{index}]")
        if not isinstance(claim["command"], list) or not claim["command"] or not all(isinstance(item, str) and item for item in claim["command"]):
            raise LedgerError(f"{claim['id']}: comparison command must be argv")
        for index, item in enumerate(claim["command"]):
            safe_text(item, f"{claim['id']}.command[{index}]")
        oid(claim["evidence_digest"], object_format, f"{claim['id']}.evidence_digest")
        key = (claim["left"], claim["right"], claim["level"], tuple(claim["scope"]), tuple(claim["exclusions"]))
        old = claim_keys.get(key)
        if old is not None:
            raise LedgerError(f"contradictory or duplicate identity claim: {old} and {claim['id']}")
        claim_keys[key] = claim["id"]
        if claim["status"] == "same" and any(release["id"] in (claim["left"], claim["right"]) and release["resolution_status"] != "resolved" for release in releases):
            raise LedgerError(f"{claim['id']}: cannot assert same for an unresolved release")

    exception_ids: set[str] = set()
    for exception in exceptions:
        if not isinstance(exception, dict) or set(exception) != {"id", "release", "state", "reason"}:
            raise LedgerError("exception has unknown or missing fields")
        if not isinstance(exception["id"], str) or not ID_RE.fullmatch(exception["id"]) or exception["id"] in exception_ids:
            raise LedgerError("duplicate or malformed exception identity")
        exception_ids.add(exception["id"])
        if exception["release"] not in release_ids or exception["state"] not in STATES or not isinstance(exception["reason"], str) or not exception["reason"]:
            raise LedgerError(f"{exception['id']}: malformed exception")
        safe_text(exception["reason"], f"{exception['id']}.reason")


def git(repository: Path, *args: str) -> str:
    completed = subprocess.run(["git", "-C", str(repository), *args], check=False,
                               capture_output=True, text=True, encoding="utf-8", errors="strict",
                               env={**os.environ, "LC_ALL": "C", "LANG": "C"})
    if completed.returncode:
        raise LedgerError(f"git {args[0]} failed")
    return completed.stdout.strip()


def regenerate(ledger: dict[str, Any], repositories: dict[str, Path]) -> dict[str, Any]:
    validate(ledger)
    result = json.loads(json.dumps(ledger))
    object_format = result["object_format"]
    for release in result["releases"]:
        repository = repositories.get(release["id"])
        if repository is None:
            continue
        if not repository.is_dir() or git(repository, "rev-parse", "--is-inside-work-tree") != "true":
            raise LedgerError(f"{release['id']}: repository is not a complete work tree")
        if git(repository, "rev-parse", "--is-shallow-repository") == "true":
            raise LedgerError(f"{release['id']}: shallow history cannot establish lineage")
        remote = git(repository, "config", "--get", "remote.origin.url")
        if remote != release["repository"]:
            raise LedgerError(f"{release['id']}: substituted repository")
        tag_ref = release["tag_ref"]
        tag_object = git(repository, "rev-parse", "--verify", f"{tag_ref}^{{tag}}")
        peeled = git(repository, "rev-parse", "--verify", f"{tag_ref}^{{commit}}")
        tree = git(repository, "rev-parse", "--verify", f"{tag_ref}^{{tree}}")
        prefix = f"{object_format}:"
        if release["resolution_status"] != "resolved":
            raise LedgerError(f"{release['id']}: local object contradicts unavailable status")
        expected = {"tag_object": prefix + tag_object, "peeled_commit": prefix + peeled, "tree": prefix + tree}
        for field, value in expected.items():
            if release.get(field) != value:
                raise LedgerError(f"{release['id']}: stale evidence for {field}")
    return result


def write_json(path: Path, value: dict[str, Any]) -> None:
    payload = json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    path.write_text(payload, encoding="utf-8", newline="\n")


def tree_entries(repository: Path, revision: str) -> dict[str, tuple[str, str, str]]:
    raw = subprocess.run(["git", "-C", str(repository), "ls-tree", "-r", "-z", revision],
                         check=False, capture_output=True,
                         env={**os.environ, "LC_ALL": "C", "LANG": "C"})
    if raw.returncode:
        raise LedgerError(f"cannot enumerate {revision} in {repository}")
    entries: dict[str, tuple[str, str, str]] = {}
    for record in raw.stdout.split(b"\0"):
        if not record:
            continue
        metadata, separator, encoded_path = record.partition(b"\t")
        parts = metadata.decode("ascii").split()
        if not separator or len(parts) != 3:
            raise LedgerError("malformed Git tree entry")
        path = encoded_path.decode("utf-8", "strict")
        if path.startswith("/") or "\\" in path or path in {".", ".."} or "/../" in path:
            raise LedgerError("unsafe Git tree path")
        entries[path] = (parts[0], parts[1], parts[2])
    return entries


def compare(left: Path, right: Path, left_revision: str, right_revision: str,
            level: str, exclusions: list[str]) -> dict[str, Any]:
    if level not in {"complete_tree", "maintained_source", "generated_release_artifact", "build_input"}:
        raise LedgerError(f"{level} comparison requires an explicit invariant test")
    if level != "complete_tree" and not exclusions:
        raise LedgerError(f"{level} comparison requires explicit classified exclusions")
    for item in exclusions:
        safe_text(item, "comparison exclusion")
        if item.startswith("../"):
            raise LedgerError("comparison exclusion escapes the tree")
    left_entries = tree_entries(left, left_revision)
    right_entries = tree_entries(right, right_revision)
    ignored = lambda path: any(path == item or path.startswith(item.rstrip("/") + "/") for item in exclusions)
    paths = sorted((set(left_entries) | set(right_entries)) - {path for path in set(left_entries) | set(right_entries) if ignored(path)})
    different = [path for path in paths if left_entries.get(path) != right_entries.get(path)]
    evidence = hashlib.sha1("\n".join(
        f"{path}\t{left_entries.get(path)}\t{right_entries.get(path)}" for path in paths).encode("utf-8")).hexdigest()
    return {"evidence_digest": f"sha1:{evidence}", "exclusions": sorted(exclusions),
            "level": level, "paths": different, "status": "same" if not different else "different"}


def classify_path(path: str) -> dict[str, Any]:
    """Return a conservative deterministic classification, never a behavior claim."""
    if path in {"src/init.cpp", "src/kernel/warning.h", "src/validation.cpp"}:
        return {"applicability": ["maintained_source"], "category": "manual-invariant",
                "confidence": "review-required", "provenance": "validation-adjacent"}
    if path.startswith("test/"):
        return {"applicability": ["maintained_source"], "category": "test",
                "confidence": "path-derived", "provenance": "test-path"}
    if path.startswith(("ci/", ".github/", "cmake/")) or path == "CMakeLists.txt":
        return {"applicability": ["build_input", "maintained_source"], "category": "build-ci",
                "confidence": "path-derived", "provenance": "build-path"}
    if path.startswith("doc/man/"):
        return {"applicability": ["generated_release_artifact"], "category": "generated-manpage",
                "confidence": "path-derived", "provenance": "generated-path"}
    if path.startswith("doc/"):
        return {"applicability": ["maintained_source"], "category": "documentation",
                "confidence": "path-derived", "provenance": "documentation-path"}
    if path.startswith(("src/qt/", "share/qt/")) or "bitcoinroots" in path or "bitcoinknots-logo" in path:
        return {"applicability": ["maintained_source"], "category": "branding-ui",
                "confidence": "path-derived", "provenance": "branding-path"}
    if path.startswith(("contrib/", "share/")):
        return {"applicability": ["build_input", "maintained_source"], "category": "packaging",
                "confidence": "path-derived", "provenance": "packaging-path"}
    return {"applicability": ["maintained_source"], "category": "release-metadata",
            "confidence": "path-derived", "provenance": "root-path"}


def inventory(left: Path, right: Path, left_revision: str, right_revision: str) -> dict[str, Any]:
    left_entries = tree_entries(left, left_revision)
    right_entries = tree_entries(right, right_revision)
    records = []
    for path in sorted(set(left_entries) | set(right_entries)):
        before, after = right_entries.get(path), left_entries.get(path)
        if before == after:
            continue
        status = "added" if before is None else "deleted" if after is None else "modified"
        record: dict[str, Any] = {"classification": classify_path(path), "left": after,
                                  "path": path, "right": before, "status": status}
        if (not before or before[1] == "blob") and (not after or after[1] == "blob") and path.endswith((".cpp", ".h", ".py", ".md", ".cmake", ".yml", ".yaml", ".txt", ".in", ".json", ".sh")):
            def blob(repository: Path, object_id: str) -> str:
                raw = subprocess.run(["git", "-C", str(repository), "cat-file", "blob", object_id], check=True, capture_output=True).stdout
                return raw.decode("utf-8", "strict")
            old = blob(right, before[2]) if before else ""
            new = blob(left, after[2]) if after else ""
            lines = list(difflib.unified_diff(old.splitlines(), new.splitlines(), n=0))
            record["hunk_digest"] = "sha1:" + hashlib.sha1("\n".join(lines).encode("utf-8")).hexdigest()
            record["hunk_count"] = sum(line.startswith("@@") for line in lines)
        else:
            record["non_text"] = True
        records.append(record)
    evidence = hashlib.sha1(json.dumps(records, ensure_ascii=False, sort_keys=True,
                                      separators=(",", ":")).encode("utf-8")).hexdigest()
    return {"evidence_digest": f"sha1:{evidence}", "records": records,
            "scope": "raw Git mode/type/blob records with conservative path classification"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate_parser = subparsers.add_parser("validate")
    validate_parser.add_argument("ledger", type=Path)
    regenerate_parser = subparsers.add_parser("regenerate")
    regenerate_parser.add_argument("ledger", type=Path)
    regenerate_parser.add_argument("--repository", action="append", default=[], metavar="RELEASE_ID=PATH")
    regenerate_parser.add_argument("--output", type=Path, required=True)
    compare_parser = subparsers.add_parser("compare")
    compare_parser.add_argument("--left", type=Path, required=True)
    compare_parser.add_argument("--right", type=Path, required=True)
    compare_parser.add_argument("--left-revision", default="HEAD")
    compare_parser.add_argument("--right-revision", default="HEAD")
    compare_parser.add_argument("--level", required=True, choices=sorted(LEVELS))
    compare_parser.add_argument("--exclude", action="append", default=[])
    inventory_parser = subparsers.add_parser("inventory")
    inventory_parser.add_argument("--left", type=Path, required=True)
    inventory_parser.add_argument("--right", type=Path, required=True)
    inventory_parser.add_argument("--left-revision", default="HEAD")
    inventory_parser.add_argument("--right-revision", default="HEAD")
    verify_parser = subparsers.add_parser("verify-report")
    verify_parser.add_argument("report", type=Path)
    verify_parser.add_argument("--left", type=Path, required=True)
    verify_parser.add_argument("--right", type=Path, required=True)
    verify_parser.add_argument("--left-revision", default="HEAD")
    verify_parser.add_argument("--right-revision", default="HEAD")
    args = parser.parse_args()
    try:
        if args.command == "validate":
            ledger = load_json(args.ledger)
            validate(ledger)
        elif args.command == "regenerate":
            ledger = load_json(args.ledger)
            repositories: dict[str, Path] = {}
            for item in args.repository:
                release_id, separator, raw_path = item.partition("=")
                if not separator or not release_id or not raw_path:
                    raise LedgerError("--repository must be RELEASE_ID=PATH")
                if os.path.isabs(raw_path):
                    raise LedgerError("--repository paths must be relative to the current directory")
                if release_id in repositories:
                    raise LedgerError("duplicate --repository release identity")
                repositories[release_id] = Path(raw_path).resolve()
            write_json(args.output, regenerate(ledger, repositories))
        elif args.command == "compare":
            print(json.dumps(compare(args.left, args.right, args.left_revision, args.right_revision,
                                     args.level, args.exclude), ensure_ascii=False, sort_keys=True))
        elif args.command == "inventory":
            print(json.dumps(inventory(args.left, args.right, args.left_revision, args.right_revision),
                             ensure_ascii=False, sort_keys=True))
        else:
            report = load_json(args.report)
            if report.get("schema_version") != 1:
                raise LedgerError("unsupported classification report schema")
            expected = dict(report)
            expected.pop("schema_version", None)
            actual = json.loads(json.dumps(inventory(args.left, args.right, args.left_revision, args.right_revision)))
            if expected != actual:
                raise LedgerError("stale classification report")
    except LedgerError as error:
        print(f"roots-lineage: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
