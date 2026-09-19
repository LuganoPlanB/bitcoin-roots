#!/usr/bin/env python3
"""Resolve one immutable release source without executing candidate code."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


CONTROL_REF = "refs/heads/codex/roots-29-4-release"
CANDIDATE_REF = "refs/heads/integration/roots-29.4"
MAIN_REF = "refs/heads/main"
RELEASE_TAG = "refs/tags/v29.4-roots.1"
RELEASE_WORKFLOW = ".github/workflows/release.yml"
OID = re.compile(r"^[0-9a-f]{40}$")


class SourceError(ValueError):
    pass


def git_environment() -> dict[str, str]:
    environment = dict(os.environ)
    for key in tuple(environment):
        if key == "GIT_CONFIG_COUNT" or key.startswith(("GIT_CONFIG_KEY_", "GIT_CONFIG_VALUE_")):
            del environment[key]
    environment.update({
        "GIT_CONFIG_GLOBAL": os.devnull,
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_CONFIG_SYSTEM": os.devnull,
        "GIT_NO_REPLACE_OBJECTS": "1",
        "GIT_OPTIONAL_LOCKS": "0",
        "LC_ALL": "C",
        "TZ": "UTC",
    })
    environment.pop("GIT_ALTERNATE_OBJECT_DIRECTORIES", None)
    return environment


def git(repository: Path, *arguments: str, check: bool = True) -> str:
    result = subprocess.run(
        [
            "git", "-C", str(repository),
            "-c", "credential.helper=",
            "-c", "core.hooksPath=/dev/null",
            "-c", "core.pager=cat",
            *arguments,
        ],
        check=False,
        capture_output=True,
        env=git_environment(),
        text=True,
    )
    if check and result.returncode:
        raise SourceError("required Git source is unavailable")
    return result.stdout.strip()


def oid(value: str, name: str) -> str:
    if not OID.fullmatch(value):
        raise SourceError(f"{name} must be a full commit ID")
    return value


def reject_host_state(repository: Path, event_sha: str) -> None:
    if not repository.is_dir() or repository.is_symlink():
        raise SourceError("trusted control checkout must be a real directory")
    git_dir = Path(git(repository, "rev-parse", "--absolute-git-dir"))
    if not git_dir.is_dir() or git_dir.is_symlink():
        raise SourceError("trusted control Git directory is invalid")
    if os.environ.get("GIT_ALTERNATE_OBJECT_DIRECTORIES") or (git_dir / "objects/info/alternates").exists():
        raise SourceError("Git alternates are forbidden")
    if (git_dir / "info/grafts").exists() or git(repository, "for-each-ref", "--format=%(refname)", "refs/replace"):
        raise SourceError("Git grafts and replacements are forbidden")
    if (git_dir / "shallow").exists():
        raise SourceError("trusted control checkout must not be shallow")
    if git(repository, "config", "--local", "--get", "rerere.enabled", check=False).lower() in {"1", "true", "yes", "on"}:
        raise SourceError("Git rerere is forbidden")
    hooks_path = git(repository, "config", "--local", "--get", "core.hooksPath", check=False)
    if hooks_path:
        raise SourceError("configured Git hooks are forbidden")
    hooks = git_dir / "hooks"
    if hooks.is_dir() and any(path.is_file() and not path.name.endswith(".sample") for path in hooks.iterdir()):
        raise SourceError("active Git hooks are forbidden")
    if git(repository, "status", "--porcelain=v1"):
        raise SourceError("trusted control checkout must be clean")
    if git(repository, "rev-parse", "HEAD^{commit}") != event_sha:
        raise SourceError("trusted control checkout differs from event SHA")


def fetch_ref(repository: Path, remote: str, reference: str, destination: str) -> None:
    if not remote or remote.startswith("-") or any(character in remote for character in "\r\n"):
        raise SourceError("release remote is invalid")
    git(repository, "fetch", "--no-tags", "--depth=1", remote, f"{reference}:{destination}")


def resolve(
    *,
    trusted_repository: Path,
    remote: str,
    event_name: str,
    event_ref: str,
    event_sha: str,
    candidate_commit: str = "",
    candidate_tree: str = "",
    control_sha: str = "",
    control_tree: str = "",
) -> dict[str, str | bool]:
    event_sha = oid(event_sha, "event SHA")
    rehearsal = event_name == "workflow_dispatch" and event_ref == CONTROL_REF
    supplied = (candidate_commit, candidate_tree, control_sha, control_tree)
    if rehearsal:
        candidate_commit = oid(candidate_commit, "candidate commit")
        candidate_tree = oid(candidate_tree, "candidate tree")
        control_sha = oid(control_sha, "control SHA")
        control_tree = oid(control_tree, "control tree")
        if control_sha != event_sha:
            raise SourceError("declared control SHA differs from event SHA")
    elif any(supplied):
        raise SourceError("candidate inputs are only valid for a control-branch rehearsal")
    elif not (
        (event_name == "push" and event_ref == MAIN_REF)
        or (event_name == "workflow_dispatch" and event_ref == RELEASE_TAG)
    ):
        raise SourceError("release event/ref combination is not allowed")
    reject_host_state(trusted_repository, event_sha)
    with tempfile.TemporaryDirectory(prefix="roots-release-source-") as temporary:
        repository = Path(temporary) / "repository"
        repository.mkdir()
        git(repository, "init", "--quiet")
        if rehearsal:
            fetch_ref(repository, remote, CONTROL_REF, "refs/release/control")
            fetch_ref(repository, remote, CANDIDATE_REF, "refs/release/candidate")
            if git(repository, "rev-parse", "refs/release/control^{commit}") != control_sha:
                raise SourceError("remote control ref differs")
            if git(repository, "rev-parse", "refs/release/control^{tree}") != control_tree:
                raise SourceError("control tree differs")
            if git(repository, "rev-parse", "refs/release/candidate^{commit}") != candidate_commit:
                raise SourceError("remote candidate ref differs")
            if git(repository, "rev-parse", "refs/release/candidate^{tree}") != candidate_tree:
                raise SourceError("candidate tree differs")
            control_workflow = git(repository, "rev-parse", "refs/release/control:" + RELEASE_WORKFLOW)
            candidate_workflow = git(repository, "rev-parse", "refs/release/candidate:" + RELEASE_WORKFLOW)
            if (
                not OID.fullmatch(control_workflow)
                or git(repository, "cat-file", "-t", control_workflow) != "blob"
                or candidate_workflow != control_workflow
            ):
                raise SourceError("candidate and control release workflow blobs differ")
            source_commit, source_tree = candidate_commit, candidate_tree
        else:
            if event_name == "push" and event_ref == MAIN_REF:
                source_ref = MAIN_REF
            else:
                source_ref = RELEASE_TAG
            fetch_ref(repository, remote, source_ref, "refs/release/source")
            source_commit = git(repository, "rev-parse", "refs/release/source^{commit}")
            source_tree = git(repository, "rev-parse", "refs/release/source^{tree}")
            if source_commit != event_sha:
                raise SourceError("event SHA differs from resolved release source")
    return {"rehearsal": rehearsal, "source_commit": source_commit, "source_tree": source_tree}


def write_github_output(path: Path, result: dict[str, str | bool]) -> None:
    if path.is_symlink() or not path.parent.is_dir():
        raise SourceError("GitHub output path is invalid")
    with path.open("a", encoding="utf-8") as output:
        output.write(f"source_commit={result['source_commit']}\n")
        output.write(f"source_tree={result['source_tree']}\n")
        output.write(f"rehearsal={'true' if result['rehearsal'] else 'false'}\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trusted-repository", type=Path, required=True)
    parser.add_argument("--remote", required=True)
    parser.add_argument("--event-name", required=True)
    parser.add_argument("--event-ref", required=True)
    parser.add_argument("--event-sha", required=True)
    parser.add_argument("--candidate-commit", default="")
    parser.add_argument("--candidate-tree", default="")
    parser.add_argument("--control-sha", default="")
    parser.add_argument("--control-tree", default="")
    parser.add_argument("--github-output", type=Path)
    arguments = parser.parse_args()
    try:
        result = resolve(
            trusted_repository=arguments.trusted_repository,
            remote=arguments.remote,
            event_name=arguments.event_name,
            event_ref=arguments.event_ref,
            event_sha=arguments.event_sha,
            candidate_commit=arguments.candidate_commit,
            candidate_tree=arguments.candidate_tree,
            control_sha=arguments.control_sha,
            control_tree=arguments.control_tree,
        )
        if arguments.github_output is not None:
            write_github_output(arguments.github_output, result)
        print(json.dumps(result, sort_keys=True))
        return 0
    except (OSError, SourceError, UnicodeError) as error:
        print("resolve-release-source: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
