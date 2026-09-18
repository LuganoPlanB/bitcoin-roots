#!/usr/bin/env python3
"""Fail closed before building an immutable Roots production candidate.

This control-plane program deliberately reads trusted release material and the
candidate checkout separately. Candidate files are never imported or executed
while the immutable inputs are checked.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import posixpath
import re
import subprocess
import sys
from typing import Any


CANDIDATE_COMMIT = "dfc74d403585f7c23815ef80d2e206b85c33919a"
CANDIDATE_TREE = "477eb9b3f50098b0b8548a9ff35efaa29fd7ecde"
INTEGRATION_REF = "refs/heads/integration/roots-29.4"
MAX_REPORT_BYTES = 16_384
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


def validate_inputs(candidate_commit: str, candidate_tree: str, control_sha: str) -> None:
    if not all(SHA1.fullmatch(value) for value in (candidate_commit, candidate_tree, control_sha)):
        raise PromotionError("candidate input is not a full SHA-1")
    if candidate_commit != CANDIDATE_COMMIT or candidate_tree != CANDIDATE_TREE:
        raise PromotionError("candidate input differs from frozen production identity")


def verify_trusted_controls(root: Path, control_sha: str) -> str:
    trusted = safe_path(root, "trusted control root")
    if git(trusted, "rev-parse", "--verify", "HEAD^{commit}") != control_sha:
        raise PromotionError("trusted checkout differs from declared control commit")
    control_tree = git(trusted, "rev-parse", "--verify", "HEAD^{tree}")
    for command in (
        ("contrib/devtools/roots-freeze-production.py", "verify", "--repository", str(trusted), "--root", str(trusted), "--output", str(trusted / "contrib/roots/frozen-production-29.4.json")),
        ("contrib/devtools/roots-post-candidate-replay.py", "verify", "--repository", str(trusted), "--root", str(trusted), "--inventory", str(trusted / "contrib/roots/post-candidate-inventory-29.4.json"), "--manifest", str(trusted / "contrib/roots/post-candidate-replay-29.4.json")),
        ("contrib/devtools/roots-post-candidate-invariants.py", "verify", "--repository", str(trusted), "--manifest", str(trusted / "contrib/roots/post-candidate-replay-29.4.json"), "--output", str(trusted / "contrib/roots/post-candidate-invariants-29.4.json")),
    ):
        result = subprocess.run([sys.executable, str(trusted / command[0]), *command[1:]], capture_output=True, text=True, encoding="utf-8", errors="strict", env=git_environment(), check=False)
        if result.returncode:
            raise PromotionError("trusted control validator rejected frozen candidate")
    return control_tree


def candidate_environment() -> dict[str, str]:
    """Return the only environment candidate build commands may receive."""
    return {"PATH": os.environ.get("PATH", ""), "HOME": os.devnull, "LC_ALL": "C", "LANG": "C", "TZ": "UTC", "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": os.devnull, "GIT_NO_REPLACE_OBJECTS": "1", "GIT_TERMINAL_PROMPT": "0"}


def report(candidate_commit: str, candidate_tree: str, control_sha: str, candidate_repository: Path, trusted_root: Path) -> dict[str, Any]:
    # This is deliberately first: malformed candidate values must not reach Git.
    validate_inputs(candidate_commit, candidate_tree, control_sha)
    control_tree = verify_trusted_controls(trusted_root, control_sha)
    validate_candidate_repository(candidate_repository, safe_path(trusted_root, "trusted control root"), candidate_commit, candidate_tree)
    validate_integration_ref(candidate_repository, candidate_commit, candidate_tree)
    return {"candidate_commit": candidate_commit, "candidate_tree": candidate_tree, "control_commit": control_sha, "control_tree": control_tree, "integration_ref": INTEGRATION_REF, "read_only": True, "secret_environment": sorted(candidate_environment())}


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
