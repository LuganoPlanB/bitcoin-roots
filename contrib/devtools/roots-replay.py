#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Safely verify and plan an offline Bitcoin Roots replay.

This command intentionally does not fetch, checkout, apply a patch, create a
worktree, or modify the repository passed with ``--repository``.  Its first
commands establish the immutable input boundary required before a later replay
handler may create an owned disposable worktree.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import shutil
import subprocess
import sys
import tempfile
from typing import Any


OID_RE = re.compile(r"^[0-9a-f]{40}$")
MAX_JSON_BYTES = 1_000_000
TOOL_VERSION = "1"
REPLAY_ENVIRONMENT = {"LC_ALL": "C", "LANG": "C", "TZ": "UTC", "umask": "022"}


class ReplayError(ValueError):
    pass


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise ReplayError("duplicate JSON object key")
        value[key] = item
    return value


def canonical_json(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def digest(value: Any) -> str:
    return "sha256:" + hashlib.sha256(canonical_json(value)).hexdigest()


def _safe_json_file(path: Path, expected_name: str, field: str) -> Any:
    if not path.is_absolute() or path.name != expected_name:
        raise ReplayError(f"{field} must be an absolute {expected_name} path")
    try:
        resolved = path.resolve(strict=True)
        if not resolved.is_file() or resolved.is_symlink() or resolved.stat().st_size > MAX_JSON_BYTES:
            raise ReplayError(f"{field} file is unsafe")
        return json.loads(resolved.read_text(encoding="utf-8"), object_pairs_hook=_unique_object)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ReplayError(f"cannot read {field}") from error


def _oid(value: str, field: str) -> str:
    if not OID_RE.fullmatch(value):
        raise ReplayError(f"{field} must be a full SHA-1 object ID")
    return value


def _directory(value: str, field: str) -> Path:
    path = Path(value)
    if not path.is_absolute():
        raise ReplayError(f"{field} must be an absolute path")
    try:
        resolved = path.resolve(strict=True)
    except OSError as error:
        raise ReplayError(f"{field} does not exist") from error
    if not resolved.is_dir() or resolved.is_symlink():
        raise ReplayError(f"{field} must name a real directory")
    return resolved


def _git(repository: Path, *args: str) -> str:
    # Empty HOME and config isolation make aliases, includeIf, hooks, replace
    # refs, and caller-local Git settings irrelevant to the evidence command.
    environment = {
        "PATH": "/usr/bin:/bin",
        "LC_ALL": "C",
        "LANG": "C",
        "TZ": "UTC",
        "HOME": tempfile.gettempdir() + "/roots-replay-empty-home",
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_NO_REPLACE_OBJECTS": "1",
        "GIT_OPTIONAL_LOCKS": "0",
    }
    result = subprocess.run(
        ["git", "-c", "core.hooksPath=/dev/null", "-C", str(repository), *args],
        check=False,
        capture_output=True,
        encoding="utf-8",
        errors="strict",
        env=environment,
    )
    if result.returncode:
        raise ReplayError("Git verification failed")
    return result.stdout.rstrip("\n")


def _git_bytes(repository: Path, *args: str) -> bytes:
    environment = {"PATH": "/usr/bin:/bin", "LC_ALL": "C", "LANG": "C", "TZ": "UTC", "HOME": tempfile.gettempdir() + "/roots-replay-empty-home", "GIT_CONFIG_NOSYSTEM": "1", "GIT_NO_REPLACE_OBJECTS": "1", "GIT_OPTIONAL_LOCKS": "0"}
    result = subprocess.run(["git", "-c", "core.hooksPath=/dev/null", "-C", str(repository), *args], check=False, capture_output=True, env=environment)
    if result.returncode:
        raise ReplayError("Git object lookup failed")
    return result.stdout


def _repository_snapshot(repository: Path) -> dict[str, str]:
    return {
        "head": _git(repository, "rev-parse", "HEAD"),
        "branch": _git(repository, "symbolic-ref", "--quiet", "--short", "HEAD") if _git_succeeds(repository, "symbolic-ref", "--quiet", "--short", "HEAD") else "DETACHED",
        "status": _git(repository, "status", "--porcelain=v1", "-z"),
        "index_tree": _git(repository, "write-tree"),
    }


def _git_succeeds(repository: Path, *args: str) -> bool:
    try:
        _git(repository, *args)
    except ReplayError:
        return False
    return True


def verify(repository: Path, revision: str, expected_tree: str) -> dict[str, Any]:
    revision = _oid(revision, "revision")
    expected_tree = _oid(expected_tree, "expected tree")
    if _git(repository, "rev-parse", "--is-inside-work-tree") != "true":
        raise ReplayError("repository must be a non-bare worktree")
    git_dir = Path(_git(repository, "rev-parse", "--git-dir"))
    if not git_dir.is_absolute():
        git_dir = repository / git_dir
    if (git_dir / "info" / "grafts").exists() or (git_dir / "info" / "alternates").exists():
        raise ReplayError("grafts and alternates are not permitted for replay inputs")
    if _git(repository, "for-each-ref", "--format=%(refname)", "refs/replace"):
        raise ReplayError("replacement refs are not permitted for replay inputs")
    before = _repository_snapshot(repository)
    if before["status"]:
        raise ReplayError("repository tracked worktree and index must be clean")
    if _git(repository, "cat-file", "-t", revision) != "commit":
        raise ReplayError("revision must resolve to a commit object")
    actual_tree = _git(repository, "rev-parse", f"{revision}^{{tree}}")
    if actual_tree != expected_tree:
        raise ReplayError("locked tree does not match revision")
    after = _repository_snapshot(repository)
    if after != before:
        raise ReplayError("verification changed the caller repository")
    return {
        "schema_version": 1,
        "revision": revision,
        "tree": actual_tree,
        "repository_snapshot": before,
    }


def _write_new(path: Path, value: dict[str, Any]) -> None:
    if not path.is_absolute() or path.name != "replay-plan.json" or path.parent == path:
        raise ReplayError("output must be an absolute replay-plan.json path")
    parent = _directory(str(path.parent), "output directory")
    destination = parent / path.name
    if destination.exists() or destination.is_symlink():
        raise ReplayError("refusing to replace an existing output")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    try:
        descriptor = os.open(destination, flags, 0o600)
        with os.fdopen(descriptor, "wb") as output:
            output.write(canonical_json(value) + b"\n")
    except OSError as error:
        raise ReplayError("cannot create replay plan") from error


def validate_plan(value: Any) -> None:
    if not isinstance(value, dict) or set(value) != {"schema_version", "tool", "input", "operations", "replay_state", "digest"}:
        raise ReplayError("invalid replay plan fields")
    if value["schema_version"] != 1 or value["tool"] != "roots-replay" or value["operations"] != ["verify", "plan"] or value["replay_state"] != "not-created":
        raise ReplayError("unsupported replay plan")
    input_lock = value["input"]
    if not isinstance(input_lock, dict) or set(input_lock) != {"schema_version", "revision", "tree", "repository_snapshot"}:
        raise ReplayError("invalid replay plan input")
    if input_lock["schema_version"] != 1 or not isinstance(input_lock["repository_snapshot"], dict):
        raise ReplayError("invalid replay plan input")
    _oid(input_lock["revision"], "plan revision")
    _oid(input_lock["tree"], "plan tree")
    if not isinstance(value["digest"], str) or value["digest"] != digest({key: item for key, item in value.items() if key != "digest"}):
        raise ReplayError("replay plan digest mismatch")


def read_manifest_units(path: Path) -> list[dict[str, Any]]:
    """Derive the execution view solely from the validated L3 manifest."""
    value = _safe_json_file(path, "adaptation-manifest-29.3.json", "adaptation manifest")
    validator = Path(__file__).with_name("roots-adaptation-manifest.py")
    result = subprocess.run([sys.executable, str(validator), str(path)], check=False, capture_output=True)
    if result.returncode or not isinstance(value, dict) or not isinstance(value.get("units"), list):
        raise ReplayError("adaptation manifest is invalid")
    units = []
    for item in sorted(value["units"], key=lambda unit: unit["id"]):
        application = item.get("application", {})
        if item.get("lifecycle") != "active" or application.get("mechanism") not in {"commit", "patch", "module/data", "generator", "manual"}:
            raise ReplayError("manifest unit lacks fail-closed application material")
        unit = {"id": item["id"], "mechanism": application["mechanism"], "reference": application["reference"], "dependencies": item["dependencies"]}
        if application["mechanism"] == "manual": unit["reason"] = application["reference"]
        units.append(unit)
    return units


def read_replay_materials(path: Path, manifest_units: list[dict[str, Any]]) -> dict[str, Any]:
    value = _safe_json_file(path, "replay-materials.json", "replay materials")
    if not isinstance(value, dict) or set(value) != {"schema_version", "materials"} or value["schema_version"] != 1 or not isinstance(value["materials"], dict):
        raise ReplayError("replay materials envelope is invalid")
    expected = {unit["reference"] for unit in manifest_units}
    if set(value["materials"]) != expected:
        raise ReplayError("replay materials do not exactly cover manifest references")
    for unit in manifest_units:
        material = value["materials"][unit["reference"]]
        if not isinstance(material, dict) or material.get("mechanism") != unit["mechanism"]:
            raise ReplayError("replay material mechanism does not match manifest")
        _validate_material_record(material)
    return value


def _safe_relative_material_path(value: Any, field: str) -> None:
    if not isinstance(value, str):
        raise ReplayError(f"{field} is invalid")
    path = Path(value)
    if path.is_absolute() or ".." in path.parts or not value or "\\" in value:
        raise ReplayError(f"{field} is unsafe")


def _validate_material_record(material: dict[str, Any]) -> None:
    """Reject stale or incomplete material before an owned clone is created."""
    mechanism = material.get("mechanism")
    fields = {
        "manual": {"mechanism"},
        "patch": {"mechanism", "patch", "expected_patch_sha256", "expected_before_tree", "expected_after_tree"},
        "commit": {"mechanism", "commit", "expected_patch_sha256", "expected_before_tree", "expected_after_tree"},
        "module/data": {"mechanism", "source", "destination", "expected_sha256", "expected_before_tree", "expected_after_tree"},
        "generator": {"mechanism", "generator", "source", "destination", "expected_source_sha256", "expected_output_sha256", "expected_before_tree", "expected_after_tree"},
    }
    if mechanism not in fields or set(material) != fields[mechanism]:
        raise ReplayError("replay material has stale or incomplete fields")
    for key in ("expected_before_tree", "expected_after_tree"):
        if key in material:
            _oid(material[key], f"material {key}")
    for key in ("expected_patch_sha256", "expected_sha256", "expected_source_sha256", "expected_output_sha256"):
        if key in material and (not isinstance(material[key], str) or not re.fullmatch(r"sha256:[0-9a-f]{64}", material[key])):
            raise ReplayError(f"material {key} is invalid")
    if mechanism == "commit":
        _oid(material["commit"], "material commit")
    if mechanism in {"patch", "module/data"}:
        _safe_relative_material_path(material["patch"] if mechanism == "patch" else material["source"], "material path")
    if mechanism == "generator":
        if material["generator"] != "normalize-lf":
            raise ReplayError("generator is not allowlisted")
        _safe_relative_material_path(material["source"], "generator source")
        _safe_relative_material_path(material["destination"], "generator destination")
    if mechanism == "module/data":
        _safe_relative_material_path(material["destination"], "data destination")


def materialize_units(manifest_units: list[dict[str, Any]], materials: dict[str, Any], materials_root: Path) -> list[dict[str, Any]]:
    """Join only a manifest-owned logical reference to its locked material."""
    units = []
    for unit in manifest_units:
        material = materials["materials"][unit["reference"]]
        record = {"id": unit["id"], "dependencies": unit["dependencies"], **material}
        if record["mechanism"] == "manual": record["reason"] = unit["reference"]
        if record["mechanism"] == "patch":
            candidate = materials_root / record.get("patch", "")
            if not record.get("patch") or candidate.is_symlink() or candidate.resolve().parent != materials_root.resolve():
                raise ReplayError("patch material path is unsafe")
            record["patch"] = str(candidate.resolve())
        if record["mechanism"] == "module/data":
            candidate = materials_root / record.get("source", "")
            if not record.get("source") or candidate.is_symlink() or candidate.resolve().parent != materials_root.resolve():
                raise ReplayError("data material path is unsafe")
            record["source"] = str(candidate.resolve())
        if record["mechanism"] == "generator":
            source = Path(record.get("source", ""))
            if not record.get("source") or source.is_absolute() or ".." in source.parts or "\\" in record["source"]:
                raise ReplayError("generator candidate source is unsafe")
        units.append(record)
    return units


def manifest_lock(units: list[dict[str, Any]], materials: dict[str, Any] | None = None) -> str:
    """The canonical units file is the configuration lock for replay/resume."""
    sequence_units(units)
    normalized = []
    for unit in units:
        item = dict(unit)
        for key in ("patch", "source"):
            if key in item:
                item[key] = Path(item[key]).name
        normalized.append(item)
    value: dict[str, Any] = {"tool_version": TOOL_VERSION, "environment": REPLAY_ENVIRONMENT, "units": normalized}
    if materials is not None: value["materials_digest"] = digest(materials)
    return digest(value)


def _cleanup_owned_candidate(state_directory: Path) -> None:
    directory = _directory(str(state_directory), "run state directory")
    candidate = directory / "owned-candidate"
    marker = directory / "owned-candidate.json"
    lock = directory / "owned-candidate.lock"
    if marker.exists() and (marker.is_symlink() or not marker.is_file()):
        raise ReplayError("owned candidate marker is unsafe")
    if lock.exists() and (lock.is_symlink() or not lock.is_file()):
        raise ReplayError("owned candidate lock is unsafe")
    if candidate.exists():
        if candidate.is_symlink() or not candidate.is_dir() or candidate.parent.resolve() != directory:
            raise ReplayError("owned candidate target is unsafe")
        shutil.rmtree(candidate)
    marker.unlink(missing_ok=True)
    lock.unlink(missing_ok=True)


class ReplaySignal(ReplayError):
    pass


class _owned_candidate_signal_guard:
    """Remove only this run's owned candidate when an interrupt is delivered."""
    def __init__(self, state_directory: Path) -> None:
        self.state_directory = state_directory
        self.previous: dict[int, Any] = {}

    def __enter__(self) -> "_owned_candidate_signal_guard":
        def interrupted(signum: int, _frame: Any) -> None:
            _cleanup_owned_candidate(self.state_directory)
            raise ReplaySignal(f"replay interrupted by signal {signum}")

        for signum in (signal.SIGINT, signal.SIGTERM):
            self.previous[signum] = signal.getsignal(signum)
            signal.signal(signum, interrupted)
        return self

    def __exit__(self, _type: Any, _value: Any, _traceback: Any) -> None:
        for signum, previous in self.previous.items():
            signal.signal(signum, previous)


def _clone_owned_candidate(source: Path, lock: dict[str, Any], state_directory: Path) -> Path:
    """Make an independent local clone; never add a worktree to the caller repo."""
    directory = _directory(str(state_directory), "run state directory")
    candidate = directory / "owned-candidate"
    marker = directory / "owned-candidate.json"
    claim = directory / "owned-candidate.lock"
    if candidate.exists() or marker.exists() or claim.exists() or candidate.is_symlink() or marker.is_symlink() or claim.is_symlink():
        raise ReplayError("owned candidate directory already exists")
    _write_bundle_file(claim, canonical_json({"schema_version": 1, "candidate": "owned-candidate"}) + b"\n")
    environment = {"PATH": "/usr/bin:/bin", "HOME": tempfile.gettempdir() + "/roots-replay-empty-home",
                   "GIT_CONFIG_NOSYSTEM": "1", "GIT_NO_REPLACE_OBJECTS": "1", **REPLAY_ENVIRONMENT}
    result = subprocess.run(["git", "-c", "core.hooksPath=/dev/null", "clone", "--no-checkout", "--", str(source), str(candidate)], check=False,
                            capture_output=True, env=environment)
    try:
        if result.returncode:
            raise ReplayError("cannot create owned candidate")
        os.chmod(candidate, 0o700)
        _git(candidate, "reset", "--hard", lock["revision"])
        _write_bundle_file(marker, canonical_json({"schema_version": 1, "input_lock": lock, "candidate": "owned-candidate"}) + b"\n")
    except (ReplayError, KeyboardInterrupt):
        _cleanup_owned_candidate(directory)
        raise
    claim.unlink(missing_ok=True)
    return candidate.resolve()


def _owned_candidate(state_directory: Path, lock: dict[str, Any]) -> Path:
    directory = _directory(str(state_directory), "run state directory")
    marker = _safe_json_file(directory / "owned-candidate.json", "owned-candidate.json", "owned candidate marker")
    if marker != {"schema_version": 1, "input_lock": lock, "candidate": "owned-candidate"}:
        raise ReplayError("owned candidate marker does not match input lock")
    candidate = directory / "owned-candidate"
    if not candidate.is_dir() or candidate.is_symlink() or _git(candidate, "rev-parse", "--is-inside-work-tree") != "true":
        raise ReplayError("owned candidate is unavailable")
    return candidate.resolve()


def replay_owned(source: Path, revision: str, expected_tree: str, units: list[dict[str, Any]], state_directory: Path, config_digest: str | None = None) -> tuple[Path, list[dict[str, str]]]:
    before = _repository_snapshot(source)
    lock = verify(source, revision, expected_tree)
    manifest = config_digest or manifest_lock(units)
    candidate = _clone_owned_candidate(source, lock, state_directory)
    try:
        with _owned_candidate_signal_guard(state_directory):
            outcomes = replay_units(candidate, units, lock, manifest, state_directory)
    except (ReplayError, KeyboardInterrupt):
        _cleanup_owned_candidate(state_directory)
        raise
    if _repository_snapshot(source) != before:
        raise ReplayError("replay changed the caller repository")
    return candidate, outcomes


def resume_owned(source: Path, revision: str, expected_tree: str, units: list[dict[str, Any]], state_path: Path,
                 state_directory: Path, config_digest: str | None = None) -> tuple[Path, list[dict[str, str]]]:
    before = _repository_snapshot(source)
    lock = verify(source, revision, expected_tree)
    manifest = config_digest or manifest_lock(units)
    state = read_run_state(state_path)
    try:
        with _owned_candidate_signal_guard(state_directory):
            candidate = _owned_candidate(state_directory, lock)
            outcomes = resume_units(candidate, units, lock, manifest, state_path, state_directory)
    except (ReplaySignal, KeyboardInterrupt):
        _cleanup_owned_candidate(state_directory)
        raise
    if _repository_snapshot(source) != before:
        raise ReplayError("resume changed the caller repository")
    return candidate, outcomes


def plan(repository: Path, revision: str, expected_tree: str, output: Path) -> dict[str, Any]:
    lock = verify(repository, revision, expected_tree)
    value = {
        "schema_version": 1,
        "tool": "roots-replay",
        "input": lock,
        "operations": ["verify", "plan"],
        "replay_state": "not-created",
    }
    value["digest"] = digest(value)
    validate_plan(value)
    _write_new(output, value)
    return value


def stage_candidate(repository: Path, expected_branch: str, expected_head: str, candidate_revision: str,
                    expected_candidate_tree: str, confirmation: str) -> dict[str, str]:
    """Advance only the explicitly named disposable integration worktree.

    This is intentionally separate from replay. It cannot fetch, merge, create
    a commit, or update a tag/ref other than the current branch's HEAD.
    """
    if confirmation != "I_STAGE_THE_EXACT_CANDIDATE":
        raise ReplayError("stage-candidate requires exact confirmation")
    if expected_branch in {"main", "master"} or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._/-]*", expected_branch):
        raise ReplayError("stage-candidate requires a non-protected integration branch")
    expected_head = _oid(expected_head, "expected head")
    candidate_revision = _oid(candidate_revision, "candidate revision")
    expected_candidate_tree = _oid(expected_candidate_tree, "expected candidate tree")
    if _git(repository, "rev-parse", "--is-inside-work-tree") != "true":
        raise ReplayError("stage target must be a non-bare worktree")
    worktrees = [line for line in _git(repository, "worktree", "list", "--porcelain").splitlines() if line.startswith("worktree ")]
    if len(worktrees) != 1:
        raise ReplayError("stage-candidate refuses repositories with linked worktrees")
    before = _repository_snapshot(repository)
    if before["status"] or before["branch"] != expected_branch or before["head"] != expected_head:
        raise ReplayError("stage target branch, HEAD, and tracked state must exactly match")
    if _git(repository, "cat-file", "-t", candidate_revision) != "commit":
        raise ReplayError("candidate revision must resolve to a commit object")
    candidate_tree = _git(repository, "rev-parse", f"{candidate_revision}^{{tree}}")
    if candidate_tree != expected_candidate_tree:
        raise ReplayError("candidate tree does not match its lock")
    _git(repository, "reset", "--hard", candidate_revision)
    after = _repository_snapshot(repository)
    if after["branch"] != expected_branch or after["head"] != candidate_revision or _git(repository, "rev-parse", "HEAD^{tree}") != expected_candidate_tree or after["status"]:
        raise ReplayError("stage-candidate postcondition failed")
    return {"branch": expected_branch, "head": candidate_revision, "tree": expected_candidate_tree}


