#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Create and verify deterministic, metadata-aware Roots delta-atlas inputs.

This program never fetches or clones. Snapshot inputs intentionally do not
claim POSIX mode provenance: only a Git tree manifest may make that claim.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import re
from typing import Any


# A two-release raw-byte manifest contains thousands of paths. Keep a bounded
# allowance above the current 1.2 MiB report while rejecting unbounded input.
MAX_JSON_BYTES = 4_000_000
IGNORED_DIRECTORY_NAMES = {".git", ".gestalt", "build", "CMakeFiles", "__pycache__"}


class AtlasError(ValueError):
    pass


def unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise AtlasError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def canonical_json(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def digest(value: Any) -> str:
    return "sha256:" + hashlib.sha256(canonical_json(value)).hexdigest()


def sha256_bytes(value: bytes) -> str:
    return "sha256:" + hashlib.sha256(value).hexdigest()


def checked_path(path: str) -> str:
    if not path or "\x00" in path or path.startswith("/") or "\\" in path:
        raise AtlasError("unsafe path")
    pieces = path.split("/")
    if any(piece in {"", ".", ".."} for piece in pieces):
        raise AtlasError("unsafe path")
    try:
        path.encode("utf-8", "strict")
    except UnicodeError as error:
        raise AtlasError("path is not valid UTF-8") from error
    return path


def read_json(path: Path) -> dict[str, Any]:
    try:
        if path.stat().st_size > MAX_JSON_BYTES:
            raise AtlasError("JSON input exceeds byte limit")
        value = json.loads(path.read_text(encoding="utf-8", errors="strict"), object_pairs_hook=unique_object)
    except AtlasError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise AtlasError("cannot read JSON input") from error
    if not isinstance(value, dict):
        raise AtlasError("JSON input must be an object")
    return value


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")


def channel_digests(records: list[dict[str, str]], mode_status: str) -> dict[str, str]:
    content = [{"path": item["path"], "sha256": item["sha256"]} for item in records if item["type"] == "file"]
    path_types = [{"path": item["path"], "type": item["type"]} for item in records]
    symlinks = [{"path": item["path"], "target": item["target"]} for item in records if item["type"] == "symlink"]
    channels = {"content": digest(content), "path_type": digest(path_types), "symlink_target": digest(symlinks)}
    if mode_status == "authoritative":
        executable = [{"path": item["path"], "executable": item["executable"]} for item in records if item["type"] == "file"]
        channels["executable_bit"] = digest(executable)
    return channels


def normalize_records(records: list[dict[str, str]], mode_status: str) -> dict[str, Any]:
    ordered = sorted(records, key=lambda item: item["path"])
    paths = [item["path"] for item in ordered]
    if len(paths) != len(set(paths)):
        raise AtlasError("duplicate normalized path")
    folded = [path.casefold() for path in paths]
    if len(folded) != len(set(folded)):
        raise AtlasError("path-case collision")
    return {"schema_version": 1, "records": ordered, "channels": channel_digests(ordered, mode_status), "mode_status": mode_status}


def snapshot_manifest(root: Path) -> dict[str, Any]:
    if not root.is_dir():
        raise AtlasError("snapshot root is not a directory")
    records: list[dict[str, str]] = []
    for current, directories, files in os.walk(root, topdown=True, followlinks=False):
        directories[:] = sorted(directory for directory in directories if directory not in IGNORED_DIRECTORY_NAMES)
        relative_current = Path(current).relative_to(root)
        for name in sorted(directories + files):
            candidate = Path(current) / name
            relative = checked_path((relative_current / name).as_posix())
            if os.path.islink(candidate):
                target = os.readlink(candidate)
                records.append({"path": relative, "target": sha256_bytes(target.encode("utf-8", "strict")), "type": "symlink"})
            elif os.path.isfile(candidate):
                records.append({"path": relative, "sha256": sha256_bytes(candidate.read_bytes()), "type": "file"})
            elif os.path.isdir(candidate):
                continue
            else:
                raise AtlasError("unsupported snapshot entry")
    manifest = normalize_records(records, "unavailable")
    manifest["source_kind"] = "snapshot"
    manifest["ignored_directory_names"] = sorted(IGNORED_DIRECTORY_NAMES)
    return manifest


def git_output(repository: Path, *args: str) -> bytes:
    completed = subprocess.run(["git", "-C", str(repository), *args], check=False, capture_output=True,
                               env={**os.environ, "LC_ALL": "C", "LANG": "C"})
    if completed.returncode:
        raise AtlasError(f"git {args[0]} failed")
    return completed.stdout


def git_blob_bytes(repository: Path, object_ids: list[str]) -> dict[str, bytes]:
    """Read each blob once, without one process per path."""
    unique_ids = sorted(set(object_ids))
    completed = subprocess.run(["git", "-C", str(repository), "cat-file", "--batch"], check=False,
                               input=("\n".join(unique_ids) + "\n").encode("ascii"), capture_output=True,
                               env={**os.environ, "LC_ALL": "C", "LANG": "C"})
    if completed.returncode:
        raise AtlasError("git cat-file failed")
    output, offset, result = completed.stdout, 0, {}
    for expected in unique_ids:
        line_end = output.find(b"\n", offset)
        if line_end < 0:
            raise AtlasError("malformed Git cat-file output")
        header = output[offset:line_end].decode("ascii").split()
        offset = line_end + 1
        if len(header) != 3 or header[0] != expected or header[1] != "blob" or not header[2].isdigit():
            raise AtlasError("Git tree references an unavailable blob")
        size = int(header[2])
        data = output[offset:offset + size]
        if len(data) != size or offset + size >= len(output) or output[offset + size:offset + size + 1] != b"\n":
            raise AtlasError("malformed Git blob output")
        result[expected] = data
        offset += size + 1
    if offset != len(output):
        raise AtlasError("unexpected trailing Git cat-file output")
    return result


def git_manifest(repository: Path, revision: str) -> dict[str, Any]:
    if not repository.is_dir() or git_output(repository, "rev-parse", "--is-inside-work-tree").strip() != b"true":
        raise AtlasError("Git input is not a work tree")
    entries: list[tuple[str, str, str]] = []
    for raw in git_output(repository, "ls-tree", "-r", "-z", revision).split(b"\0"):
        if not raw:
            continue
        metadata, separator, encoded_path = raw.partition(b"\t")
        fields = metadata.decode("ascii").split()
        if not separator or len(fields) != 3:
            raise AtlasError("malformed Git tree entry")
        mode, object_type, object_id = fields
        path = checked_path(encoded_path.decode("utf-8", "strict"))
        if object_type != "blob":
            raise AtlasError("unsupported non-blob Git tree entry")
        entries.append((mode, object_id, path))
    blobs = git_blob_bytes(repository, [object_id for _, object_id, _ in entries])
    records: list[dict[str, str]] = []
    for mode, object_id, path in entries:
        contents = blobs[object_id]
        if mode == "120000":
            records.append({"path": path, "target": sha256_bytes(contents), "type": "symlink"})
        elif mode in {"100644", "100755"}:
            records.append({"executable": "true" if mode == "100755" else "false", "path": path,
                            "sha256": sha256_bytes(contents), "type": "file"})
        else:
            raise AtlasError("unsupported Git file mode")
    manifest = normalize_records(records, "authoritative")
    manifest["source_kind"] = "git-tree"
    manifest["revision"] = git_output(repository, "rev-parse", "--verify", f"{revision}^{{tree}}").decode("ascii").strip()
    return manifest


def git_commit(repository: Path, revision: str) -> str:
    return git_output(repository, "rev-parse", "--verify", f"{revision}^{{commit}}").decode("ascii").strip()


def git_succeeds(repository: Path, *args: str) -> bool:
    return subprocess.run(["git", "-C", str(repository), *args], check=False, capture_output=True,
                          env={**os.environ, "LC_ALL": "C", "LANG": "C"}).returncode == 0


def changed_records(before: dict[str, Any], after: dict[str, Any]) -> list[dict[str, Any]]:
    before_records = {record["path"]: record for record in before["records"]}
    after_records = {record["path"]: record for record in after["records"]}
    changes = []
    for path in sorted(set(before_records) | set(after_records)):
        old, new = before_records.get(path), after_records.get(path)
        if old == new:
            continue
        changes.append({"after": new, "before": old, "path": path,
                        "status": "added" if old is None else "deleted" if new is None else "modified"})
    return changes


def release_commit(ledger: dict[str, Any], release_id: str) -> str:
    for release in ledger.get("releases", []):
        if isinstance(release, dict) and release.get("id") == release_id:
            commit = release.get("peeled_commit")
            if isinstance(commit, str) and commit.startswith("sha1:"):
                return commit.removeprefix("sha1:")
    raise AtlasError(f"lineage ledger lacks {release_id} commit")


def layer_partition(repository: Path, ledger_path: Path, first_roots: str, knots_parent: str) -> dict[str, Any]:
    """Compose immutable raw-tree deltas without rename/copy inference."""
    ledger = read_json(ledger_path)
    core = release_commit(ledger, "core-29.3")
    roots_target = release_commit(ledger, "roots-29.3-roots.1")
    parent = git_commit(repository, knots_parent)
    first = git_commit(repository, first_roots)
    expected_parent = git_output(repository, "show", "-s", "--format=%P", first).decode("ascii").strip()
    if expected_parent != parent:
        raise AtlasError("first Roots commit does not have the locked Knots parent")
    if not git_succeeds(repository, "merge-base", "--is-ancestor", core, parent):
        raise AtlasError("Core release is not an ancestor of the Knots parent")
    if not git_succeeds(repository, "merge-base", "--is-ancestor", first, roots_target):
        raise AtlasError("first Roots commit is not an ancestor of the target")
    revisions = [("core_to_knots", core, parent), ("knots_to_first_roots", parent, first),
                 ("first_roots_to_target", first, roots_target)]
    manifests = {revision: git_manifest(repository, revision) for revision in {core, parent, first, roots_target}}
    layers = []
    for layer_id, before_revision, after_revision in revisions:
        records = changed_records(manifests[before_revision], manifests[after_revision])
        layers.append({"after_tree": manifests[after_revision]["revision"], "before_tree": manifests[before_revision]["revision"],
                       "id": layer_id, "records": records, "record_count": len(records)})
    direct = changed_records(manifests[core], manifests[roots_target])
    state = {record["path"]: record for record in manifests[core]["records"]}
    ownership: dict[str, list[str]] = {}
    for layer in layers:
        for record in layer["records"]:
            path = record["path"]
            ownership.setdefault(path, []).append(layer["id"])
            if record["after"] is None:
                state.pop(path, None)
            else:
                state[path] = record["after"]
    final = {record["path"]: record for record in manifests[roots_target]["records"]}
    if state != final:
        raise AtlasError("ordered layer composition does not reach the target tree")
    unexplained = [record["path"] for record in direct if record["path"] not in ownership]
    if unexplained:
        raise AtlasError("direct Core-to-Roots delta has unexplained paths")
    first_changes = {record["path"]: record for record in layers[1]["records"]}
    later_changes = {record["path"]: record for record in layers[2]["records"]}
    amended = []
    for path in sorted(set(first_changes) & set(later_changes)):
        relation = "reverted" if later_changes[path]["after"] == first_changes[path]["before"] else "amended"
        amended.append({"layers": ["knots_to_first_roots", "first_roots_to_target"], "path": path, "relation": relation})
    return {"schema_version": 1,
            "comparison": "raw Git blob bytes, path type, executable bits, and symlink targets; rename/copy inference excluded",
            "direct_core_to_roots": {"records": direct, "record_count": len(direct)},
            "inputs": {"core": core, "knots_parent": parent, "first_roots": first, "roots_target": roots_target},
            "layers": layers,
            "overlapping_ownership": [{"layers": owners, "path": path} for path, owners in sorted(ownership.items()) if len(owners) > 1],
            "first_roots_later_relations": amended,
            "unexplained_direct_paths": unexplained}


TEXT_SUFFIXES = (".cmake", ".cpp", ".h", ".json", ".md", ".py", ".sh", ".txt", ".yml", ".yaml")
SYMBOL_RE = re.compile(r"^[+-]?(?:class|struct|namespace)\s+([A-Za-z_][A-Za-z0-9_:]*)|^[+-]?[A-Za-z_][A-Za-z0-9_:<>, ]+\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\(", re.MULTILINE)
TARGET_RE = re.compile(r"\badd_(?:library|executable)\s*\(\s*([A-Za-z0-9_.-]+)", re.IGNORECASE)


def inventory_metadata(path: str, patch: bytes) -> dict[str, Any]:
    text = path.endswith(TEXT_SUFFIXES)
    result: dict[str, Any] = {"binary": not text,
                               "generated_status": "generated" if path.startswith("doc/man/") else "source",
                               "subtree": next((prefix.rstrip("/") for prefix in ("src/secp256k1/", "src/leveldb/", "src/crc32c/", "src/crypto/ctaes/", "src/minisketch/") if path.startswith(prefix)), None)}
    if text:
        decoded = patch.decode("utf-8", "replace")
        result["hunk_digest"] = sha256_bytes(patch)
        result["hunk_count"] = sum(line.startswith("@@") for line in decoded.splitlines())
        result["symbols"] = sorted({first or second for first, second in SYMBOL_RE.findall(decoded)})
        result["build_targets"] = sorted(set(TARGET_RE.findall(decoded))) if path.endswith(("CMakeLists.txt", ".cmake")) else []
    return result


def granular_inventory(repository: Path, partition: dict[str, Any]) -> dict[str, Any]:
    if partition.get("schema_version") != 1 or not isinstance(partition.get("layers"), list):
        raise AtlasError("unsupported layer partition")
    inputs = partition.get("inputs", {})
    transitions = {"core_to_knots": (inputs.get("core"), inputs.get("knots_parent")),
                   "knots_to_first_roots": (inputs.get("knots_parent"), inputs.get("first_roots")),
                   "first_roots_to_target": (inputs.get("first_roots"), inputs.get("roots_target"))}
    layers = []
    for layer in partition["layers"]:
        layer_id = layer.get("id")
        if layer_id not in transitions or not all(isinstance(item, str) for item in transitions[layer_id]):
            raise AtlasError("partition has invalid layer input")
        before, after = transitions[layer_id]
        records = []
        for raw in layer["records"]:
            path = raw["path"]
            patch = git_output(repository, "diff", "--no-ext-diff", "--unified=0", before, after, "--", path)
            records.append({**raw, **inventory_metadata(path, patch)})
        layers.append({"id": layer_id, "record_count": len(records), "records": records})
    flattened = [record for layer in layers for record in layer["records"]]
    if sum(layer["record_count"] for layer in layers) != len(flattened):
        raise AtlasError("inventory path coverage failure")
    return {"schema_version": 1, "input_partition_digest": digest(partition), "layers": layers,
            "coverage": {"layer_record_count": len(flattened), "unique_paths": len({record["path"] for record in flattened})}}


def classify_record(layer: str, record: dict[str, Any]) -> dict[str, str]:
    path = record["path"]
    if path.startswith("doc/man/"):
        area, purpose, risk = "release", "generated-output", "low"
    elif path.startswith((".github/", "ci/", "cmake/")) or path.endswith(("CMakeLists.txt", ".cmake")):
        area, purpose, risk = "build-ci", "build-or-release", "medium"
    elif path.startswith("test/"):
        area, purpose, risk = "tests", "test-coverage", "medium"
    elif path.startswith("src/wallet/"):
        area, purpose, risk = "wallet", "wallet", "high"
    elif path.startswith(("src/net", "src/node/", "src/txrequest")):
        area, purpose, risk = "p2p", "networking", "high"
    elif path.startswith("src/policy/") or path in {"src/txmempool.cpp", "src/txmempool.h"}:
        area, purpose, risk = "policy", "local-policy", "critical"
    elif path.startswith(("src/consensus/", "src/script/", "src/primitives/")) or path == "src/validation.cpp":
        area, purpose, risk = "consensus-validation", "consensus-adjacent", "critical"
    elif path.startswith(("src/qt/", "share/qt/")):
        area, purpose, risk = "gui", "branding-ui", "medium"
    elif path.startswith("doc/"):
        area, purpose, risk = "documentation", "documentation", "low"
    else:
        area, purpose, risk = "common", "maintenance", "medium"
    provenance = "knots-inherited" if layer == "core_to_knots" else "roots-owned"
    effect = "manual-review" if area in {"consensus-validation", "policy"} else "none-observed"
    return {"area": area, "consensus_policy_effect": effect, "confidence": "path-derived-review-required" if risk in {"critical", "high"} else "path-derived", "primary_purpose": purpose, "provenance": provenance, "risk": risk}


def classify_inventory(inventory: dict[str, Any]) -> dict[str, Any]:
    if inventory.get("schema_version") != 1 or not isinstance(inventory.get("layers"), list):
        raise AtlasError("unsupported granular inventory")
    records = []
    for layer in inventory["layers"]:
        layer_id = layer.get("id")
        if not isinstance(layer_id, str) or not isinstance(layer.get("records"), list):
            raise AtlasError("malformed inventory layer")
        for record in layer["records"]:
            if not isinstance(record, dict) or not isinstance(record.get("path"), str):
                raise AtlasError("malformed inventory record")
            records.append({"classification": classify_record(layer_id, record), "layer": layer_id, "path": record["path"]})
    if len(records) != sum(layer.get("record_count", -1) for layer in inventory["layers"]):
        raise AtlasError("classification coverage failure")
    queues = {"critical": [], "high": [], "ambiguous": []}
    for record in records:
        classification = record["classification"]
        if classification["risk"] in {"critical", "high"}:
            queues[classification["risk"]].append(record["path"])
        if classification["confidence"] != "path-derived":
            queues["ambiguous"].append(record["path"])
    return {"schema_version": 1, "input_inventory_digest": digest(inventory), "records": records,
            "review_queues": {key: sorted(values) for key, values in queues.items()}}


def publish_atlas(lock: dict[str, Any], partition: dict[str, Any], inventory: dict[str, Any], classification: dict[str, Any]) -> dict[str, Any]:
    if any(value.get("schema_version") != 1 for value in (lock, partition, inventory, classification)):
        raise AtlasError("unsupported atlas input schema")
    if classification.get("input_inventory_digest") != digest(inventory):
        raise AtlasError("stale classification input")
    direct = partition.get("direct_core_to_roots", {}).get("records", [])
    if partition.get("unexplained_direct_paths") or not isinstance(direct, list):
        raise AtlasError("partition does not completely cover direct delta")
    direct_paths = {record.get("path") for record in direct}
    classified_paths = {record.get("path") for record in classification.get("records", [])}
    if not direct_paths <= classified_paths:
        raise AtlasError("direct delta lacks classification")
    candidates: dict[str, list[str]] = {}
    for record in classification["records"]:
        area = record["classification"]["area"]
        candidates.setdefault(area, []).append(record["path"])
    return {"schema_version": 1,
            "input_digests": {"classification": digest(classification), "input_lock": digest(lock), "inventory": digest(inventory), "partition": digest(partition)},
            "coverage": {"direct_paths": len(direct_paths), "classified_layer_records": len(classification["records"]), "unexplained_direct_paths": 0},
            "layer_summaries": [{"id": layer["id"], "record_count": layer["record_count"]} for layer in partition["layers"]],
            "review_queues": classification["review_queues"],
            "adaptation_candidates": [{"area": area, "paths": sorted(paths)} for area, paths in sorted(candidates.items())],
            "handoff": "L3 must create logical adaptation units from these candidate areas and retain every input digest and review queue."}


def archive_manifest(archive: Path) -> dict[str, Any]:
    records: list[dict[str, str]] = []
    member_order: list[str] = []
    try:
        with tarfile.open(archive, "r:*") as contents:
            for member in contents:
                path = checked_path(member.name.rstrip("/"))
                member_order.append(path)
                if member.isfile():
                    extracted = contents.extractfile(member)
                    if extracted is None:
                        raise AtlasError("cannot read archive member")
                    records.append({"path": path, "sha256": sha256_bytes(extracted.read()), "type": "file"})
                elif member.issym():
                    records.append({"path": path, "target": sha256_bytes(member.linkname.encode("utf-8", "strict")), "type": "symlink"})
                elif not member.isdir():
                    raise AtlasError("unsupported archive member")
    except (OSError, tarfile.TarError) as error:
        raise AtlasError("cannot read archive") from error
    manifest = normalize_records(records, "unavailable")
    manifest["archive_member_order"] = digest(member_order)
    manifest["source_kind"] = "archive"
    return manifest


def compare(left: dict[str, Any], right: dict[str, Any]) -> dict[str, Any]:
    if left.get("schema_version") != 1 or right.get("schema_version") != 1:
        raise AtlasError("unsupported manifest schema")
    all_channels = sorted(set(left["channels"]) | set(right["channels"]))
    result: dict[str, str] = {}
    for channel in all_channels:
        if channel not in left["channels"] or channel not in right["channels"]:
            result[channel] = "unavailable"
        else:
            result[channel] = "same" if left["channels"][channel] == right["channels"][channel] else "different"
    if "archive_member_order" in left or "archive_member_order" in right:
        result["archive_member_order"] = "same" if left.get("archive_member_order") == right.get("archive_member_order") else "different"
    return {"channels": result, "equal": all(status == "same" for status in result.values())}


def parse_assignment(value: str) -> tuple[str, Path]:
    label, separator, raw_path = value.partition("=")
    if not separator or not label or not raw_path or "/" in label or "\\" in label:
        raise AtlasError("input must be LABEL=RELATIVE_PATH")
    path = Path(raw_path)
    if path.is_absolute() or ".." in path.parts:
        raise AtlasError("input paths must be relative")
    return label, path


def input_lock(ledger_path: Path, snapshots: list[str]) -> dict[str, Any]:
    ledger = read_json(ledger_path)
    releases = ledger.get("releases")
    if not isinstance(releases, list):
        raise AtlasError("lineage ledger lacks releases")
    pinned = {}
    for release in releases:
        if not isinstance(release, dict) or release.get("id") not in {"knots-29.3.knots20260507", "roots-29.3-roots.1"}:
            continue
        if not all(isinstance(release.get(key), str) for key in ("id", "peeled_commit", "tree", "tag_ref")):
            raise AtlasError("lineage ledger lacks immutable release identity")
        pinned[release["id"]] = {key: release[key] for key in ("peeled_commit", "tag_ref", "tree")}
    if set(pinned) != {"knots-29.3.knots20260507", "roots-29.3-roots.1"}:
        raise AtlasError("lineage ledger lacks required Knots/Roots releases")
    entries = []
    seen = set()
    for value in snapshots:
        label, path = parse_assignment(value)
        if label in seen:
            raise AtlasError("duplicate snapshot label")
        seen.add(label)
        entries.append({"id": label, "manifest": snapshot_manifest(path), "provenance": "workspace-supplied non-Git snapshot; modes and release identity unavailable"})
    if {entry["id"] for entry in entries} != {"core-29.3-snapshot", "core-29.4-snapshot"}:
        raise AtlasError("lock requires Core 29.3 and 29.4 snapshot labels")
    return {"schema_version": 1, "git_inputs": pinned, "normalization": {"locale": "C", "path_separator": "/", "snapshot_executable_bits": "unavailable", "snapshot_modes": "unavailable", "text": "raw bytes; no line-ending conversion"}, "snapshot_inputs": sorted(entries, key=lambda entry: entry["id"])}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    for name in ("snapshot-manifest", "git-manifest", "archive-manifest"):
        command = subparsers.add_parser(name)
        command.add_argument("input", type=Path)
        if name == "git-manifest":
            command.add_argument("revision")
        command.add_argument("--output", type=Path, required=True)
    compare_parser = subparsers.add_parser("compare")
    compare_parser.add_argument("left", type=Path)
    compare_parser.add_argument("right", type=Path)
    lock_parser = subparsers.add_parser("lock-inputs")
    lock_parser.add_argument("--ledger", type=Path, required=True)
    lock_parser.add_argument("--snapshot", action="append", default=[], required=True)
    lock_parser.add_argument("--output", type=Path, required=True)
    partition_parser = subparsers.add_parser("partition-layers")
    partition_parser.add_argument("--repository", type=Path, required=True)
    partition_parser.add_argument("--ledger", type=Path, required=True)
    partition_parser.add_argument("--knots-parent", required=True)
    partition_parser.add_argument("--first-roots", required=True)
    partition_parser.add_argument("--output", type=Path, required=True)
    verify_partition_parser = subparsers.add_parser("verify-layer-partition")
    verify_partition_parser.add_argument("report", type=Path)
    verify_partition_parser.add_argument("--repository", type=Path, required=True)
    verify_partition_parser.add_argument("--ledger", type=Path, required=True)
    verify_partition_parser.add_argument("--knots-parent", required=True)
    verify_partition_parser.add_argument("--first-roots", required=True)
    inventory_parser = subparsers.add_parser("inventory-granularity")
    inventory_parser.add_argument("--repository", type=Path, required=True)
    inventory_parser.add_argument("--partition", type=Path, required=True)
    inventory_parser.add_argument("--output", type=Path, required=True)
    classify_parser = subparsers.add_parser("classify-inventory")
    classify_parser.add_argument("--inventory", type=Path, required=True)
    classify_parser.add_argument("--output", type=Path, required=True)
    report_parser = subparsers.add_parser("publish-report")
    for name in ("lock", "partition", "inventory", "classification"):
        report_parser.add_argument(f"--{name}", type=Path, required=True)
    report_parser.add_argument("--output", type=Path, required=True)
    verify_report_parser = subparsers.add_parser("verify-report")
    verify_report_parser.add_argument("report", type=Path)
    for name in ("lock", "partition", "inventory", "classification"):
        verify_report_parser.add_argument(f"--{name}", type=Path, required=True)
    verify_parser = subparsers.add_parser("verify-input-lock")
    verify_parser.add_argument("lock", type=Path)
    verify_parser.add_argument("--ledger", type=Path, required=True)
    verify_parser.add_argument("--snapshot", action="append", default=[], required=True)
    args = parser.parse_args()
    try:
        if args.command == "snapshot-manifest":
            write_json(args.output, snapshot_manifest(args.input))
        elif args.command == "git-manifest":
            write_json(args.output, git_manifest(args.input, args.revision))
        elif args.command == "archive-manifest":
            write_json(args.output, archive_manifest(args.input))
        elif args.command == "compare":
            print(json.dumps(compare(read_json(args.left), read_json(args.right)), sort_keys=True))
        elif args.command == "lock-inputs":
            write_json(args.output, input_lock(args.ledger, args.snapshot))
        elif args.command == "partition-layers":
            write_json(args.output, layer_partition(args.repository, args.ledger, args.first_roots, args.knots_parent))
        elif args.command == "verify-layer-partition":
            if read_json(args.report) != layer_partition(args.repository, args.ledger, args.first_roots, args.knots_parent):
                raise AtlasError("stale layer partition")
        elif args.command == "inventory-granularity":
            write_json(args.output, granular_inventory(args.repository, read_json(args.partition)))
        elif args.command == "classify-inventory":
            write_json(args.output, classify_inventory(read_json(args.inventory)))
        elif args.command == "publish-report":
            write_json(args.output, publish_atlas(read_json(args.lock), read_json(args.partition), read_json(args.inventory), read_json(args.classification)))
        elif args.command == "verify-report":
            actual = publish_atlas(read_json(args.lock), read_json(args.partition), read_json(args.inventory), read_json(args.classification))
            if read_json(args.report) != actual:
                raise AtlasError("stale atlas report")
        else:
            expected = read_json(args.lock)
            actual = input_lock(args.ledger, args.snapshot)
            if expected != actual:
                raise AtlasError("stale input lock")
    except AtlasError as error:
        print(f"roots-delta-atlas: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
