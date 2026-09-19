#!/usr/bin/env python3
"""Construct, record, verify, and privately anchor the Roots 29.4 G3 repair."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from typing import Any


CORE_COMMIT = "3fc0865963a38b871e9f7d94e6151c4953563516"
CANONICAL_COMMIT = "cbc88cff9b35b95a549c0313e424e13093fcd6a1"
CANONICAL_TREE = "39a5e30207a09962e78ae81c24cc65b1e478ef90"
G2_COMMIT = "dfc74d403585f7c23815ef80d2e206b85c33919a"
G2_TREE = "477eb9b3f50098b0b8548a9ff35efaa29fd7ecde"
OLD_BOOTSTRAP = "6f85dfb728ca0976658e0fbd26101e7df5e35a14"
ACCEPTED_BOOTSTRAP = "bb977bbd7742650ea1e9d93aa0b6573ee5b14985"
G3_COMMIT = "c8dc2e70bc145930855cf615ba9caffaccdcdcb9"
G3_TREE = "70cd94d5f0ca21ac61720f33ecb86ba115a3b7a9"
G3_REF = "refs/roots/29.4/frozen-production-g3"
G3_SUBJECT = b"fix(ci): use accepted portability bootstrap\n"
G3_DATE = "2026-09-18T00:00:00Z"
WORKFLOW = ".github/workflows/roots-portability.yml"
REGISTRY = "contrib/roots/post-methodology-adaptations.json"
ACCOUNTING = "contrib/roots/continuous-accounting-pr.json"
G2_FREEZE = "contrib/roots/frozen-production-29.4.json"
G3_OWNED_PATHS = (
    "contrib/roots/construct-canonical-29.4.bash",
    "contrib/roots/create-local-integration-baseline.bash",
    "contrib/roots/local-integration-baseline-29.4.json",
    "contrib/roots/promotion-29.4.json",
    "contrib/roots/validate-local-integration-baseline.py",
)
CHANGED_PATHS = (WORKFLOW, ACCOUNTING, REGISTRY)
ARTIFACTS = {
    "replay": "contrib/roots/post-candidate-replay-29.4-g3.json",
    "invariants": "contrib/roots/post-candidate-invariants-29.4-g3.json",
    "accounting": "contrib/roots/production-accounting-29.4-g3.json",
    "review": "contrib/roots/review-export-29.4-g3.json",
    "freeze": "contrib/roots/frozen-production-29.4-g3.json",
    "acceptance": "contrib/roots/acceptance-evidence-29.4-g3.json",
}
HISTORICAL_G2_RUNS = [35340615938, 35340807557, 35340807632, 35340822312, 35340826867]
SHA1 = re.compile(r"^[0-9a-f]{40}$")


class FreezeError(ValueError):
    pass


def canonical(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True).encode("utf-8") + b"\n"


def digest(value: bytes) -> str:
    return "sha256:" + hashlib.sha256(value).hexdigest()


def git_environment(index: Path | None = None) -> dict[str, str]:
    environment = dict(os.environ)
    for key in tuple(environment):
        if key == "GIT_CONFIG_COUNT" or key.startswith(("GIT_CONFIG_KEY_", "GIT_CONFIG_VALUE_")):
            del environment[key]
    environment.update({
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_CONFIG_GLOBAL": os.devnull,
        "GIT_AUTHOR_NAME": "Bitcoin Roots",
        "GIT_AUTHOR_EMAIL": "release@bitcoin-roots.invalid",
        "GIT_AUTHOR_DATE": G3_DATE,
        "GIT_COMMITTER_NAME": "Bitcoin Roots",
        "GIT_COMMITTER_EMAIL": "release@bitcoin-roots.invalid",
        "GIT_COMMITTER_DATE": G3_DATE,
        "GIT_NO_REPLACE_OBJECTS": "1",
        "GIT_OPTIONAL_LOCKS": "0",
        "LC_ALL": "C",
        "TZ": "UTC",
    })
    environment.pop("GIT_ALTERNATE_OBJECT_DIRECTORIES", None)
    if index is not None:
        environment["GIT_INDEX_FILE"] = str(index)
    return environment


def git(repository: Path, *arguments: str, input_data: bytes | None = None, index: Path | None = None, check: bool = True) -> bytes:
    result = subprocess.run(
        [
            "git", "-C", str(repository),
            "-c", "core.hooksPath=/dev/null",
            "-c", "commit.gpgSign=false",
            "-c", "core.abbrev=40",
            "-c", "diff.external=",
            "-c", "diff.algorithm=myers",
            "-c", "diff.renames=false",
            "-c", "diff.indentHeuristic=false",
            "-c", "core.pager=cat",
            "-c", "pager.diff=false",
            "-c", "pager.range-diff=false",
            "-c", "format.useAutoBase=false",
            "-c", "format.signature=",
            *arguments,
        ],
        input=input_data,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=git_environment(index),
        check=False,
    )
    if check and result.returncode:
        raise FreezeError("required Git input is unavailable")
    return result.stdout


def git_text(repository: Path, *arguments: str, **kwargs: Any) -> str:
    return git(repository, *arguments, **kwargs).decode("utf-8", "strict").strip()


def local_config(repository: Path, key: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repository), "config", "--local", "--get", key],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=git_environment(),
        check=False,
    )
    return result.stdout.decode("utf-8", "strict").strip() if result.returncode == 0 else ""


def repository_git_dir(repository: Path) -> Path:
    if not repository.is_dir() or repository.is_symlink():
        raise FreezeError("repository must be a real directory")
    path = Path(git_text(repository, "rev-parse", "--absolute-git-dir"))
    if not path.is_dir() or path.is_symlink():
        raise FreezeError("Git directory must be a real directory")
    return path


def reject_host_state(repository: Path) -> None:
    git_dir = repository_git_dir(repository)
    if os.environ.get("GIT_ALTERNATE_OBJECT_DIRECTORIES") or (git_dir / "objects/info/alternates").exists():
        raise FreezeError("Git alternates are forbidden")
    if (git_dir / "info/grafts").exists() or git_text(repository, "for-each-ref", "--format=%(refname)", "refs/replace"):
        raise FreezeError("Git grafts and replacements are forbidden")
    if (git_dir / "shallow").exists():
        raise FreezeError("shallow repositories are forbidden")
    rerere = local_config(repository, "rerere.enabled")
    if rerere.lower() in {"1", "true", "yes", "on"} or ((git_dir / "rr-cache").is_dir() and any((git_dir / "rr-cache").iterdir())):
        raise FreezeError("Git rerere state is forbidden")
    hooks_path = local_config(repository, "core.hooksPath")
    if hooks_path:
        raise FreezeError("configured Git hooks are forbidden")
    hooks = git_dir / "hooks"
    if hooks.is_dir() and any(path.is_file() and not path.name.endswith(".sample") for path in hooks.iterdir()):
        raise FreezeError("active Git hooks are forbidden")
    status = git(repository, "status", "--porcelain=v1", "-z").decode("utf-8", "strict")
    entries = {entry for entry in status.split("\0") if entry}
    allowed = {"?? " + path for path in ARTIFACTS.values()}
    if entries - allowed:
        raise FreezeError("repository must be clean")


def require_commit(repository: Path, commit: str, tree: str | None = None) -> None:
    if git_text(repository, "cat-file", "-t", commit) != "commit" or git_text(repository, "rev-parse", commit + "^{commit}") != commit:
        raise FreezeError("locked commit is unavailable")
    if tree is not None and git_text(repository, "rev-parse", commit + "^{tree}") != tree:
        raise FreezeError("locked commit tree differs")


def blob(repository: Path, revision: str, path: str) -> bytes:
    return git(repository, "show", revision + ":" + path)


def write_blob(repository: Path, value: bytes) -> str:
    oid = git_text(repository, "hash-object", "-w", "--stdin", input_data=value)
    if not SHA1.fullmatch(oid):
        raise FreezeError("written blob identity is invalid")
    return oid


def load_accounting(root: Path) -> Any:
    path = root / "contrib/devtools/roots-continuous-accounting.py"
    if not path.is_file() or path.is_symlink():
        raise FreezeError("trusted accounting validator is unavailable")
    specification = importlib.util.spec_from_file_location("roots_g3_accounting", path)
    if specification is None or specification.loader is None:
        raise FreezeError("trusted accounting validator cannot be loaded")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def changed_atoms(repository: Path, base: str, head: str) -> set[tuple[str, str, str]]:
    names = git(repository, "diff", "--name-status", "-z", "--no-renames", base, head).split(b"\0")
    result: list[tuple[str, str, str]] = []
    for status, raw_path in zip(names[0::2], names[1::2]):
        if not status or not raw_path:
            continue
        path = raw_path.decode("utf-8", "strict")
        patch = git(repository, "diff", "--no-ext-diff", "--binary", "--full-index", "--unified=0", base, head, "--", path)
        kind = {b"A": "add", b"D": "delete"}.get(status[:1], "modify")
        if b"GIT binary patch" in patch:
            kind = "binary"
        if b"old mode " in patch or b"new mode " in patch:
            mode_lines = b"\n".join(line for line in patch.splitlines() if line.startswith((b"old mode ", b"new mode ")))
            result.append((path, "mode", digest(mode_lines)))
        blocks = [b"@@ " + block for block in patch.split(b"\n@@ ")[1:]]
        if not blocks:
            if kind == "modify" and b"old mode " in patch:
                continue
            blocks = [patch]
        result.extend((path, kind, digest(block)) for block in blocks)
    return set(result)


def transform_workflow(repository: Path) -> bytes:
    value = blob(repository, G2_COMMIT, WORKFLOW).decode("utf-8", "strict")
    old = "          ref: " + OLD_BOOTSTRAP
    new = "          ref: " + ACCEPTED_BOOTSTRAP
    if value.count(old) != 1 or new in value:
        raise FreezeError("G2 portability bootstrap precondition differs")
    return value.replace(old, new).encode("utf-8")


def transform_registry(repository: Path) -> bytes:
    value = json.loads(blob(repository, G2_COMMIT, REGISTRY))
    units = value.get("units") if isinstance(value, dict) else None
    if not isinstance(units, list):
        raise FreezeError("G2 registry is invalid")
    matches = [unit for unit in units if unit.get("id") == "roots-release-history-v1"]
    if len(matches) != 1 or any(path in matches[0].get("paths", []) for path in G3_OWNED_PATHS):
        raise FreezeError("G2 registry ownership precondition differs")
    matches[0]["paths"] = sorted([*matches[0]["paths"], *G3_OWNED_PATHS])
    return canonical(value)


def fresh_accounting_entry(atom: tuple[str, str, str], owner: str) -> dict[str, str]:
    path, kind, atom_digest = atom
    if path not in G3_OWNED_PATHS or owner != "roots-release-history-v1":
        raise FreezeError("accounting metadata cannot be inferred")
    return {
        "adaptation": owner,
        "dependencies": "none",
        "digest": atom_digest,
        "disposition": "update",
        "kind": kind,
        "path": path,
        "provenance": "reviewed",
        "rationale": "specific G3 reconstruction atom",
        "replay_impact": "replay",
        "risk": "low",
        "scope": path,
        "tests": "ci/test/test_roots_pr_gate.py",
    }


def regenerate_accounting(repository: Path, root: Path, provisional: str, registry_bytes: bytes) -> bytes:
    accounting = load_accounting(root)
    observed = changed_atoms(repository, CANONICAL_COMMIT, provisional)
    observed = {atom for atom in observed if atom[0] != ACCOUNTING}
    template = json.loads(blob(repository, G2_COMMIT, ACCOUNTING))
    exact = {(item["path"], item["kind"], item["digest"]): item for item in template["changes"]}
    by_path: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for item in template["changes"]:
        by_path.setdefault((item["path"], item["kind"]), []).append(item)
    manifest = json.loads(blob(repository, G2_COMMIT, "contrib/roots/adaptation-manifest-29.3.json"))
    registry = json.loads(registry_bytes)
    owners = {path: unit["id"] for unit in manifest["units"] for path in unit["touched"]["paths"]}
    owners.update({path: unit["id"] for unit in registry["units"] for path in unit["paths"]})
    changes = []
    for atom in sorted(observed):
        if atom in exact:
            changes.append(exact[atom])
            continue
        candidates = by_path.get(atom[:2], [])
        metadata = {
            json.dumps({key: value for key, value in item.items() if key not in {"path", "kind", "digest"}}, sort_keys=True)
            for item in candidates
        }
        if len(metadata) == 1:
            changes.append({"path": atom[0], "kind": atom[1], "digest": atom[2], **json.loads(next(iter(metadata)))})
            continue
        if atom[0] not in owners:
            raise FreezeError("changed path lacks versioned ownership")
        changes.append(fresh_accounting_entry(atom, owners[atom[0]]))
    record = {"schema_version": 1, "changes": changes}
    accounting.validate(record, observed)
    return canonical(record)


def construct(repository: Path, root: Path) -> tuple[str, str, dict[str, bytes]]:
    reject_host_state(repository)
    for commit, tree in ((CANONICAL_COMMIT, CANONICAL_TREE), (G2_COMMIT, G2_TREE)):
        require_commit(repository, commit, tree)
    require_commit(repository, CORE_COMMIT)
    require_commit(repository, OLD_BOOTSTRAP)
    require_commit(repository, ACCEPTED_BOOTSTRAP)
    if git_text(repository, "show", "-s", "--format=%P", G2_COMMIT) != CANONICAL_COMMIT:
        raise FreezeError("G2 parent differs")
    workflow_bytes = transform_workflow(repository)
    registry_bytes = transform_registry(repository)
    with tempfile.TemporaryDirectory() as temporary:
        index = Path(temporary) / "index"
        git(repository, "read-tree", G2_COMMIT, index=index)
        for path, value in ((WORKFLOW, workflow_bytes), (REGISTRY, registry_bytes)):
            git(repository, "update-index", "--add", "--cacheinfo", "100644," + write_blob(repository, value) + "," + path, index=index)
        provisional_tree = git_text(repository, "write-tree", index=index)
        provisional = git_text(repository, "commit-tree", provisional_tree, "-p", G2_COMMIT, input_data=b"provisional G3 evidence\n")
        accounting_bytes = regenerate_accounting(repository, root, provisional, registry_bytes)
        git(repository, "update-index", "--add", "--cacheinfo", "100644," + write_blob(repository, accounting_bytes) + "," + ACCOUNTING, index=index)
        tree = git_text(repository, "write-tree", index=index)
    commit = git_text(repository, "commit-tree", tree, "-p", G2_COMMIT, input_data=G3_SUBJECT)
    if (commit, tree) != (G3_COMMIT, G3_TREE):
        raise FreezeError("deterministic G3 identity differs")
    if git_text(repository, "show", "-s", "--format=%P", commit) != G2_COMMIT:
        raise FreezeError("G3 parent differs")
    paths = tuple(git_text(repository, "diff-tree", "--no-commit-id", "--name-only", "-r", G2_COMMIT, commit).splitlines())
    if paths != CHANGED_PATHS:
        raise FreezeError("G3 changed paths differ")
    return commit, tree, {WORKFLOW: workflow_bytes, REGISTRY: registry_bytes, ACCOUNTING: accounting_bytes}


def review_material(repository: Path, commit: str) -> dict[str, Any]:
    patch = git(repository, "format-patch", "--stdout", "--no-stat", "--full-index", "--binary", "--no-numbered", "--no-signature", "--no-renames", "--no-ext-diff", "--no-color", "--subject-prefix=PATCH", G2_COMMIT + ".." + commit)
    range_diff = git(repository, "range-diff", "--no-color", "--no-dual-color", "--no-renames", "--no-ext-diff", "--creation-factor=60", CORE_COMMIT + ".." + G2_COMMIT, CORE_COMMIT + ".." + commit)
    scoped = git(repository, "diff", "--no-ext-diff", "--no-color", "--binary", "--full-index", "--no-abbrev", "--no-renames", G2_COMMIT, commit)
    return {
        "advisory": "range-diff is review evidence, not authorization",
        "git_version": git_text(repository, "version"),
        "patch_range": G2_COMMIT + ".." + commit,
        "patch_series_sha256": digest(patch),
        "range_diff_ranges": [CORE_COMMIT + ".." + G2_COMMIT, CORE_COMMIT + ".." + commit],
        "range_diff_sha256": digest(range_diff),
        "schema_version": 1,
        "scoped_diff_sha256": digest(scoped),
    }


def atom_digest(accounting_bytes: bytes) -> tuple[int, str]:
    changes = json.loads(accounting_bytes)["changes"]
    identities = [[item["path"], item["kind"], item["digest"]] for item in changes]
    return len(identities), digest(json.dumps(identities, separators=(",", ":")).encode("utf-8"))


def expected_artifacts(repository: Path, root: Path) -> dict[str, bytes]:
    commit, tree, changed = construct(repository, root)
    workflow_before = blob(repository, G2_COMMIT, WORKFLOW)
    registry_before = blob(repository, G2_COMMIT, REGISTRY)
    accounting_before = blob(repository, G2_COMMIT, ACCOUNTING)
    review = review_material(repository, commit)
    count, atoms = atom_digest(changed[ACCOUNTING])
    g2_freeze_path = root / G2_FREEZE
    if not g2_freeze_path.is_file() or g2_freeze_path.is_symlink():
        raise FreezeError("preserved G2 freeze evidence is unavailable")
    replay = {
        "canonical_base": {"commit": "sha1:" + CANONICAL_COMMIT, "tree": "sha1:" + CANONICAL_TREE},
        "correction": {
            "behavioral_paths": [WORKFLOW],
            "evidence_paths": [ACCOUNTING, REGISTRY],
            "new_bootstrap": "sha1:" + ACCEPTED_BOOTSTRAP,
            "old_bootstrap": "sha1:" + OLD_BOOTSTRAP,
            "workflow_after_sha256": digest(changed[WORKFLOW]),
            "workflow_before_sha256": digest(workflow_before),
        },
        "g2": {"commit": "sha1:" + G2_COMMIT, "tree": "sha1:" + G2_TREE},
        "g3": {"commit": "sha1:" + commit, "parent": "sha1:" + G2_COMMIT, "tree": "sha1:" + tree},
        "metadata": {
            "author": "Bitcoin Roots <release@bitcoin-roots.invalid>",
            "author_date": G3_DATE,
            "committer": "Bitcoin Roots <release@bitcoin-roots.invalid>",
            "committer_date": G3_DATE,
            "message": G3_SUBJECT.decode().strip(),
        },
        "regenerated_evidence": {
            "accounting_after_sha256": digest(changed[ACCOUNTING]),
            "accounting_before_sha256": digest(accounting_before),
            "registry_after_sha256": digest(changed[REGISTRY]),
            "registry_before_sha256": digest(registry_before),
        },
        "schema_version": 1,
    }
    invariants = {
        "changed_paths": list(CHANGED_PATHS),
        "decision": "accepted",
        "g3_commit": "sha1:" + commit,
        "g3_tree": "sha1:" + tree,
        "invariants": {
            "core_valid_block_acceptance": "unchanged-no-runtime-path",
            "no_rdts_bip110_enforcement": "unchanged-no-runtime-path",
            "policy_rejection_block_acceptance": "unchanged-no-runtime-path",
        },
        "runtime_zone_paths": [],
        "schema_version": 1,
    }
    accounting = {
        "base": {"commit": "sha1:" + CANONICAL_COMMIT, "tree": "sha1:" + CANONICAL_TREE},
        "candidate_record": {"atom_count": count, "atom_digest": atoms, "sha256": digest(changed[ACCOUNTING])},
        "production_commits": [
            {"commit": "sha1:" + G2_COMMIT, "parent": "sha1:" + CANONICAL_COMMIT, "state": "published-rejected-portability", "tree": "sha1:" + G2_TREE},
            {"commit": "sha1:" + commit, "parent": "sha1:" + G2_COMMIT, "state": "private-g3", "tree": "sha1:" + tree},
        ],
        "schema_version": 1,
    }
    encoded = {
        "replay": canonical(replay),
        "invariants": canonical(invariants),
        "accounting": canonical(accounting),
        "review": canonical(review),
    }
    freeze = {
        "configuration_digests": {ARTIFACTS[key]: digest(encoded[key]) for key in ("replay", "invariants", "accounting", "review")},
        "g2_evidence": {"path": G2_FREEZE, "sha256": digest(g2_freeze_path.read_bytes())},
        "g3_commit": "sha1:" + commit,
        "g3_parent": "sha1:" + G2_COMMIT,
        "g3_ref": G3_REF,
        "g3_tree": "sha1:" + tree,
        "publication_authorized": False,
        "schema_version": 1,
    }
    encoded["freeze"] = canonical(freeze)
    acceptance = {
        "accepted_bootstrap": "sha1:" + ACCEPTED_BOOTSTRAP,
        "artifacts": {ARTIFACTS[key]: digest(encoded[key]) for key in ("replay", "invariants", "accounting", "review", "freeze")},
        "decision": "accepted-private-freeze",
        "g2_failure": {
            "bootstrap": "sha1:" + OLD_BOOTSTRAP,
            "reason": "methodology bundle digest is invalid",
            "run_id": 35340807632,
        },
        "g3_commit": "sha1:" + commit,
        "g3_tree": "sha1:" + tree,
        "historical_g2_run_ids": HISTORICAL_G2_RUNS,
        "remote_mutation_authorized": False,
        "schema_version": 1,
    }
    encoded["acceptance"] = canonical(acceptance)
    return encoded


def artifact_paths(root: Path) -> dict[str, Path]:
    absolute = Path(os.path.abspath(root))
    if not absolute.is_dir() or absolute.is_symlink():
        raise FreezeError("artifact root must be a real directory")
    result = {key: absolute / relative for key, relative in ARTIFACTS.items()}
    for path in result.values():
        if path.is_symlink():
            raise FreezeError("artifact symlink rejected")
        try:
            path.absolute().relative_to(absolute)
        except ValueError as error:
            raise FreezeError("artifact path escapes root") from error
    return result


def build(repository: Path, root: Path) -> None:
    paths = artifact_paths(root)
    if any(path.exists() for path in paths.values()):
        raise FreezeError("refusing artifact overwrite")
    expected = expected_artifacts(repository, root)
    for key, path in paths.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(expected[key])


def verify(repository: Path, root: Path) -> dict[str, bytes]:
    paths = artifact_paths(root)
    expected = expected_artifacts(repository, root)
    for key, path in paths.items():
        if not path.is_file() or path.read_bytes() != expected[key]:
            raise FreezeError("G3 " + key + " evidence differs")
    return expected


def anchor(repository: Path, root: Path, reference: str) -> None:
    if reference != G3_REF:
        raise FreezeError("G3 anchor ref is not the owned private ref")
    verify(repository, root)
    result = subprocess.run(
        ["git", "-C", str(repository), "update-ref", reference, G3_COMMIT, "0" * 40],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=git_environment(),
        check=False,
    )
    if result.returncode:
        raise FreezeError("G3 anchor ref already exists or cannot be created")
    if git_text(repository, "rev-parse", reference + "^{commit}") != G3_COMMIT or git_text(repository, "rev-parse", reference + "^{tree}") != G3_TREE:
        raise FreezeError("G3 anchor identity differs")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("build", "verify", "anchor"))
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--ref", default=G3_REF)
    arguments = parser.parse_args()
    try:
        if arguments.command == "build":
            build(arguments.repository, arguments.root)
        elif arguments.command == "verify":
            verify(arguments.repository, arguments.root)
        else:
            anchor(arguments.repository, arguments.root, arguments.ref)
        print(json.dumps({"commit": G3_COMMIT, "decision": "accepted", "tree": G3_TREE}, sort_keys=True))
        return 0
    except (FreezeError, OSError, UnicodeError, json.JSONDecodeError, ValueError) as error:
        print("roots-freeze-portability-g3: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