def apply_patch_unit(worktree: Path, unit_id: str, patch: Path, expected_patch_sha256: str,
                     expected_before_tree: str, expected_after_tree: str) -> dict[str, str]:
    """Apply one reviewed patch only when both Git-tree boundaries match.

    The caller must supply an owned disposable worktree. A failed `git apply`
    or postcondition restores its original HEAD tree, so the worktree never
    represents a partly accepted candidate.
    """
    if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", unit_id):
        raise ReplayError("unit ID is unsafe")
    expected_before_tree = _oid(expected_before_tree, "expected before tree")
    expected_after_tree = _oid(expected_after_tree, "expected after tree")
    if not re.fullmatch(r"sha256:[0-9a-f]{64}", expected_patch_sha256):
        raise ReplayError("patch digest is invalid")
    try:
        patch_path = patch.resolve(strict=True)
    except OSError as error:
        raise ReplayError("patch does not exist") from error
    if not patch_path.is_file() or patch_path.is_symlink():
        raise ReplayError("patch must be a regular non-symlink file")
    if "sha256:" + hashlib.sha256(patch_path.read_bytes()).hexdigest() != expected_patch_sha256:
        raise ReplayError("patch digest does not match its lock")
    before = _repository_snapshot(worktree)
    current_tree = _git(worktree, "write-tree")
    if not _git_succeeds(worktree, "diff", "--quiet") or (current_tree != expected_before_tree and current_tree != expected_after_tree):
        raise ReplayError("patch unit worktree does not match its locked input")
    if current_tree == expected_after_tree:
        return {"unit": unit_id, "outcome": "already-present", "tree": expected_after_tree}
    try:
        _git(worktree, "apply", "--check", "--index", "--whitespace=error", str(patch_path))
        _git(worktree, "apply", "--index", "--whitespace=error", str(patch_path))
        if _git(worktree, "write-tree") != expected_after_tree:
            raise ReplayError("patch application did not produce its locked tree")
    except ReplayError:
        _git(worktree, "reset", "--hard", before["head"])
        raise
    return {"unit": unit_id, "outcome": "applied", "tree": expected_after_tree}


