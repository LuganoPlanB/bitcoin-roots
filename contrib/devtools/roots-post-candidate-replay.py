#!/usr/bin/env python3
"""Construct the locked post-candidate infrastructure tree with an isolated index."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Any


ROOT_COMMIT = "cbc88cff9b35b95a549c0313e424e13093fcd6a1"
ROOT_TREE = "39a5e30207a09962e78ae81c24cc65b1e478ef90"
SOURCE_COMMIT = "e2387f0d975121869064e55eb0afb99e7639120b"
CLOSURE_PREFIXES = ("contrib/roots/", "contrib/devtools/roots")
CLOSURE_TEST_PREFIX = "ci/test/test_roots_"
OVERLAY_PATH = "contrib/roots/replay-29.4-proposal/post-candidate-production-overlay.patch"
OVERLAY_PATHS = (
    "ci/roots-trusted-replay-gate.py",
    "ci/test/test_roots_lineage.py",
    "ci/test/test_roots_llm_contract.py",
    "ci/test/test_roots_maintainer_runbook.py",
    "ci/test/test_roots_pr_gate.py",
    "ci/test/test_roots_replay.py",
    "contrib/devtools/roots-methodology.py",
    "contrib/roots/construct-canonical-29.4.bash",
    "contrib/roots/create-local-integration-baseline.bash",
    "contrib/roots/llm-execution-contract-v1.json",
    "contrib/roots/local-integration-baseline-29.4.json",
    "contrib/roots/maintainer-runbook-review.json",
    "contrib/roots/maintainer-runbook.md",
    "contrib/roots/method-retrospective-29.4.json",
    "contrib/roots/methodology-v1.json",
    "contrib/roots/promotion-29.4.json",
    "contrib/roots/replay-29.4-proposal/acceptance-evidence.json",
    "contrib/roots/replay-29.4-proposal/canonical-lineage.bash",
    "contrib/roots/replay-29.4-proposal/lineage-row-29.4.json",
    "contrib/roots/validate-local-integration-baseline.py",
)
OBSOLETE_PATHS = (
    "ci/test/test_roots_hardening_handoff.py",
    "contrib/roots/replay-29.4-proposal/incremental-oracle.bash",
)


class ReplayError(ValueError):
    pass


def canonical(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True).encode() + b"\n"


def digest(value: bytes) -> str:
    return "sha256:" + hashlib.sha256(value).hexdigest()


def git(repository: Path, *arguments: str, env: dict[str, str] | None = None, input_data: bytes | None = None) -> bytes:
    result = subprocess.run(["git", "-C", str(repository), *arguments], input=input_data, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, check=False)
    if result.returncode:
        raise ReplayError("git command failed: " + " ".join(arguments))
    return result.stdout


def text(repository: Path, *arguments: str, env: dict[str, str] | None = None) -> str:
    return git(repository, *arguments, env=env).decode("utf-8", "strict").strip()


def sha1(value: str) -> str:
    if len(value) != 40 or any(character not in "0123456789abcdef" for character in value):
        raise ReplayError("identity is not a full immutable SHA-1")
    return value


def object_at(repository: Path, commit: str) -> tuple[str, str]:
    sha1(commit)
    if text(repository, "rev-parse", "--verify", commit + "^{commit}") != commit:
        raise ReplayError("locked commit is unavailable")
    return commit, text(repository, "rev-parse", commit + "^{tree}")


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8", errors="strict"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ReplayError("cannot read JSON") from error
    if not isinstance(value, dict):
        raise ReplayError("JSON record must be an object")
    return value


def inventory_paths(inventory: dict[str, Any]) -> list[str]:
    if inventory.get("schema_version") != 1 or not isinstance(inventory.get("atoms"), list):
        raise ReplayError("inventory schema is unsupported")
    paths = {
        atom.get("path")
        for atom in inventory["atoms"]
        if isinstance(atom, dict) and atom.get("disposition") in {"unchanged", "adapted", "absorbed", "generated", "manual"}
    }
    if not paths or any(not isinstance(path, str) or not path or path.startswith("/") or ".." in path.split("/") for path in paths):
        raise ReplayError("inventory has an unsafe or empty retained path set")
    return sorted(path for path in paths if path not in OBSOLETE_PATHS)


def build_patch(repository: Path, candidate: str, source: str, paths: list[str]) -> bytes:
    return git(repository, "diff", "--binary", "--full-index", "--no-renames", candidate, source, "--", *paths)


def dependency_closure_paths(repository: Path, source: str) -> list[str]:
    paths = git(repository, "ls-tree", "-r", "--name-only", source).decode().splitlines()
    retained = [
        path for path in paths
        if path.startswith(CLOSURE_PREFIXES)
        or (path.startswith(CLOSURE_TEST_PREFIX) and path.endswith(".py"))
    ]
    if not retained:
        raise ReplayError("dependency closure is empty")
    return [path for path in retained if path not in OBSOLETE_PATHS]


def apply_patch(repository: Path, candidate: str, patch: bytes, *, remove_obsolete: bool = False) -> str:
    if not patch:
        raise ReplayError("locked patch is vacuous")
    with tempfile.TemporaryDirectory(prefix="roots-post-candidate-index-") as temporary:
        index = str(Path(temporary) / "index")
        environment = {"GIT_INDEX_FILE": index}
        git(repository, "read-tree", candidate, env=environment)
        git(repository, "apply", "--cached", "--whitespace=nowarn", "-", env=environment, input_data=patch)
        if remove_obsolete:
            git(repository, "rm", "--cached", "--ignore-unmatch", "--", *OBSOLETE_PATHS, env=environment)
        return text(repository, "write-tree", env=environment)


def overlay_patch(root: Path) -> bytes:
    path = root / OVERLAY_PATH
    try:
        value = path.read_bytes()
    except OSError as error:
        raise ReplayError("production overlay is unavailable") from error
    if not value:
        raise ReplayError("production overlay is vacuous")
    return value


def changed_paths(repository: Path, before: str, after: str) -> list[str]:
    return git(repository, "diff", "--name-only", "--no-renames", before, after).decode("utf-8", "strict").splitlines()


def build_manifest(repository: Path, inventory_path: Path, root: Path) -> dict[str, Any]:
    inventory_bytes = inventory_path.read_bytes()
    inventory = read_json(inventory_path)
    paths = sorted(set(inventory_paths(inventory)) | set(dependency_closure_paths(repository, SOURCE_COMMIT)) | set(OVERLAY_PATHS))
    candidate, candidate_tree = object_at(repository, ROOT_COMMIT)
    source, source_tree = object_at(repository, SOURCE_COMMIT)
    if candidate_tree != ROOT_TREE:
        raise ReplayError("canonical candidate tree is not accepted")
    source_patch = build_patch(repository, candidate, source, paths)
    tree = apply_patch(repository, candidate, source_patch, remove_obsolete=True)
    patch = build_patch(repository, candidate, tree, paths)
    overlay = overlay_patch(root)
    final_tree = apply_patch(repository, tree, overlay)
    if tuple(changed_paths(repository, tree, final_tree)) != OVERLAY_PATHS:
        raise ReplayError("production overlay paths are not exact")
    if final_tree == tree:
        raise ReplayError("production overlay result is vacuous")
    return {
        "candidate": {"commit": "sha1:" + candidate, "tree": "sha1:" + candidate_tree},
        "handler": {
            "kind": "locked-git-diff-path-set-with-production-overlay",
            "overlay": {
                "base_tree": "sha1:" + tree,
                "patch_path": OVERLAY_PATH,
                "patch_sha256": digest(overlay),
                "paths": list(OVERLAY_PATHS),
                "result_tree": "sha1:" + final_tree,
            },
            "patch_sha256": digest(patch),
            "paths": paths,
            "result_tree": "sha1:" + final_tree,
        },
        "inventory_sha256": digest(inventory_bytes),
        "schema_version": 1,
        "source": {"commit": "sha1:" + source, "tree": "sha1:" + source_tree},
    }


def validate_manifest(value: dict[str, Any]) -> None:
    if set(value) != {"candidate", "handler", "inventory_sha256", "schema_version", "source"} or value.get("schema_version") != 1:
        raise ReplayError("manifest schema is not exact")
    candidate, source, handler = value["candidate"], value["source"], value["handler"]
    if not isinstance(candidate, dict) or candidate != {"commit": "sha1:" + ROOT_COMMIT, "tree": "sha1:" + ROOT_TREE}:
        raise ReplayError("manifest candidate is mutable or stale")
    if not isinstance(source, dict) or set(source) != {"commit", "tree"} or source["commit"] != "sha1:" + SOURCE_COMMIT:
        raise ReplayError("manifest source is mutable or stale")
    if not isinstance(handler, dict) or set(handler) != {"kind", "overlay", "patch_sha256", "paths", "result_tree"} or handler["kind"] != "locked-git-diff-path-set-with-production-overlay":
        raise ReplayError("manifest handler is unsupported")
    if not isinstance(value["inventory_sha256"], str) or not value["inventory_sha256"].startswith("sha256:") or not isinstance(handler["patch_sha256"], str) or not handler["patch_sha256"].startswith("sha256:"):
        raise ReplayError("manifest digest is invalid")
    paths = handler["paths"]
    if not isinstance(paths, list) or paths != sorted(set(paths)) or not paths or any(not isinstance(path, str) or path.startswith("/") or ".." in path.split("/") for path in paths):
        raise ReplayError("manifest paths are unsafe or unordered")
    overlay = handler["overlay"]
    if not isinstance(overlay, dict) or set(overlay) != {"base_tree", "patch_path", "patch_sha256", "paths", "result_tree"}:
        raise ReplayError("manifest production overlay schema is not exact")
    if overlay["patch_path"] != OVERLAY_PATH or overlay["paths"] != list(OVERLAY_PATHS):
        raise ReplayError("manifest production overlay scope is not exact")
    for field in ("base_tree", "result_tree"):
        if not isinstance(overlay[field], str) or not overlay[field].startswith("sha1:"):
            raise ReplayError("manifest production overlay tree is invalid")
    if not isinstance(overlay["patch_sha256"], str) or not overlay["patch_sha256"].startswith("sha256:"):
        raise ReplayError("manifest production overlay digest is invalid")


def verify_or_apply(repository: Path, inventory_path: Path, manifest_path: Path, root: Path, output_ref: str | None) -> dict[str, Any]:
    manifest = read_json(manifest_path)
    validate_manifest(manifest)
    expected = build_manifest(repository, inventory_path, root)
    if canonical(manifest) != canonical(expected):
        raise ReplayError("manifest does not match the locked inventory, source, patch, and result")
    candidate = ROOT_COMMIT
    patch = build_patch(repository, candidate, SOURCE_COMMIT, manifest["handler"]["paths"])
    intermediate_tree = apply_patch(repository, candidate, patch, remove_obsolete=True)
    overlay = overlay_patch(root)
    if digest(overlay) != manifest["handler"]["overlay"]["patch_sha256"]:
        raise ReplayError("production overlay digest differs from manifest")
    if "sha1:" + intermediate_tree != manifest["handler"]["overlay"]["base_tree"]:
        raise ReplayError("production overlay base tree differs from manifest")
    result_tree = apply_patch(repository, intermediate_tree, overlay)
    if tuple(changed_paths(repository, intermediate_tree, result_tree)) != OVERLAY_PATHS:
        raise ReplayError("production overlay paths differ from manifest")
    if "sha1:" + result_tree != manifest["handler"]["result_tree"]:
        raise ReplayError("handler result tree differs from manifest")
    if output_ref is not None:
        if not output_ref.startswith("refs/roots/29.4/") or ".." in output_ref or output_ref.endswith("/"):
            raise ReplayError("output ref is not an owned private ref")
        zero = "0" * 40
        result = subprocess.run(["git", "-C", str(repository), "update-ref", output_ref, result_tree, zero], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        if result.returncode:
            raise ReplayError("private output ref already exists or cannot be created")
    return {"decision": "accepted", "patch_sha256": manifest["handler"]["patch_sha256"], "paths": len(manifest["handler"]["paths"]), "result_tree": manifest["handler"]["result_tree"]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("build-manifest", "verify", "apply"))
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output-ref")
    arguments = parser.parse_args()
    try:
        repository = arguments.repository.resolve()
        if arguments.command == "build-manifest":
            if arguments.manifest.exists():
                raise ReplayError("refusing to overwrite manifest")
            arguments.manifest.write_bytes(canonical(build_manifest(repository, arguments.inventory, arguments.root.resolve())))
            report = {"decision": "accepted", "manifest_sha256": digest(arguments.manifest.read_bytes())}
        else:
            report = verify_or_apply(repository, arguments.inventory, arguments.manifest, arguments.root.resolve(), arguments.output_ref if arguments.command == "apply" else None)
        print(json.dumps(report, sort_keys=True))
        return 0
    except ReplayError as error:
        print("roots-post-candidate-replay: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
