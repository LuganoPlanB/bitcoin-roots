#!/usr/bin/env python3
"""Construct, record, verify, and privately anchor the Roots 29.4 G4 repair."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
from typing import Any


ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path) -> Any:
    specification = importlib.util.spec_from_file_location(name, path)
    if specification is None or specification.loader is None:
        raise ValueError("trusted helper cannot be loaded")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


BASE = load_module("roots_freeze_g3", ROOT / "contrib/devtools/roots-freeze-portability-g3.py")
REPLAY = load_module("roots_replay_g4", ROOT / "contrib/devtools/roots-replay.py")

CORE_COMMIT = "3fc0865963a38b871e9f7d94e6151c4953563516"
CANONICAL_COMMIT = "cbc88cff9b35b95a549c0313e424e13093fcd6a1"
CANONICAL_TREE = "39a5e30207a09962e78ae81c24cc65b1e478ef90"
G3_COMMIT = "c8dc2e70bc145930855cf615ba9caffaccdcdcb9"
G3_TREE = "70cd94d5f0ca21ac61720f33ecb86ba115a3b7a9"
G4_COMMIT = "990732b942778c7c96bc9f647f290846eec71c12"
G4_TREE = "5e0fe225597174052ed927becf666019b1b9799e"
G4_REF = "refs/roots/29.4/frozen-production-g4"
G4_SUBJECT = b"fix(release): freeze deterministic G4 candidate\n"
G4_DATE = "2026-09-18T00:00:00Z"

CI_WORKFLOW = ".github/workflows/ci.yml"
RELEASE_WORKFLOW = ".github/workflows/release.yml"
TRUSTED_REPLAY_WORKFLOW = ".github/workflows/roots-trusted-replay.yml"
FROZEN_TRUSTED_REPLAY_WORKFLOW = "contrib/roots/frozen-trusted-replay-29.4-g4.yml"
FROZEN_TRUSTED_REPLAY_WORKFLOW_BLOB = "79d9e66b9ad6651d5af2ee1dcc8e7beb574d67a6"
TRUSTED_REPLAY_TEST = "ci/test/test_roots_trusted_replay_ci.py"
FROZEN_TRUSTED_REPLAY_TEST_BLOB = "27f7752be6dac0f6b386519476f7582ee8510833"
VALIDATOR = "contrib/roots/validate-local-integration-baseline.py"
REGISTRY = "contrib/roots/post-methodology-adaptations.json"
ACCOUNTING = "contrib/roots/continuous-accounting-pr.json"
NEW_PATHS = (
    ".github/workflows/publish-release.yml",
    "ci/release/publish-verified-draft.sh",
    "ci/release/resolve-release-source.py",
    "ci/test/test_release_source_lock.py",
)
FRESH_PATHS = (*NEW_PATHS, CI_WORKFLOW)
OVERLAY_PATHS = (
    TRUSTED_REPLAY_WORKFLOW,
    ".github/workflows/publish-release.yml",
    RELEASE_WORKFLOW,
    "ci/release/publish-verified-draft.sh",
    "ci/release/resolve-release-source.py",
    "ci/roots-trusted-replay-gate.py",
    "ci/roots-trusted-replay-review.py",
    "ci/test/test_prepare_release.py",
    "ci/test/test_release_source_lock.py",
    "ci/test/test_roots_replay.py",
    "ci/test/test_roots_trusted_replay_ci.py",
    "contrib/devtools/roots-methodology.py",
    "contrib/devtools/roots-replay.py",
    "contrib/roots/methodology-v1.json",
    "doc/maintainers/replay-engine.md",
)
CHANGED_PATHS = tuple(sorted((*OVERLAY_PATHS, CI_WORKFLOW, VALIDATOR, REGISTRY, ACCOUNTING)))
ARTIFACTS = {
    "replay": "contrib/roots/post-candidate-replay-29.4-g4.json",
    "invariants": "contrib/roots/post-candidate-invariants-29.4-g4.json",
    "accounting": "contrib/roots/production-accounting-29.4-g4.json",
    "review": "contrib/roots/review-export-29.4-g4.json",
    "freeze": "contrib/roots/frozen-production-29.4-g4.json",
    "acceptance": "contrib/roots/acceptance-evidence-29.4-g4.json",
}
G3_ARTIFACTS = {
    key: value.replace("-g4.json", "-g3.json") for key, value in ARTIFACTS.items()
}


class FreezeError(ValueError):
    pass


def canonical(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True).encode("utf-8") + b"\n"


def digest(value: bytes) -> str:
    return "sha256:" + hashlib.sha256(value).hexdigest()


def git_blob_id(value: bytes) -> str:
    return hashlib.sha1(f"blob {len(value)}\0".encode("ascii") + value).hexdigest()


def safe_control_file(root: Path, relative: str) -> tuple[str, bytes]:
    path = root / relative
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 2_000_000:
        raise FreezeError("reviewed control file is unavailable: " + relative)
    mode = "100755" if stat.S_IMODE(path.stat().st_mode) & 0o111 else "100644"
    return mode, path.read_bytes()


def frozen_trusted_replay_test(root: Path) -> tuple[str, bytes]:
    mode, value = safe_control_file(root, TRUSTED_REPLAY_TEST)
    replacements = (
        (
            b'        self.assertIn("test \\"$CANDIDATE_COMMIT\\" = 990732b942778c7c96bc9f647f290846eec71c12", text)\n',
            b'        self.assertIn("test \\"$CANDIDATE_COMMIT\\" = c8dc2e70bc145930855cf615ba9caffaccdcdcb9", text)\n',
        ),
        (
            b'        self.assertIn("test \\"$CANDIDATE_TREE\\" = 5e0fe225597174052ed927becf666019b1b9799e", text)\n',
            b'        self.assertIn("test \\"$CANDIDATE_TREE\\" = 70cd94d5f0ca21ac61720f33ecb86ba115a3b7a9", text)\n',
        ),
        (b'        self.assertNotIn("test \\"$CANDIDATE_COMMIT\\" = c8dc2e70bc145930855cf615ba9caffaccdcdcb9", text)\n', b""),
        (b'        self.assertNotIn("test \\"$CANDIDATE_TREE\\" = 70cd94d5f0ca21ac61720f33ecb86ba115a3b7a9", text)\n', b""),
    )
    for current, frozen in replacements:
        if value.count(current) != 1:
            raise FreezeError("post-G4 trusted replay test precondition differs")
        value = value.replace(current, frozen)
    if mode != "100755" or git_blob_id(value) != FROZEN_TRUSTED_REPLAY_TEST_BLOB:
        raise FreezeError("frozen G4 trusted replay test differs")
    return mode, value


def transform_ci(repository: Path) -> bytes:
    value = BASE.blob(repository, G3_COMMIT, CI_WORKFLOW).decode("utf-8", "strict")
    old = "-DWITH_BDB=ON -DWITH_USDT=ON"
    new = "-DWITH_BDB=ON -DWARN_INCOMPATIBLE_BDB=OFF -DWITH_USDT=ON"
    if value.count(old) != 1 or "WARN_INCOMPATIBLE_BDB" in value:
        raise FreezeError("G3 per-commit BDB configure precondition differs")
    transformed = value.replace(old, new)
    result = transformed.encode("utf-8")
    validate_ci(result)
    return result


def validate_ci(value: bytes) -> None:
    text = value.decode("utf-8", "strict")
    lines = [line for line in text.splitlines() if "WARN_INCOMPATIBLE_BDB" in line]
    if (
        len(lines) != 1
        or lines[0].count("-DWARN_INCOMPATIBLE_BDB=OFF") != 1
        or "git rebase --exec" not in lines[0]
        or "-DWITH_BDB=ON -DWARN_INCOMPATIBLE_BDB=OFF -DWITH_USDT=ON" not in lines[0]
    ):
        raise FreezeError("BDB acknowledgement scope differs")


def transform_registry(repository: Path) -> bytes:
    value = json.loads(BASE.blob(repository, G3_COMMIT, REGISTRY))
    units = value.get("units") if isinstance(value, dict) else None
    if not isinstance(units, list):
        raise FreezeError("G3 registry is invalid")
    matches = [unit for unit in units if unit.get("id") == "roots-release-history-v1"]
    if len(matches) != 1 or any(path in matches[0].get("paths", []) for path in NEW_PATHS):
        raise FreezeError("G3 registry ownership precondition differs")
    matches[0]["paths"] = sorted([*matches[0]["paths"], *NEW_PATHS])
    return canonical(value)


def fresh_accounting_entry(atom: tuple[str, str, str], owner: str) -> dict[str, str]:
    path, kind, atom_digest = atom
    expected_owner = {
        CI_WORKFLOW: "roots-roots-build-ci-build-or-release",
        **{path: "roots-release-history-v1" for path in NEW_PATHS},
    }
    if path not in FRESH_PATHS or owner != expected_owner[path]:
        raise FreezeError(f"accounting metadata cannot be inferred for {path} {kind} owner {owner}")
    return {
        "adaptation": owner,
        "dependencies": "none",
        "digest": atom_digest,
        "disposition": "update",
        "kind": kind,
        "path": path,
        "provenance": "reviewed",
        "rationale": "narrow deterministic G4 CI and release-source correction",
        "replay_impact": "replay",
        "risk": "high" if path == CI_WORKFLOW else "medium",
        "scope": path,
        "tests": "ci/test/test_roots_pr_gate.py ci/test/test_release_source_lock.py ci/test/test_prepare_release.py ci/test/test_roots_freeze_g4.py",
    }


def regenerate_accounting(repository: Path, root: Path, provisional: str, registry_bytes: bytes) -> bytes:
    accounting = BASE.load_accounting(root)
    observed = {
        atom for atom in BASE.changed_atoms(repository, CANONICAL_COMMIT, provisional)
        if atom[0] != ACCOUNTING
    }
    template = json.loads(BASE.blob(repository, G3_COMMIT, ACCOUNTING))
    exact = {(item["path"], item["kind"], item["digest"]): item for item in template["changes"]}
    by_path: dict[tuple[str, str], list[dict[str, Any]]] = {}
    by_path_only: dict[str, list[dict[str, Any]]] = {}
    for item in template["changes"]:
        by_path.setdefault((item["path"], item["kind"]), []).append(item)
        by_path_only.setdefault(item["path"], []).append(item)
    manifest = json.loads(BASE.blob(repository, G3_COMMIT, "contrib/roots/adaptation-manifest-29.3.json"))
    registry = json.loads(registry_bytes)
    owners = {path: unit["id"] for unit in manifest["units"] for path in unit["touched"]["paths"]}
    owners.update({path: unit["id"] for unit in registry["units"] for path in unit["paths"]})
    changes = []
    for atom in sorted(observed):
        if atom in exact:
            changes.append(exact[atom])
            continue
        candidates = by_path.get(atom[:2], []) or by_path_only.get(atom[0], [])
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
    BASE.reject_host_state(repository)
    BASE.require_commit(repository, CANONICAL_COMMIT, CANONICAL_TREE)
    BASE.require_commit(repository, G3_COMMIT, G3_TREE)
    BASE.require_commit(repository, CORE_COMMIT)
    if BASE.git_text(repository, "show", "-s", "--format=%P", G3_COMMIT) == "":
        raise FreezeError("G3 parent is unavailable")
    # The post-G4 control workflow authorizes G4 and is intentionally different
    # from the workflow stored in G4. Reconstruct from this reviewed byte
    # snapshot, pinned by its Git blob ID, without consulting a mutable control
    # workflow or requiring the private G4 ref/object to exist.
    overlay = {
        path: safe_control_file(root, path)
        for path in OVERLAY_PATHS
        if path != TRUSTED_REPLAY_WORKFLOW
    }
    frozen_mode, frozen_workflow = safe_control_file(root, FROZEN_TRUSTED_REPLAY_WORKFLOW)
    if frozen_mode != "100644" or git_blob_id(frozen_workflow) != FROZEN_TRUSTED_REPLAY_WORKFLOW_BLOB:
        raise FreezeError("frozen G4 trusted replay workflow differs")
    overlay[TRUSTED_REPLAY_WORKFLOW] = (frozen_mode, frozen_workflow)
    overlay[TRUSTED_REPLAY_TEST] = frozen_trusted_replay_test(root)
    publish_mode, publish_bytes = overlay["ci/release/publish-verified-draft.sh"]
    if publish_mode != "100644" or not publish_bytes.startswith(b"#!/usr/bin/env bash\n"):
        raise FreezeError("reviewed publish script precondition differs")
    overlay["ci/release/publish-verified-draft.sh"] = ("100755", publish_bytes)
    workflow_mode, workflow_bytes = overlay[RELEASE_WORKFLOW]
    if workflow_mode != "100644" or b"persist-credentials: false" not in workflow_bytes:
        raise FreezeError("reviewed release workflow contract differs")
    if b"id-token:" in workflow_bytes or b"persist-credentials: true" in workflow_bytes:
        raise FreezeError("release workflow grants forbidden candidate credentials")
    ci_bytes = transform_ci(repository)
    registry_bytes = transform_registry(repository)
    with tempfile.TemporaryDirectory() as temporary:
        index = Path(temporary) / "index"
        BASE.git(repository, "read-tree", G3_COMMIT, index=index)
        for path, (mode, value) in overlay.items():
            BASE.git(repository, "update-index", "--add", "--cacheinfo", f"{mode},{BASE.write_blob(repository, value)},{path}", index=index)
        for path, value in ((CI_WORKFLOW, ci_bytes), (REGISTRY, registry_bytes)):
            BASE.git(repository, "update-index", "--add", "--cacheinfo", "100644," + BASE.write_blob(repository, value) + "," + path, index=index)
        BASE.git(repository, "update-index", "--chmod=+x", "--", VALIDATOR, index=index)
        provisional_tree = BASE.git_text(repository, "write-tree", index=index)
        provisional = BASE.git_text(repository, "commit-tree", provisional_tree, "-p", G3_COMMIT, input_data=b"provisional G4 evidence\n")
        accounting_bytes = regenerate_accounting(repository, root, provisional, registry_bytes)
        BASE.git(repository, "update-index", "--add", "--cacheinfo", "100644," + BASE.write_blob(repository, accounting_bytes) + "," + ACCOUNTING, index=index)
        tree = BASE.git_text(repository, "write-tree", index=index)
    commit = BASE.git_text(repository, "commit-tree", tree, "-p", G3_COMMIT, input_data=G4_SUBJECT)
    verify_candidate(repository, root, commit)
    expected = (G4_COMMIT, G4_TREE)
    if G4_COMMIT != "0" * 40 and (commit, tree) != expected:
        raise FreezeError("deterministic G4 identity differs")
    if BASE.git_text(repository, "show", "-s", "--format=%P", commit) != G3_COMMIT:
        raise FreezeError("G4 parent differs")
    paths = tuple(BASE.git_text(repository, "diff-tree", "--no-commit-id", "--name-only", "-r", G3_COMMIT, commit).splitlines())
    if paths != CHANGED_PATHS:
        raise FreezeError("G4 changed paths differ: " + repr(paths))
    changed = {path: value for path, (_, value) in overlay.items()}
    changed.update({CI_WORKFLOW: ci_bytes, REGISTRY: registry_bytes, ACCOUNTING: accounting_bytes})
    return commit, tree, changed


def verify_candidate(repository: Path, root: Path, commit: str) -> None:
    BASE.require_commit(repository, commit)
    if BASE.git_text(repository, "show", "-s", "--format=%P", commit) != G3_COMMIT:
        raise FreezeError("G4 parent differs")
    paths = tuple(BASE.git_text(repository, "diff-tree", "--no-commit-id", "--name-only", "-r", G3_COMMIT, commit).splitlines())
    if paths != CHANGED_PATHS:
        raise FreezeError("G4 changed paths differ")
    validator_entry = BASE.git_text(repository, "ls-tree", commit, "--", VALIDATOR).split()
    if not validator_entry or validator_entry[0] != "100755":
        raise FreezeError("G4 validator mode differs")
    validate_ci(BASE.blob(repository, commit, CI_WORKFLOW))
    _, reviewed_workflow = safe_control_file(root, RELEASE_WORKFLOW)
    if BASE.blob(repository, commit, RELEASE_WORKFLOW) != reviewed_workflow:
        raise FreezeError("G4 release workflow differs from reviewed control")


def canonical_patch(repository: Path, commit: str) -> bytes:
    config = tuple(argument for item in REPLAY.CANONICAL_FORMAT_PATCH_CONFIG for argument in ("-c", item))
    return BASE.git(
        repository,
        *config,
        "format-patch",
        *REPLAY.CANONICAL_FORMAT_PATCH_OPTIONS,
        "-O", os.devnull,
        G3_COMMIT + ".." + commit,
    )


def atom_digest(accounting_bytes: bytes) -> tuple[int, str]:
    changes = json.loads(accounting_bytes)["changes"]
    identities = [[item["path"], item["kind"], item["digest"]] for item in changes]
    return len(identities), digest(json.dumps(identities, separators=(",", ":")).encode("utf-8"))


def expected_artifacts(repository: Path, root: Path) -> dict[str, bytes]:
    commit, tree, changed = construct(repository, root)
    count, atoms = atom_digest(changed[ACCOUNTING])
    workflow_blob = BASE.git_text(repository, "rev-parse", commit + ":" + RELEASE_WORKFLOW)
    patch = canonical_patch(repository, commit)
    scoped = BASE.git(repository, "diff", "--no-ext-diff", "--no-color", "--binary", "--full-index", "--no-renames", G3_COMMIT, commit)
    range_diff = BASE.git(repository, "range-diff", "--no-color", "--no-dual-color", "--no-renames", "--no-ext-diff", "--creation-factor=60", CORE_COMMIT + ".." + G3_COMMIT, CORE_COMMIT + ".." + commit)
    runbook = {
        path: {
            "mode": BASE.git_text(repository, "ls-tree", commit, "--", path).split()[0],
            "sha256": digest(BASE.blob(repository, commit, path)),
        }
        for path in (CI_WORKFLOW, RELEASE_WORKFLOW, VALIDATOR, *NEW_PATHS)
    }
    replay = {
        "base": {"commit": "sha1:" + G3_COMMIT, "tree": "sha1:" + G3_TREE},
        "candidate": {"commit": "sha1:" + commit, "parent": "sha1:" + G3_COMMIT, "tree": "sha1:" + tree},
        "canonical_export_contract": REPLAY.CANONICAL_EXPORT_CONTRACT,
        "changed_paths": list(CHANGED_PATHS),
        "runbook_identities": runbook,
        "schema_version": 1,
    }
    invariants = {
        "changed_paths": list(CHANGED_PATHS),
        "decision": "accepted",
        "g4_commit": "sha1:" + commit,
        "g4_tree": "sha1:" + tree,
        "invariants": {
            "core_valid_block_acceptance": "unchanged-no-runtime-path",
            "no_rdts_bip110_enforcement": "unchanged-no-runtime-path",
            "policy_rejection_block_acceptance": "unchanged-no-runtime-path",
        },
        "runtime_zone_paths": [],
        "schema_version": 1,
    }
    production = {
        "base": {"commit": "sha1:" + CANONICAL_COMMIT, "tree": "sha1:" + CANONICAL_TREE},
        "candidate_record": {"atom_count": count, "atom_digest": atoms, "sha256": digest(changed[ACCOUNTING])},
        "production_commits": [
            {"commit": "sha1:" + G3_COMMIT, "state": "private-rejected-ci", "tree": "sha1:" + G3_TREE},
            {"commit": "sha1:" + commit, "parent": "sha1:" + G3_COMMIT, "state": "private-g4", "tree": "sha1:" + tree},
        ],
        "schema_version": 1,
    }
    review = {
        "advisory": "range-diff is review evidence, not authorization",
        "canonical_export_contract": REPLAY.CANONICAL_EXPORT_CONTRACT,
        "patch_range": G3_COMMIT + ".." + commit,
        "patch_series_sha256": digest(patch),
        "range_diff_ranges": [CORE_COMMIT + ".." + G3_COMMIT, CORE_COMMIT + ".." + commit],
        "range_diff_sha256": digest(range_diff),
        "release_workflow_blob": "sha1:" + workflow_blob,
        "schema_version": 1,
        "scoped_diff_sha256": digest(scoped),
    }
    encoded = {
        "replay": canonical(replay),
        "invariants": canonical(invariants),
        "accounting": canonical(production),
        "review": canonical(review),
    }
    preserved = {}
    for key, relative in G3_ARTIFACTS.items():
        path = root / relative
        if path.is_symlink() or not path.is_file():
            raise FreezeError("preserved G3 evidence is unavailable")
        preserved[relative] = digest(path.read_bytes())
    freeze = {
        "configuration_digests": {ARTIFACTS[key]: digest(encoded[key]) for key in ("replay", "invariants", "accounting", "review")},
        "g3_evidence": preserved,
        "g4_commit": "sha1:" + commit,
        "g4_parent": "sha1:" + G3_COMMIT,
        "g4_ref": G4_REF,
        "g4_tree": "sha1:" + tree,
        "publication_authorized": False,
        "schema_version": 1,
    }
    encoded["freeze"] = canonical(freeze)
    acceptance = {
        "artifacts": {ARTIFACTS[key]: digest(encoded[key]) for key in ("replay", "invariants", "accounting", "review", "freeze")},
        "decision": "accepted-private-freeze",
        "g3_failures": {
            "integration_run": 35357813170,
            "pr_run": 35357821039,
            "reasons": ["non-executable validator", "BDB 5.3 requires narrow acknowledgement"],
        },
        "g4_commit": "sha1:" + commit,
        "g4_tree": "sha1:" + tree,
        "release_workflow_blob": "sha1:" + workflow_blob,
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
    if any(path.is_symlink() for path in result.values()):
        raise FreezeError("artifact symlink rejected")
    return result


def build(repository: Path, root: Path) -> None:
    paths = artifact_paths(root)
    if any(path.exists() for path in paths.values()):
        raise FreezeError("refusing artifact overwrite")
    for key, value in expected_artifacts(repository, root).items():
        paths[key].parent.mkdir(parents=True, exist_ok=True)
        paths[key].write_bytes(value)


def verify(repository: Path, root: Path) -> dict[str, bytes]:
    expected = expected_artifacts(repository, root)
    for key, path in artifact_paths(root).items():
        if not path.is_file() or path.read_bytes() != expected[key]:
            raise FreezeError("G4 " + key + " evidence differs")
    return expected


def reject_anchor_host_state(repository: Path) -> None:
    git_dir = BASE.repository_git_dir(repository)
    if os.environ.get("GIT_ALTERNATE_OBJECT_DIRECTORIES") or (git_dir / "objects/info/alternates").exists():
        raise FreezeError("anchor Git alternates are forbidden")
    if (git_dir / "info/grafts").exists() or BASE.git_text(repository, "for-each-ref", "--format=%(refname)", "refs/replace"):
        raise FreezeError("anchor Git grafts and replacements are forbidden")
    if (git_dir / "shallow").exists():
        raise FreezeError("anchor repository must not be shallow")
    if BASE.local_config(repository, "rerere.enabled").lower() in {"1", "true", "yes", "on"}:
        raise FreezeError("anchor Git rerere is forbidden")
    if BASE.local_config(repository, "core.hooksPath"):
        raise FreezeError("configured anchor Git hooks are forbidden")
    hooks = git_dir / "hooks"
    if hooks.is_dir() and any(path.is_file() and not path.name.endswith(".sample") for path in hooks.iterdir()):
        raise FreezeError("active anchor Git hooks are forbidden")


def anchor(repository: Path, root: Path, reference: str, target_repository: Path | None = None) -> None:
    if reference != G4_REF or G4_COMMIT == "0" * 40:
        raise FreezeError("G4 anchor identity is not finalized")
    verify(repository, root)
    target = target_repository or repository
    reject_anchor_host_state(target)
    if BASE.git_text(target, "show-ref", "--verify", reference, check=False):
        raise FreezeError("G4 anchor ref already exists")
    imported = subprocess.run(
        [
            "git", "-C", str(target),
            "-c", "credential.helper=",
            "-c", "core.hooksPath=/dev/null",
            "fetch", "--no-tags", "--no-write-fetch-head", str(repository), G4_COMMIT,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=BASE.git_environment(),
        check=False,
    )
    if imported.returncode:
        raise FreezeError("G4 object import failed")
    result = subprocess.run(
        ["git", "-C", str(target), "update-ref", reference, G4_COMMIT, "0" * 40],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=BASE.git_environment(),
        check=False,
    )
    if result.returncode:
        raise FreezeError("G4 anchor ref already exists or cannot be created")
    if BASE.git_text(target, "rev-parse", reference + "^{tree}") != G4_TREE:
        raise FreezeError("G4 anchor identity differs")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("construct", "build", "verify", "anchor"))
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--ref", default=G4_REF)
    parser.add_argument("--anchor-repository", type=Path)
    arguments = parser.parse_args()
    try:
        if arguments.command == "construct":
            commit, tree, _ = construct(arguments.repository, arguments.root)
        elif arguments.command == "build":
            build(arguments.repository, arguments.root)
            commit, tree = G4_COMMIT, G4_TREE
        elif arguments.command == "verify":
            verify(arguments.repository, arguments.root)
            commit, tree = G4_COMMIT, G4_TREE
        else:
            anchor(arguments.repository, arguments.root, arguments.ref, arguments.anchor_repository)
            commit, tree = G4_COMMIT, G4_TREE
        print(json.dumps({"commit": commit, "decision": "accepted", "tree": tree}, sort_keys=True))
        return 0
    except (BASE.FreezeError, FreezeError, OSError, UnicodeError, json.JSONDecodeError, ValueError) as error:
        print("roots-freeze-g4: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