def apply_commit_unit(worktree: Path, unit_id: str, commit: str, expected_patch_sha256: str,
                      expected_before_tree: str, expected_after_tree: str) -> dict[str, str]:
    """Apply a single-parent commit by its locked binary patch, never cherry-pick."""
    commit = _oid(commit, "commit")
    if _git(worktree, "cat-file", "-t", commit) != "commit":
        raise ReplayError("commit unit reference must resolve to a commit")
    parents = _git(worktree, "show", "-s", "--format=%P", commit).split()
    if len(parents) != 1:
        raise ReplayError("commit unit must have exactly one parent")
    payload = _git_bytes(worktree, "diff", "--binary", "--full-index", parents[0], commit)
    actual = "sha256:" + hashlib.sha256(payload).hexdigest()
    if actual != expected_patch_sha256:
        raise ReplayError("commit patch does not match its lock")
    with tempfile.NamedTemporaryFile(prefix="roots-replay-", suffix=".patch", delete=False) as output:
        output.write(payload)
        patch = Path(output.name)
    try:
        return apply_patch_unit(worktree, unit_id, patch, actual, expected_before_tree, expected_after_tree)
    finally:
        patch.unlink(missing_ok=True)


def manual_unit(unit_id: str, reason: str) -> dict[str, str]:
    """Record a manual boundary without modifying a candidate."""
    if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", unit_id) or not reason or "\n" in reason:
        raise ReplayError("manual unit requires a bounded ID and reason")
    return {"unit": unit_id, "outcome": "manual", "reason": reason}


