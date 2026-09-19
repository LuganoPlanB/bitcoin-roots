#!/usr/bin/env python3
"""Fail closed before building an immutable Roots production candidate.

This control-plane program deliberately reads trusted release material and the
candidate checkout separately. Candidate files are never imported or executed
while the immutable inputs are checked.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import posixpath
import re
import subprocess
import sys
from typing import Any


CANDIDATE_COMMIT = "990732b942778c7c96bc9f647f290846eec71c12"
CANDIDATE_TREE = "5e0fe225597174052ed927becf666019b1b9799e"
G3_COMMIT = "c8dc2e70bc145930855cf615ba9caffaccdcdcb9"
G3_TREE = "70cd94d5f0ca21ac61720f33ecb86ba115a3b7a9"
G2_COMMIT = "dfc74d403585f7c23815ef80d2e206b85c33919a"
G2_TREE = "477eb9b3f50098b0b8548a9ff35efaa29fd7ecde"
CANONICAL_COMMIT = "cbc88cff9b35b95a549c0313e424e13093fcd6a1"
CANONICAL_TREE = "39a5e30207a09962e78ae81c24cc65b1e478ef90"
ACCEPTED_BOOTSTRAP = "bb977bbd7742650ea1e9d93aa0b6573ee5b14985"
REJECTED_BOOTSTRAP = "6f85dfb728ca0976658e0fbd26101e7df5e35a14"
HISTORICAL_G2_RUNS = [35340615938, 35340807557, 35340807632, 35340822312, 35340826867]
INTEGRATION_REF = "refs/heads/integration/roots-29.4"
MAX_REPORT_BYTES = 16_384
MAX_EVIDENCE_BYTES = 131_072
PORTABILITY_WORKFLOW = ".github/workflows/roots-portability.yml"
PORTABILITY_WORKFLOW_SHA256 = "sha256:58355c3005887204ceb2a797dda44c92386c7d9cd6713dc38ad510685944dcb6"
G2_FREEZE = "contrib/roots/frozen-production-29.4.json"
G3_EVIDENCE_DIGESTS = {
    "contrib/roots/acceptance-evidence-29.4-g3.json": "sha256:48cb41ecbd72468409e1ddca7a9cc105da70ce6f7c76b064ae1d4419372c4d5a",
    "contrib/roots/frozen-production-29.4-g3.json": "sha256:7fd093093c84d0b6ead4d677cce06a3837b4995cc054ddb43255a1d9e0931b3f",
    "contrib/roots/post-candidate-invariants-29.4-g3.json": "sha256:0f148978b71820ef929540ce11d3d66a26c3eed35619465c944f7aa3e1fe2e7a",
    "contrib/roots/post-candidate-replay-29.4-g3.json": "sha256:7bbbcb7cb93a1782c1fb843a96d9e15f062fcb4abdcc850e31543cfb4f7adcae",
    "contrib/roots/production-accounting-29.4-g3.json": "sha256:40065ee05b7c67a28ad53ba113c31f82fecbb6fca0680c843ac72d48c9dd6fc6",
    "contrib/roots/review-export-29.4-g3.json": "sha256:4cfe2038408d8be2acb789129e83ca34f9ef309f0728f542be1e4533a05f3aed",
}
G4_EVIDENCE_DIGESTS = {
    "contrib/roots/acceptance-evidence-29.4-g4.json": "sha256:3490677c65446fe84b163b592d94dac040ef86d5d4638c91e47bc0bada862d29",
    "contrib/roots/frozen-production-29.4-g4.json": "sha256:4131a32650df6d3b272490b7e8d700287c1bc8dcff75c04e7976ecc4f80b78fa",
    "contrib/roots/post-candidate-invariants-29.4-g4.json": "sha256:d3b41f020e7e597cad191b7e955588c67c0afcbc5b1f682b57bad3916b4cbabf",
    "contrib/roots/post-candidate-replay-29.4-g4.json": "sha256:2bcfe88e6232a6782617fb6adfd4488164a8c3ecb9ab64d7fe83215a1e54a25a",
    "contrib/roots/production-accounting-29.4-g4.json": "sha256:fe7665a039302a24f18387eb36126f7f67ecd3252b31fa28d609444afb576726",
    "contrib/roots/review-export-29.4-g4.json": "sha256:f58d4cc6e52d4f0c4c38075c7e0a824452eca46ccd5ccc08d966f034e560ef1d",
}
RELEASE_WORKFLOW = ".github/workflows/release.yml"
RELEASE_WORKFLOW_BLOB = "b923b4dd6c817a36b2807ea1a3f9ec91266101da"
CANONICAL_EXPORT_CONTRACT = "roots-replay-generated-mbox-v1"
SHA1 = re.compile(r"^[0-9a-f]{40}$")


class PromotionError(ValueError):
    pass


def safe_path(path: Path, description: str) -> Path:
    absolute = Path(os.path.abspath(path))
    if not absolute.is_dir() or absolute.is_symlink() or any(parent.is_symlink() for parent in (absolute, *absolute.parents)):
        raise PromotionError(f"{description} is unsafe")
    return absolute


def git_environment() -> dict[str, str]:
    forbidden = {"GIT_DIR", "GIT_WORK_TREE", "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_REPLACE_REF_BASE"}
    if any(name in os.environ for name in forbidden):
        raise PromotionError("host Git object override is forbidden")
    return {"PATH": os.environ.get("PATH", ""), "LC_ALL": "C", "LANG": "C", "TZ": "UTC", "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": os.devnull, "GIT_NO_REPLACE_OBJECTS": "1", "GIT_OPTIONAL_LOCKS": "0", "GIT_TERMINAL_PROMPT": "0"}


def git(repository: Path, *arguments: str) -> str:
    result = subprocess.run(["git", "-C", str(repository), "-c", "core.hooksPath=/dev/null", "-c", "core.useReplaceRefs=false", *arguments], capture_output=True, text=True, encoding="utf-8", errors="strict", env=git_environment(), check=False)
    if result.returncode:
        raise PromotionError("candidate Git resolution failed")
    return result.stdout.strip()


def digest(value: bytes) -> str:
    return "sha256:" + hashlib.sha256(value).hexdigest()


def trusted_file(root: Path, relative: str) -> Path:
    path = root
    for component in Path(relative).parts:
        path /= component
        if path.is_symlink():
            raise PromotionError("trusted evidence path is symlinked")
    if not path.is_file() or path.stat().st_size > MAX_EVIDENCE_BYTES:
        raise PromotionError("trusted evidence is unavailable or oversized")
    return path


def load_trusted_json(root: Path, relative: str) -> dict[str, Any]:
    try:
        value = json.loads(trusted_file(root, relative).read_text(encoding="utf-8", errors="strict"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PromotionError("trusted evidence is invalid") from error
    if not isinstance(value, dict):
        raise PromotionError("trusted evidence is not an object")
    return value


def verify_g3_evidence(root: Path) -> None:
    workflow = trusted_file(root, PORTABILITY_WORKFLOW)
    if digest(workflow.read_bytes()) != PORTABILITY_WORKFLOW_SHA256:
        raise PromotionError("trusted portability workflow differs from accepted G3")
    for relative, expected in G3_EVIDENCE_DIGESTS.items():
        if digest(trusted_file(root, relative).read_bytes()) != expected:
            raise PromotionError("trusted G3 evidence digest differs")

    acceptance = load_trusted_json(root, "contrib/roots/acceptance-evidence-29.4-g3.json")
    freeze = load_trusted_json(root, "contrib/roots/frozen-production-29.4-g3.json")
    replay = load_trusted_json(root, "contrib/roots/post-candidate-replay-29.4-g3.json")
    accounting = load_trusted_json(root, "contrib/roots/production-accounting-29.4-g3.json")
    expected_commit, expected_tree = "sha1:" + G3_COMMIT, "sha1:" + G3_TREE
    if acceptance.get("g3_commit") != expected_commit or acceptance.get("g3_tree") != expected_tree:
        raise PromotionError("G3 acceptance identity differs")
    if acceptance.get("accepted_bootstrap") != "sha1:" + ACCEPTED_BOOTSTRAP or acceptance.get("remote_mutation_authorized") is not False:
        raise PromotionError("G3 acceptance boundary differs")
    failure = acceptance.get("g2_failure")
    if not isinstance(failure, dict) or failure.get("bootstrap") != "sha1:" + REJECTED_BOOTSTRAP or failure.get("run_id") != 35340807632 or acceptance.get("historical_g2_run_ids") != HISTORICAL_G2_RUNS:
        raise PromotionError("G2 rejection history differs")
    if freeze.get("g3_commit") != expected_commit or freeze.get("g3_tree") != expected_tree or freeze.get("g3_parent") != "sha1:" + G2_COMMIT or freeze.get("g3_ref") != "refs/roots/29.4/frozen-production-g3" or freeze.get("publication_authorized") is not False:
        raise PromotionError("G3 freeze boundary differs")
    g2_evidence = freeze.get("g2_evidence")
    if not isinstance(g2_evidence, dict) or g2_evidence.get("path") != G2_FREEZE or g2_evidence.get("sha256") != digest(trusted_file(root, G2_FREEZE).read_bytes()):
        raise PromotionError("preserved G2 freeze evidence differs")
    if replay.get("canonical_base") != {"commit": "sha1:" + CANONICAL_COMMIT, "tree": "sha1:" + CANONICAL_TREE} or replay.get("g2") != {"commit": "sha1:" + G2_COMMIT, "tree": "sha1:" + G2_TREE} or replay.get("g3") != {"commit": expected_commit, "parent": "sha1:" + G2_COMMIT, "tree": expected_tree}:
        raise PromotionError("G3 replay lineage differs")
    commits = accounting.get("production_commits")
    if not isinstance(commits, list) or [item.get("commit") for item in commits if isinstance(item, dict)] != ["sha1:" + G2_COMMIT, expected_commit] or [item.get("state") for item in commits if isinstance(item, dict)] != ["published-rejected-portability", "private-g3"]:
        raise PromotionError("G3 production accounting differs")


def git_blob_id(value: bytes) -> str:
    header = f"blob {len(value)}\0".encode("ascii")
    return hashlib.sha1(header + value).hexdigest()


def verify_g4_evidence(root: Path) -> str:
    verify_g3_evidence(root)
    for relative, expected in G4_EVIDENCE_DIGESTS.items():
        if digest(trusted_file(root, relative).read_bytes()) != expected:
            raise PromotionError("trusted G4 evidence digest differs")

    acceptance = load_trusted_json(root, "contrib/roots/acceptance-evidence-29.4-g4.json")
    freeze = load_trusted_json(root, "contrib/roots/frozen-production-29.4-g4.json")
    invariants = load_trusted_json(root, "contrib/roots/post-candidate-invariants-29.4-g4.json")
    replay = load_trusted_json(root, "contrib/roots/post-candidate-replay-29.4-g4.json")
    accounting = load_trusted_json(root, "contrib/roots/production-accounting-29.4-g4.json")
    review = load_trusted_json(root, "contrib/roots/review-export-29.4-g4.json")
    expected_commit = "sha1:" + CANDIDATE_COMMIT
    expected_tree = "sha1:" + CANDIDATE_TREE
    expected_parent = "sha1:" + G3_COMMIT
    expected_parent_tree = "sha1:" + G3_TREE
    expected_workflow_blob = "sha1:" + RELEASE_WORKFLOW_BLOB

    if acceptance.get("g4_commit") != expected_commit or acceptance.get("g4_tree") != expected_tree:
        raise PromotionError("G4 acceptance identity differs")
    if acceptance.get("decision") != "accepted-private-freeze" or acceptance.get("remote_mutation_authorized") is not False:
        raise PromotionError("G4 acceptance boundary differs")
    if acceptance.get("release_workflow_blob") != expected_workflow_blob:
        raise PromotionError("G4 accepted release workflow differs")
    if acceptance.get("artifacts") != {key: value for key, value in G4_EVIDENCE_DIGESTS.items() if "acceptance-evidence" not in key}:
        raise PromotionError("G4 accepted artifact set differs")
    if acceptance.get("g3_failures") != {"integration_run": 35357813170, "pr_run": 35357821039, "reasons": ["non-executable validator", "BDB 5.3 requires narrow acknowledgement"]}:
        raise PromotionError("G3 rejection history differs")

    if freeze.get("g4_commit") != expected_commit or freeze.get("g4_tree") != expected_tree or freeze.get("g4_parent") != expected_parent or freeze.get("g4_ref") != "refs/roots/29.4/frozen-production-g4" or freeze.get("publication_authorized") is not False:
        raise PromotionError("G4 freeze boundary differs")
    if freeze.get("g3_evidence") != G3_EVIDENCE_DIGESTS:
        raise PromotionError("preserved G3 evidence differs")
    expected_configuration = {key: value for key, value in G4_EVIDENCE_DIGESTS.items() if "acceptance-evidence" not in key and "frozen-production" not in key}
    if freeze.get("configuration_digests") != expected_configuration:
        raise PromotionError("G4 freeze configuration differs")

    if invariants.get("g4_commit") != expected_commit or invariants.get("g4_tree") != expected_tree or invariants.get("runtime_zone_paths") != [] or invariants.get("decision") != "accepted":
        raise PromotionError("G4 invariant evidence differs")
    if replay.get("base") != {"commit": expected_parent, "tree": expected_parent_tree} or replay.get("candidate") != {"commit": expected_commit, "parent": expected_parent, "tree": expected_tree} or replay.get("canonical_export_contract") != CANONICAL_EXPORT_CONTRACT:
        raise PromotionError("G4 replay lineage differs")
    commits = accounting.get("production_commits")
    if not isinstance(commits, list) or [item.get("commit") for item in commits if isinstance(item, dict)] != [expected_parent, expected_commit] or [item.get("state") for item in commits if isinstance(item, dict)] != ["private-rejected-ci", "private-g4"]:
        raise PromotionError("G4 production accounting differs")
    if review.get("canonical_export_contract") != CANONICAL_EXPORT_CONTRACT or review.get("patch_range") != f"{G3_COMMIT}..{CANDIDATE_COMMIT}" or review.get("release_workflow_blob") != expected_workflow_blob:
        raise PromotionError("G4 review evidence differs")

    workflow_blob = git_blob_id(trusted_file(root, RELEASE_WORKFLOW).read_bytes())
    if workflow_blob != RELEASE_WORKFLOW_BLOB:
        raise PromotionError("trusted release workflow differs from accepted G4")
    return workflow_blob


def validate_candidate_repository(repository: Path, trusted_root: Path, commit: str, tree: str) -> None:
    candidate = safe_path(repository, "candidate repository")
    if candidate == trusted_root:
        raise PromotionError("candidate repository must be isolated from trusted control")
    if git(candidate, "rev-parse", "--is-shallow-repository") != "false":
        raise PromotionError("candidate history is shallow")
    git_dir = Path(git(candidate, "rev-parse", "--git-dir"))
    if not git_dir.is_absolute():
        git_dir = candidate / git_dir
    if (git_dir / "objects/info/alternates").exists() or git(candidate, "for-each-ref", "--format=%(refname)", "refs/replace"):
        raise PromotionError("candidate object substitution is forbidden")
    resolved_commit = git(candidate, "rev-parse", "--verify", f"{commit}^{{commit}}")
    actual_tree = git(candidate, "rev-parse", "--verify", f"{commit}^{{tree}}")
    if resolved_commit != commit or actual_tree != tree:
        raise PromotionError("candidate commit or tree differs from immutable input")
    validate_candidate_tree(candidate, commit)


def validate_candidate_tree(repository: Path, commit: str) -> None:
    """Reject hostile tree entries before candidate-controlled tools run."""
    result = subprocess.run(
        ["git", "-C", str(repository), "ls-tree", "-rz", "-r", "-l", "--full-tree", commit],
        capture_output=True, env=git_environment(), check=False,
    )
    if result.returncode:
        raise PromotionError("candidate tree listing failed")
    for entry in result.stdout.split(b"\0"):
        if not entry:
            continue
        try:
            metadata, path = entry.split(b"\t", 1)
            mode, kind, object_id, size = metadata.split()
            decoded = path.decode("utf-8", "strict")
            object_size = int(size)
        except (UnicodeError, ValueError):
            raise PromotionError("candidate tree entry is malformed") from None
        if not decoded or decoded.startswith("/") or any(part in {"", ".", ".."} for part in decoded.split("/")) or any(ord(char) < 32 or ord(char) == 127 for char in decoded):
            raise PromotionError("candidate tree path is unsafe")
        if kind != b"blob" or not SHA1.fullmatch(object_id.decode("ascii")) or object_size < 0:
            raise PromotionError("candidate tree object is unsafe")
        if mode == b"120000":
            target = git(repository, "cat-file", "blob", object_id.decode("ascii"))
            resolved = posixpath.normpath(posixpath.join(posixpath.dirname(decoded), target))
            if not target or target.startswith("/") or resolved == ".." or resolved.startswith("../") or any(ord(char) < 32 or ord(char) == 127 for char in target):
                raise PromotionError("candidate symlink target is unsafe")
        elif mode != b"100644" and mode != b"100755":
            raise PromotionError("candidate tree mode is unsafe")


def validate_integration_ref(repository: Path, commit: str, tree: str) -> None:
    ref = "refs/remotes/origin/integration/roots-29.4"
    resolved_commit = git(repository, "rev-parse", "--verify", ref + "^{commit}")
    resolved_tree = git(repository, "rev-parse", "--verify", ref + "^{tree}")
    if resolved_commit != commit or resolved_tree != tree:
        raise PromotionError("integration ref differs from immutable candidate")


def validate_release_workflow(repository: Path, commit: str, expected_blob: str) -> None:
    if git(repository, "rev-parse", "--verify", f"{commit}:{RELEASE_WORKFLOW}") != expected_blob:
        raise PromotionError("candidate release workflow differs from trusted control")


def validate_inputs(candidate_commit: str, candidate_tree: str, control_sha: str) -> None:
    if not all(SHA1.fullmatch(value) for value in (candidate_commit, candidate_tree, control_sha)):
        raise PromotionError("candidate input is not a full SHA-1")
    if candidate_commit != CANDIDATE_COMMIT or candidate_tree != CANDIDATE_TREE:
        raise PromotionError("candidate input differs from frozen production identity")


def verify_trusted_controls(root: Path, control_sha: str) -> tuple[str, str]:
    trusted = safe_path(root, "trusted control root")
    if git(trusted, "rev-parse", "--verify", "HEAD^{commit}") != control_sha:
        raise PromotionError("trusted checkout differs from declared control commit")
    control_tree = git(trusted, "rev-parse", "--verify", "HEAD^{tree}")
    release_workflow_blob = verify_g4_evidence(trusted)
    return control_tree, release_workflow_blob


def candidate_environment() -> dict[str, str]:
    """Return the only environment candidate build commands may receive."""
    return {"PATH": os.environ.get("PATH", ""), "HOME": os.devnull, "LC_ALL": "C", "LANG": "C", "TZ": "UTC", "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": os.devnull, "GIT_NO_REPLACE_OBJECTS": "1", "GIT_TERMINAL_PROMPT": "0"}


def report(candidate_commit: str, candidate_tree: str, control_sha: str, candidate_repository: Path, trusted_root: Path) -> dict[str, Any]:
    # This is deliberately first: malformed candidate values must not reach Git.
    validate_inputs(candidate_commit, candidate_tree, control_sha)
    control_tree, release_workflow_blob = verify_trusted_controls(trusted_root, control_sha)
    validate_candidate_repository(candidate_repository, safe_path(trusted_root, "trusted control root"), candidate_commit, candidate_tree)
    validate_integration_ref(candidate_repository, candidate_commit, candidate_tree)
    validate_release_workflow(candidate_repository, candidate_commit, release_workflow_blob)
    return {"candidate_commit": candidate_commit, "candidate_tree": candidate_tree, "control_commit": control_sha, "control_tree": control_tree, "integration_ref": INTEGRATION_REF, "release_workflow_blob": "sha1:" + release_workflow_blob, "read_only": True, "secret_environment": sorted(candidate_environment())}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate-commit", required=True)
    parser.add_argument("--candidate-tree", required=True)
    parser.add_argument("--control-sha", required=True)
    parser.add_argument("--candidate-repository", type=Path, required=True)
    parser.add_argument("--trusted-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        value = report(args.candidate_commit, args.candidate_tree, args.control_sha, args.candidate_repository, args.trusted_root)
        payload = json.dumps(value, sort_keys=True, separators=(",", ":")).encode() + b"\n"
        if len(payload) > MAX_REPORT_BYTES or args.output.name != "trusted-promotion-report.json" or args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir():
            raise PromotionError("bounded promotion evidence output is invalid")
        args.output.write_bytes(payload)
    except (OSError, PromotionError) as error:
        print(f"roots-trusted-promotion: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