def absorbed_unit(worktree: Path, unit_id: str, expected_tree: str, evidence: str) -> dict[str, str]:
    """Accept an absorbed outcome only from an explicit reviewed tree lock."""
    if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", unit_id) or not evidence or "\n" in evidence:
        raise ReplayError("absorbed unit requires a bounded ID and evidence")
    expected_tree = _oid(expected_tree, "absorbed tree")
    if _repository_snapshot(worktree)["status"] or _git(worktree, "rev-parse", "HEAD^{tree}") != expected_tree:
        raise ReplayError("absorbed unit tree does not match its reviewed lock")
    return {"unit": unit_id, "outcome": "absorbed", "tree": expected_tree}


def apply_data_unit(worktree: Path, unit_id: str, source: Path, destination: str,
                    expected_sha256: str, expected_before_tree: str, expected_after_tree: str) -> dict[str, str]:
    """Copy one reviewed data input, with content and tree locks on both sides."""
    if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", unit_id) or not re.fullmatch(r"sha256:[0-9a-f]{64}", expected_sha256):
        raise ReplayError("data unit lock is invalid")
    relative = Path(destination)
    if relative.is_absolute() or ".." in relative.parts or not destination or "\\" in destination:
        raise ReplayError("data destination is unsafe")
    try:
        source_path = source.resolve(strict=True)
    except OSError as error:
        raise ReplayError("data source does not exist") from error
    if not source_path.is_file() or source_path.is_symlink() or "sha256:" + hashlib.sha256(source_path.read_bytes()).hexdigest() != expected_sha256:
        raise ReplayError("data source does not match its lock")
    expected_before_tree = _oid(expected_before_tree, "expected before tree")
    expected_after_tree = _oid(expected_after_tree, "expected after tree")
    target = worktree / relative
    if target.exists() and target.is_symlink() or any(part.is_symlink() for part in [worktree, *target.parents] if part.exists()):
        raise ReplayError("data destination traverses a symlink")
    before = _repository_snapshot(worktree)
    if not _git_succeeds(worktree, "diff", "--quiet") or _git(worktree, "write-tree") != expected_before_tree:
        raise ReplayError("data unit worktree does not match its locked input")
    try:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source_path.read_bytes())
        _git(worktree, "add", "--", destination)
        if _git(worktree, "write-tree") != expected_after_tree:
            raise ReplayError("data application did not produce its locked tree")
    except (OSError, ReplayError):
        _git(worktree, "reset", "--hard", before["head"])
        raise
    return {"unit": unit_id, "outcome": "applied", "tree": expected_after_tree}


def apply_generator_unit(worktree: Path, unit_id: str, generator: str, source: str, destination: str,
                         expected_source_sha256: str, expected_output_sha256: str,
                         expected_before_tree: str, expected_after_tree: str) -> dict[str, str]:
    """Run one in-process allowlisted deterministic generator.

    No candidate executable or maintainer-provided command line is invoked.
    The initial allowlist is intentionally narrow: normalizing CRLF input into
    a declared generated output. New generators require a code review entry,
    not a command-line escape hatch.
    """
    if generator != "normalize-lf":
        raise ReplayError("generator is not allowlisted")
    if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", unit_id):
        raise ReplayError("unit ID is unsafe")
    for name, value in (("source", source), ("destination", destination)):
        item = Path(value)
        if item.is_absolute() or ".." in item.parts or not value or "\\" in value:
            raise ReplayError(f"generator {name} is unsafe")
    if not re.fullmatch(r"sha256:[0-9a-f]{64}", expected_source_sha256) or not re.fullmatch(r"sha256:[0-9a-f]{64}", expected_output_sha256):
        raise ReplayError("generator content lock is invalid")
    expected_before_tree = _oid(expected_before_tree, "expected before tree")
    expected_after_tree = _oid(expected_after_tree, "expected after tree")
    source_path, target = worktree / source, worktree / destination
    if not source_path.is_file() or source_path.is_symlink() or (target.exists() and target.is_symlink()):
        raise ReplayError("generator input or output is not a regular file")
    source_bytes = source_path.read_bytes()
    if "sha256:" + hashlib.sha256(source_bytes).hexdigest() != expected_source_sha256:
        raise ReplayError("generator input does not match its lock")
    output = source_bytes.replace(b"\r\n", b"\n")
    if "sha256:" + hashlib.sha256(output).hexdigest() != expected_output_sha256:
        raise ReplayError("generator output does not match its lock")
    before = _repository_snapshot(worktree)
    if not _git_succeeds(worktree, "diff", "--quiet") or _git(worktree, "write-tree") != expected_before_tree:
        raise ReplayError("generator worktree does not match its locked input")
    try:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(output)
        _git(worktree, "add", "--", destination)
        if _git(worktree, "write-tree") != expected_after_tree:
            raise ReplayError("generator did not produce its locked tree")
    except (OSError, ReplayError):
        _git(worktree, "reset", "--hard", before["head"])
        raise
    return {"unit": unit_id, "outcome": "applied", "tree": expected_after_tree}


def apply_typed_unit(worktree: Path, unit: dict[str, Any]) -> dict[str, str]:
    """Dispatch only a complete mechanism record; unknown fields fail closed."""
    mechanism = unit.get("mechanism") if isinstance(unit, dict) else None
    if mechanism == "manual" and set(unit) == {"id", "mechanism", "reason"}:
        return manual_unit(unit["id"], unit["reason"])
    if mechanism == "absorbed" and set(unit) == {"id", "mechanism", "expected_tree", "evidence"}:
        return absorbed_unit(worktree, unit["id"], unit["expected_tree"], unit["evidence"])
    if mechanism == "patch" and set(unit) == {"id", "mechanism", "patch", "expected_patch_sha256", "expected_before_tree", "expected_after_tree"}:
        return apply_patch_unit(worktree, unit["id"], Path(unit["patch"]), unit["expected_patch_sha256"], unit["expected_before_tree"], unit["expected_after_tree"])
    if mechanism == "module/data" and set(unit) == {"id", "mechanism", "source", "destination", "expected_sha256", "expected_before_tree", "expected_after_tree"}:
        return apply_data_unit(worktree, unit["id"], Path(unit["source"]), unit["destination"], unit["expected_sha256"], unit["expected_before_tree"], unit["expected_after_tree"])
    if mechanism == "commit" and set(unit) == {"id", "mechanism", "commit", "expected_patch_sha256", "expected_before_tree", "expected_after_tree"}:
        return apply_commit_unit(worktree, unit["id"], unit["commit"], unit["expected_patch_sha256"], unit["expected_before_tree"], unit["expected_after_tree"])
    if mechanism == "generator" and set(unit) == {"id", "mechanism", "generator", "source", "destination", "expected_source_sha256", "expected_output_sha256", "expected_before_tree", "expected_after_tree"}:
        return apply_generator_unit(worktree, unit["id"], unit["generator"], unit["source"], unit["destination"], unit["expected_source_sha256"], unit["expected_output_sha256"], unit["expected_before_tree"], unit["expected_after_tree"])
    raise ReplayError("unsupported or incomplete typed application unit")


def sequence_units(units: list[dict[str, Any]]) -> list[str]:
    """Validate deterministic dependency order before any handler is invoked."""
    completed: set[str] = set()
    ordered: list[str] = []
    for unit in units:
        if not isinstance(unit, dict) or not isinstance(unit.get("id"), str) or not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", unit["id"]):
            raise ReplayError("sequence contains an unsafe unit ID")
        dependencies = unit.get("dependencies", [])
        if not isinstance(dependencies, list) or any(not isinstance(item, str) for item in dependencies) or unit["id"] in completed or any(item not in completed for item in dependencies):
            raise ReplayError("sequence is duplicate, cyclic, or out of order")
        completed.add(unit["id"])
        ordered.append(unit["id"])
    return ordered


def _state_outcome(outcome: dict[str, str], unit: dict[str, Any]) -> dict[str, Any]:
    """Retain reviewable typed evidence without retaining host-local paths."""
    evidence: dict[str, Any] = {"mechanism": unit["mechanism"]}
    for key in ("expected_patch_sha256", "expected_sha256", "expected_source_sha256", "expected_output_sha256", "expected_before_tree", "expected_after_tree", "generator", "destination", "reason", "evidence"):
        if key in unit:
            evidence[key] = unit[key]
    return {"unit": outcome["unit"], "outcome": outcome["outcome"], "tree": outcome.get("tree", _oid(_git(Path("."), "write-tree"), "tree")), "evidence": evidence}


def replay_units(worktree: Path, units: list[dict[str, Any]], input_lock: dict[str, Any],
                 manifest_digest: str, state_directory: Path) -> list[dict[str, str]]:
    """Apply an ordered list and durably snapshot every completed boundary."""
    order = sequence_units(units)
    directory = _directory(str(state_directory), "run state directory")
    initial_head = _git(worktree, "rev-parse", "HEAD")
    outcomes: list[dict[str, str]] = []
    try:
        for index, unit in enumerate(units, start=1):
            outcome = apply_typed_unit(worktree, {key: value for key, value in unit.items() if key != "dependencies"})
            tree = outcome.get("tree", _git(worktree, "write-tree"))
            outcomes.append({"unit": outcome["unit"], "outcome": outcome["outcome"], "tree": tree, "evidence": _state_outcome({**outcome, "tree": tree}, unit)["evidence"]})
            state = make_run_state(input_lock, manifest_digest, order, outcomes, _git(worktree, "write-tree"))
            write_run_state(directory / f"replay-state-{index:04}.json", state)
    except ReplayError:
        _git(worktree, "reset", "--hard", initial_head)
        raise
    return outcomes


def resume_units(worktree: Path, units: list[dict[str, Any]], input_lock: dict[str, Any],
                 manifest_digest: str, state_path: Path, state_directory: Path) -> list[dict[str, str]]:
    """Continue only the suffix following an exact durable state prefix."""
    order = sequence_units(units)
    state = read_run_state(state_path)
    current_tree = _git(worktree, "write-tree")
    validate_resume(state, input_lock, manifest_digest, current_tree)
    completed = state["completed_units"]
    if [item["unit"] for item in completed] != order[:len(completed)]:
        raise ReplayError("resume state is not an ordered unit prefix")
    directory = _directory(str(state_directory), "run state directory")
    initial_head = _git(worktree, "rev-parse", "HEAD")
    outcomes = list(completed)
    try:
        for index, unit in enumerate(units[len(completed):], start=len(completed) + 1):
            outcome = apply_typed_unit(worktree, {key: value for key, value in unit.items() if key != "dependencies"})
            tree = outcome.get("tree", _git(worktree, "write-tree"))
            outcomes.append({"unit": outcome["unit"], "outcome": outcome["outcome"], "tree": tree, "evidence": _state_outcome({**outcome, "tree": tree}, unit)["evidence"]})
            next_state = make_run_state(input_lock, manifest_digest, order, outcomes, _git(worktree, "write-tree"))
            write_run_state(directory / f"replay-state-{index:04}.json", next_state)
    except ReplayError:
        _git(worktree, "reset", "--hard", initial_head)
        raise
    return outcomes


def make_run_state(input_lock: dict[str, Any], manifest_digest: str, ordered_units: list[str],
                   completed_units: list[dict[str, str]], candidate_tree: str) -> dict[str, Any]:
    """Build the deterministic, content-addressed boundary for resume."""
    if not isinstance(input_lock, dict) or not re.fullmatch(r"sha256:[0-9a-f]{64}", manifest_digest):
        raise ReplayError("run state input lock is invalid")
    if not ordered_units or len(ordered_units) != len(set(ordered_units)) or any(not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", item) for item in ordered_units):
        raise ReplayError("run state order is invalid")
    if any(not isinstance(item, dict) or set(item) not in ({"unit", "outcome", "tree"}, {"unit", "outcome", "tree", "evidence"}) or item["unit"] not in ordered_units or item["outcome"] not in {"applied", "already-present", "absorbed", "manual"} or not isinstance(item["tree"], str) for item in completed_units):
        raise ReplayError("run state outcomes are invalid")
    _oid(candidate_tree, "candidate tree")
    value = {"schema_version": 1, "tool_version": TOOL_VERSION, "environment": REPLAY_ENVIRONMENT,
             "input_lock": input_lock, "manifest_digest": manifest_digest, "ordered_units": ordered_units,
             "completed_units": completed_units, "candidate_tree": candidate_tree}
    value["digest"] = digest(value)
    return value


def validate_resume(state: Any, input_lock: dict[str, Any], manifest_digest: str, candidate_tree: str) -> None:
    if not isinstance(state, dict) or set(state) != {"schema_version", "tool_version", "environment", "input_lock", "manifest_digest", "ordered_units", "completed_units", "candidate_tree", "digest"}:
        raise ReplayError("run state fields are invalid")
    if state["schema_version"] != 1 or state["tool_version"] != TOOL_VERSION or state["environment"] != REPLAY_ENVIRONMENT:
        raise ReplayError("run state tool or environment is incompatible")
    expected = make_run_state(state["input_lock"], state["manifest_digest"], state["ordered_units"], state["completed_units"], state["candidate_tree"])
    if state != expected:
        raise ReplayError("run state digest is invalid")
    if state["input_lock"] != input_lock or state["manifest_digest"] != manifest_digest or state["candidate_tree"] != candidate_tree:
        raise ReplayError("resume inputs or candidate tree drifted")


def write_run_state(path: Path, state: dict[str, Any]) -> None:
    if not path.is_absolute() or not re.fullmatch(r"replay-state(?:-[0-9]{4})?\.json", path.name):
        raise ReplayError("run state path must be an absolute replay-state JSON path")
    validate_resume(state, state["input_lock"], state["manifest_digest"], state["candidate_tree"])
    parent = _directory(str(path.parent), "run state directory")
    destination = parent / path.name
    if destination.exists() or destination.is_symlink():
        raise ReplayError("refusing to replace an existing run state")
    try:
        descriptor = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as output:
            output.write(canonical_json(state) + b"\n")
    except OSError as error:
        raise ReplayError("cannot write run state") from error


def read_run_state(path: Path) -> dict[str, Any]:
    value = _safe_json_file(path, path.name, "run state") if path.is_absolute() and re.fullmatch(r"replay-state(?:-[0-9]{4})?\.json", path.name) else None
    if not isinstance(value, dict):
        raise ReplayError("run state must be an object")
    return value


def inspect_run(path: Path) -> dict[str, Any]:
    """Return deterministic, path-redacted state suitable for review output."""
    state = read_run_state(path)
    validate_resume(state, state["input_lock"], state["manifest_digest"], state["candidate_tree"])
    return {
        "schema_version": 1,
        "state_digest": state["digest"],
        "manifest_digest": state["manifest_digest"],
        "candidate_tree": state["candidate_tree"],
        "ordered_units": state["ordered_units"],
        "completed_units": state["completed_units"],
    }


def abandon_run(path: Path, output: Path) -> dict[str, str]:
    """Retain immutable evidence and write a separate deterministic abandon marker."""
    summary = inspect_run(path)
    if not output.is_absolute() or output.name != "replay-abandoned.json":
        raise ReplayError("abandon output must be an absolute replay-abandoned.json path")
    parent = _directory(str(output.parent), "abandon output directory")
    destination = parent / output.name
    if destination.exists() or destination.is_symlink():
        raise ReplayError("refusing to replace an existing abandon marker")
    value = {"schema_version": 1, "action": "abandoned", "state_digest": summary["state_digest"], "candidate_tree": summary["candidate_tree"]}
    try:
        descriptor = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(canonical_json(value) + b"\n")
    except OSError as error:
        raise ReplayError("cannot write abandon marker") from error
    return value


def _write_bundle_file(path: Path, value: bytes) -> None:
    if path.exists() or path.is_symlink():
        raise ReplayError("refusing to replace review output")
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(value)
    except OSError as error:
        raise ReplayError("cannot write review output") from error


def review_bundle(state_path: Path, output_directory: Path) -> dict[str, Any]:
    """Write stable path-free JSON and text evidence for a completed boundary."""
    summary = inspect_run(state_path)
    directory = _directory(str(output_directory), "review output directory")
    completed = summary["completed_units"]
    evidence = [item.get("evidence", {}) for item in completed]
    value = {
        "schema_version": 1,
        "tool": "roots-replay",
        "tool_version": TOOL_VERSION,
        "input": {key: summary_value for key, summary_value in read_run_state(state_path)["input_lock"].items() if key != "repository"},
        "manifest_digest": summary["manifest_digest"],
        "ordered_units": summary["ordered_units"],
        "outcomes": completed,
        "patch_ids": [item["expected_patch_sha256"] for item in evidence if "expected_patch_sha256" in item],
        "blob_ids": [item[key] for item in evidence for key in ("expected_sha256", "expected_source_sha256", "expected_output_sha256") if key in item],
        "rewritten_hunks": [],
        "generators": [item["generator"] for item in evidence if "generator" in item],
        "candidate_tree": summary["candidate_tree"],
        "state_digest": summary["state_digest"],
        "risk_gate": "manual-review-required",
        "next_commands": ["inspect replay state", "perform semantic review before staging"],
    }
    value["digest"] = digest(value)
    json_path = directory / "replay-review.json"
    text_path = directory / "replay-review.txt"
    text = "Roots replay review\nstate: {state_digest}\ncandidate-tree: {candidate_tree}\nmanifest: {manifest_digest}\noutcomes: {outcomes}\nrisk-gate: manual-review-required\n".format(
        state_digest=value["state_digest"], candidate_tree=value["candidate_tree"],
        manifest_digest=value["manifest_digest"], outcomes=",".join(item["outcome"] for item in value["outcomes"]))
    _write_bundle_file(json_path, canonical_json(value) + b"\n")
    try:
        _write_bundle_file(text_path, text.encode("utf-8"))
    except ReplayError:
        # A partial bundle is not reviewable; remove only the file created by
        # this invocation, never a pre-existing caller file.
        json_path.unlink(missing_ok=True)
        raise
    return {"digest": value["digest"], "json": json_path.name, "text": text_path.name}


def export_patch_series(worktree: Path, state_path: Path, output: Path) -> dict[str, str]:
    """Create one deterministic generated git-format-patch compatible mbox."""
    state = read_run_state(state_path)
    validate_resume(state, state["input_lock"], state["manifest_digest"], _git(worktree, "write-tree"))
    if not output.is_absolute() or output.name != "replay-generated-series.patch":
        raise ReplayError("patch export must be an absolute replay-generated-series.patch path")
    parent = _directory(str(output.parent), "patch export directory")
    destination = parent / output.name
    base = state["input_lock"].get("revision")
    if not isinstance(base, str):
        raise ReplayError("run state input revision is invalid")
    # Handlers deliberately leave an index/tree candidate rather than making
    # maintainer-visible commits. Build one deterministic unreachable commit
    # object solely to feed format-patch; no ref, branch, or HEAD changes.
    environment = {"PATH": "/usr/bin:/bin", "HOME": tempfile.gettempdir() + "/roots-replay-empty-home",
                   "GIT_CONFIG_NOSYSTEM": "1", "GIT_NO_REPLACE_OBJECTS": "1", **REPLAY_ENVIRONMENT,
                   "GIT_AUTHOR_NAME": "Roots Replay", "GIT_AUTHOR_EMAIL": "replay@invalid",
                   "GIT_COMMITTER_NAME": "Roots Replay", "GIT_COMMITTER_EMAIL": "replay@invalid",
                   "GIT_AUTHOR_DATE": "2000-01-01T00:00:00Z", "GIT_COMMITTER_DATE": "2000-01-01T00:00:00Z"}
    commit = subprocess.run(["git", "-C", str(worktree), "commit-tree", state["candidate_tree"], "-p", base], input=b"Roots replay generated candidate\n", check=False, capture_output=True, env=environment).stdout.decode("ascii", "strict").strip()
    _oid(commit, "generated export commit")
    payload = _git_bytes(worktree, "format-patch", "--stdout", "--no-stat", "--zero-commit", "--subject-prefix=ROOTS-REPLAY-GENERATED", f"{base}..{commit}")
    _write_bundle_file(destination, payload)
    return {"file": destination.name, "sha256": "sha256:" + hashlib.sha256(payload).hexdigest()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("verify", "plan", "replay", "resume", "inspect", "abandon", "report", "export-patches", "stage-candidate"):
        command = commands.add_parser(name)
        if name not in {"inspect", "abandon", "report"}:
            command.add_argument("--repository", required=True)
        if name in {"verify", "plan", "replay", "resume"}:
            command.add_argument("--revision", required=True)
            command.add_argument("--expected-tree", required=True)
        if name == "plan":
            command.add_argument("--output", required=True)
        if name == "replay":
            command.add_argument("--manifest", required=True)
            command.add_argument("--materials", required=True)
            command.add_argument("--materials-root", required=True)
            command.add_argument("--state-directory", required=True)
            command.add_argument("--apply", action="store_true")
        if name == "resume":
            command.add_argument("--manifest", required=True)
            command.add_argument("--materials", required=True)
            command.add_argument("--materials-root", required=True)
            command.add_argument("--state", required=True)
            command.add_argument("--state-directory", required=True)
        if name in {"inspect", "report", "export-patches", "abandon"}:
            command.add_argument("--state", required=True)
        if name == "abandon":
            command.add_argument("--output", required=True)
        if name == "report":
            command.add_argument("--output-directory", required=True)
        if name == "export-patches":
            command.add_argument("--output", required=True)
        if name == "stage-candidate":
            command.add_argument("--integration-branch", required=True)
            command.add_argument("--expected-head", required=True)
            command.add_argument("--candidate-revision", required=True)
            command.add_argument("--expected-candidate-tree", required=True)
            command.add_argument("--confirm", required=True)
    args = parser.parse_args()
    try:
        repository = _directory(args.repository, "repository") if hasattr(args, "repository") else None
        if args.command == "verify":
            assert repository
            print(json.dumps(verify(repository, args.revision, args.expected_tree), ensure_ascii=False, sort_keys=True, separators=(",", ":")))
        elif args.command == "plan":
            assert repository
            value = plan(repository, args.revision, args.expected_tree, Path(args.output))
            print(value["digest"])
        elif args.command == "stage-candidate":
            assert repository
            print(json.dumps(stage_candidate(repository, args.integration_branch, args.expected_head, args.candidate_revision, args.expected_candidate_tree, args.confirm), sort_keys=True, separators=(",", ":")))
        elif args.command == "replay":
            assert repository
            manifest_units = read_manifest_units(Path(args.manifest))
            materials = read_replay_materials(Path(args.materials), manifest_units)
            units = materialize_units(manifest_units, materials, _directory(args.materials_root, "materials root"))
            # Preflight is intentionally the default: validate the source and
            # ordered manifest without creating a candidate.
            if not args.apply:
                verify(repository, args.revision, args.expected_tree)
                print(json.dumps({"mode": "preflight", "manifest_digest": manifest_lock(units), "materials_digest": digest(materials)}, sort_keys=True, separators=(",", ":")))
                return 0
            candidate, outcomes = replay_owned(repository, args.revision, args.expected_tree, units, Path(args.state_directory), digest({"manifest": json.loads(Path(args.manifest).read_text(encoding="utf-8")), "materials": materials}))
            print(json.dumps({"candidate": candidate.name, "outcomes": outcomes}, sort_keys=True, separators=(",", ":")))
        elif args.command == "resume":
            assert repository
            manifest_units = read_manifest_units(Path(args.manifest))
            materials = read_replay_materials(Path(args.materials), manifest_units)
            units = materialize_units(manifest_units, materials, _directory(args.materials_root, "materials root"))
            candidate, outcomes = resume_owned(repository, args.revision, args.expected_tree, units, Path(args.state), Path(args.state_directory), digest({"manifest": json.loads(Path(args.manifest).read_text(encoding="utf-8")), "materials": materials}))
            print(json.dumps({"candidate": candidate.name, "outcomes": outcomes}, sort_keys=True, separators=(",", ":")))
        elif args.command == "inspect":
            print(json.dumps(inspect_run(Path(args.state)), sort_keys=True, separators=(",", ":")))
        elif args.command == "abandon":
            print(json.dumps(abandon_run(Path(args.state), Path(args.output)), sort_keys=True, separators=(",", ":")))
        elif args.command == "report":
            print(json.dumps(review_bundle(Path(args.state), Path(args.output_directory)), sort_keys=True, separators=(",", ":")))
        elif args.command == "export-patches":
            assert repository
            print(json.dumps(export_patch_series(repository, Path(args.state), Path(args.output)), sort_keys=True, separators=(",", ":")))
    except ReplayError as error:
        print(f"roots-replay: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
