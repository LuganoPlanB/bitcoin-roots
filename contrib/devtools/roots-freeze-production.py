#!/usr/bin/env python3
"""Create and verify the private frozen Roots 29.4 production identity."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Any

CORE_COMMIT = "3fc0865963a38b871e9f7d94e6151c4953563516"
CANDIDATE_COMMIT = "cbc88cff9b35b95a549c0313e424e13093fcd6a1"
FINAL_SUBJECT = b"freeze Roots 29.4 post-candidate infrastructure\n"
FINAL_DATE = "2026-09-17T00:00:00Z"
FROZEN_REF = "refs/roots/29.4/frozen-production-g2"
SHA1 = re.compile(r"^sha1:[0-9a-f]{40}$")
SHA256 = re.compile(r"^sha256:[0-9a-f]{64}$")
MATERIAL_LINKS = {
    "inventory": "contrib/roots/post-candidate-inventory-29.4.json",
    "invariants": "contrib/roots/post-candidate-invariants-29.4.json",
    "replay": "contrib/roots/post-candidate-replay-29.4.json",
}
MATERIAL_NAMES = {
    "inventory": "post-candidate-inventory-29.4.json",
    "invariants": "post-candidate-invariants-29.4.json",
    "replay": "post-candidate-replay-29.4.json",
}
TOP_LEVEL_KEYS = {
    "approvals", "candidate_commit", "configuration_digests", "final_commit",
    "invariants", "limitations", "links", "outcomes", "patch_series_sha256",
    "provenance", "publication_authorized", "range_diff_sha256", "result_tree",
    "schema_version", "scoped_diff_sha256", "tests", "git_version",
}


class FreezeError(ValueError):
    pass


def canonical(value: dict[str, Any]) -> bytes:
    return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True).encode("utf-8") + b"\n"


def digest(value: bytes) -> str:
    return "sha256:" + hashlib.sha256(value).hexdigest()


def reject_symlink_components(path: Path, description: str) -> None:
    absolute = Path(os.path.abspath(path))
    for component in (absolute, *absolute.parents):
        if component.is_symlink():
            raise FreezeError(description + " symlink rejected")


def require_within(path: Path, root: Path, description: str, exists: bool = True) -> Path:
    root_absolute = Path(os.path.abspath(root))
    path_absolute = Path(os.path.abspath(path))
    reject_symlink_components(root_absolute, "freeze root")
    reject_symlink_components(path_absolute, description)
    if not root_absolute.is_dir():
        raise FreezeError("freeze root is not a directory")
    try:
        path_absolute.relative_to(root_absolute)
    except ValueError as error:
        raise FreezeError(description + " escapes root") from error
    if exists and not path_absolute.is_file():
        raise FreezeError(description + " is not a regular file")
    return path_absolute


def git_environment() -> dict[str, str]:
    environment = dict(os.environ)
    for key in tuple(environment):
        if key == "GIT_CONFIG_COUNT" or key.startswith("GIT_CONFIG_KEY_") or key.startswith("GIT_CONFIG_VALUE_"):
            del environment[key]
    environment.update({
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_CONFIG_GLOBAL": os.devnull,
        "GIT_AUTHOR_NAME": "Bitcoin Roots",
        "GIT_AUTHOR_EMAIL": "release@bitcoin-roots.invalid",
        "GIT_AUTHOR_DATE": FINAL_DATE,
        "GIT_COMMITTER_NAME": "Bitcoin Roots",
        "GIT_COMMITTER_EMAIL": "release@bitcoin-roots.invalid",
        "GIT_COMMITTER_DATE": FINAL_DATE,
        "GIT_NO_REPLACE_OBJECTS": "1",
        "GIT_OPTIONAL_LOCKS": "0",
    })
    return environment


def git(repository: Path, *arguments: str, input_data: bytes | None = None) -> bytes:
    result = subprocess.run(
        [
            "git", "-C", str(repository),
            "-c", "core.hooksPath=/dev/null",
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
        input=input_data, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        env=git_environment(), check=False,
    )
    if result.returncode:
        raise FreezeError("required Git input is unavailable")
    return result.stdout


def git_text(repository: Path, *arguments: str, input_data: bytes | None = None) -> str:
    return git(repository, *arguments, input_data=input_data).decode("utf-8", "strict").strip()


def require_commit(repository: Path, commit: str, description: str) -> None:
    if git_text(repository, "cat-file", "-t", commit) != "commit":
        raise FreezeError(description + " is not a commit")
    if git_text(repository, "rev-parse", "--verify", commit + "^{commit}") != commit:
        raise FreezeError(description + " differs")


def result_tree(root: Path) -> str:
    replay = require_within(root / MATERIAL_LINKS["replay"], root, "freeze replay input")
    try:
        manifest = json.loads(replay.read_text(encoding="utf-8", errors="strict"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise FreezeError("freeze replay manifest is invalid") from error
    handler = manifest.get("handler") if isinstance(manifest, dict) else None
    value = handler.get("result_tree") if isinstance(handler, dict) else None
    if not isinstance(value, str) or not SHA1.fullmatch(value):
        raise FreezeError("freeze replay result tree is invalid")
    return value[5:]


def construct_final_commit(repository: Path, tree: str) -> str:
    require_commit(repository, CANDIDATE_COMMIT, "candidate commit")
    if git_text(repository, "cat-file", "-t", tree) != "tree":
        raise FreezeError("result tree is not a tree")
    final = git_text(repository, "commit-tree", tree, "-p", CANDIDATE_COMMIT, input_data=FINAL_SUBJECT)
    if not re.fullmatch(r"[0-9a-f]{40}", final):
        raise FreezeError("deterministic final commit is invalid")
    if git_text(repository, "cat-file", "-t", final) != "commit":
        raise FreezeError("deterministic final object is not a commit")
    if git_text(repository, "rev-parse", final + "^{tree}") != tree:
        raise FreezeError("deterministic final commit tree differs")
    if git_text(repository, "show", "-s", "--format=%P", final).split() != [CANDIDATE_COMMIT]:
        raise FreezeError("deterministic final commit parent differs")
    return final


def material_digests(repository: Path, final: str) -> tuple[str, str, str]:
    series = git(
        repository, "format-patch", "--stdout", "--no-stat", "--full-index",
        "--binary", "--no-numbered", "--no-signature", "--no-renames",
        "--no-ext-diff", "--no-color", "--subject-prefix=PATCH",
        CANDIDATE_COMMIT + ".." + final,
    )
    range_diff = git(
        repository, "range-diff", "--no-color", "--no-dual-color",
        "--no-renames", "--no-ext-diff", "--creation-factor=60",
        CORE_COMMIT + ".." + CANDIDATE_COMMIT,
        CORE_COMMIT + ".." + final,
    )
    scoped = git(
        repository, "diff", "--no-ext-diff", "--no-color", "--binary",
        "--full-index", "--no-abbrev", "--no-renames", CANDIDATE_COMMIT, final,
    )
    if not series or not range_diff or not scoped:
        raise FreezeError("freeze material is vacuous")
    return digest(series), digest(range_diff), digest(scoped)


def input_digests(root: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for field, relative in MATERIAL_LINKS.items():
        path = require_within(root / relative, root, "freeze " + field + " input")
        values[MATERIAL_NAMES[field]] = digest(path.read_bytes())
    return values


def expected_record(repository: Path, root: Path) -> dict[str, Any]:
    repository = require_within(repository, root, "freeze repository", exists=False)
    if not repository.is_dir():
        raise FreezeError("freeze repository is not a directory")
    require_commit(repository, CORE_COMMIT, "Core commit")
    values = input_digests(root)
    tree = result_tree(root)
    final = construct_final_commit(repository, tree)
    series, range_diff, scoped = material_digests(repository, final)
    return {
        "approvals": ["l4-root-review-pending"],
        "candidate_commit": "sha1:" + CANDIDATE_COMMIT,
        "configuration_digests": values,
        "final_commit": "sha1:" + final,
        "git_version": git_text(repository, "version"),
        "invariants": values[MATERIAL_NAMES["invariants"]],
        "limitations": ["publication authorization remains false", "private local evidence only"],
        "links": MATERIAL_LINKS,
        "outcomes": {"inventory": values[MATERIAL_NAMES["inventory"]], "replay": values[MATERIAL_NAMES["replay"]]},
        "patch_series_sha256": series,
        "provenance": {"core_commit": "sha1:" + CORE_COMMIT, "inventory": values[MATERIAL_NAMES["inventory"]], "replay": values[MATERIAL_NAMES["replay"]]},
        "publication_authorized": False,
        "range_diff_sha256": range_diff,
        "result_tree": "sha1:" + tree,
        "schema_version": 1,
        "scoped_diff_sha256": scoped,
        "tests": [
            "ci.test.test_roots_post_candidate_inventory",
            "ci.test.test_roots_post_candidate_replay",
            "ci.test.test_roots_post_candidate_invariants",
            "ci.test.test_roots_freeze_production",
        ],
    }


def require_exact_schema(record: Any) -> None:
    if not isinstance(record, dict) or set(record) != TOP_LEVEL_KEYS:
        raise FreezeError("freeze schema keys invalid")
    if record["schema_version"] != 1:
        raise FreezeError("freeze schema version invalid")
    for field in ("candidate_commit", "final_commit", "result_tree"):
        if not isinstance(record[field], str) or not SHA1.fullmatch(record[field]):
            raise FreezeError("freeze " + field + " invalid")
    if not isinstance(record["git_version"], str) or not record["git_version"].startswith("git version "):
        raise FreezeError("freeze git_version invalid")
    for field in ("invariants", "patch_series_sha256", "range_diff_sha256", "scoped_diff_sha256"):
        if not isinstance(record[field], str) or not SHA256.fullmatch(record[field]):
            raise FreezeError("freeze " + field + " invalid")
    if not isinstance(record["publication_authorized"], bool):
        raise FreezeError("freeze publication authorization invalid")
    for field in ("approvals", "limitations", "tests"):
        values = record[field]
        if not isinstance(values, list) or not values or any(not isinstance(value, str) or not value for value in values):
            raise FreezeError("freeze " + field + " invalid")
    if not isinstance(record["configuration_digests"], dict) or set(record["configuration_digests"]) != set(MATERIAL_NAMES.values()):
        raise FreezeError("freeze configuration schema invalid")
    if any(not isinstance(value, str) or not SHA256.fullmatch(value) for value in record["configuration_digests"].values()):
        raise FreezeError("freeze configuration digest invalid")
    if not isinstance(record["links"], dict) or set(record["links"]) != set(MATERIAL_LINKS):
        raise FreezeError("freeze links schema invalid")
    for field, value in record["links"].items():
        if not isinstance(value, str):
            raise FreezeError("freeze link " + field + " type invalid")
        link = Path(value)
        if not value or link.is_absolute() or "." in link.parts or ".." in link.parts:
            raise FreezeError("freeze link " + field + " unsafe")
    if not isinstance(record["outcomes"], dict) or set(record["outcomes"]) != {"inventory", "replay"}:
        raise FreezeError("freeze outcomes schema invalid")
    if any(not isinstance(value, str) or not SHA256.fullmatch(value) for value in record["outcomes"].values()):
        raise FreezeError("freeze outcomes type invalid")
    if not isinstance(record["provenance"], dict) or set(record["provenance"]) != {"core_commit", "inventory", "replay"}:
        raise FreezeError("freeze provenance schema invalid")
    if not isinstance(record["provenance"]["core_commit"], str) or not SHA1.fullmatch(record["provenance"]["core_commit"]):
        raise FreezeError("freeze provenance core type invalid")
    if any(not isinstance(record["provenance"][field], str) or not SHA256.fullmatch(record["provenance"][field]) for field in ("inventory", "replay")):
        raise FreezeError("freeze provenance digest type invalid")


def compare_fields(actual: Any, expected: Any, path: str = "") -> None:
    if isinstance(expected, dict):
        for key in sorted(expected):
            compare_fields(actual[key], expected[key], key if not path else path + "." + key)
    elif isinstance(expected, list):
        if len(actual) != len(expected):
            raise FreezeError("freeze field " + path + " differs")
        for index, (actual_item, expected_item) in enumerate(zip(actual, expected)):
            compare_fields(actual_item, expected_item, path + "[" + str(index) + "]")
    elif actual != expected:
        raise FreezeError("freeze field " + path + " differs")


def validate_record(record: Any, repository: Path, root: Path) -> None:
    require_exact_schema(record)
    compare_fields(record, expected_record(repository, root))


def anchor_record(repository: Path, record: dict[str, Any], reference: str) -> None:
    if reference != FROZEN_REF:
        raise FreezeError("freeze anchor ref is not an owned private ref")
    final_commit = record["final_commit"][5:]
    result = subprocess.run(
        ["git", "-C", str(repository), "update-ref", reference, final_commit, "0" * 40],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=git_environment(),
        check=False,
    )
    if result.returncode:
        raise FreezeError("freeze anchor ref already exists or cannot be created")
    if git_text(repository, "rev-parse", "--verify", reference + "^{commit}") != final_commit:
        raise FreezeError("freeze anchor ref does not resolve to final commit")
    if git_text(repository, "rev-parse", reference + "^{tree}") != record["result_tree"][5:]:
        raise FreezeError("freeze anchor ref tree differs")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("build", "verify", "anchor"))
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ref", default=FROZEN_REF)
    arguments = parser.parse_args()
    try:
        root = Path(os.path.abspath(arguments.root))
        output = require_within(arguments.output, root, "freeze output", exists=False)
        if arguments.command == "build":
            if output.exists():
                raise FreezeError("refusing overwrite")
            output.write_bytes(canonical(expected_record(arguments.repository, root)))
        else:
            output = require_within(output, root, "freeze output")
            record = json.loads(output.read_text(encoding="utf-8", errors="strict"))
            validate_record(record, arguments.repository, root)
            if output.read_bytes() != canonical(expected_record(arguments.repository, root)):
                raise FreezeError("frozen identity bytes differ")
            if arguments.command == "anchor":
                repository = require_within(arguments.repository, root, "freeze repository", exists=False)
                anchor_record(repository, record, arguments.ref)
        print('{"decision":"accepted"}')
        return 0
    except (FreezeError, OSError, UnicodeError, json.JSONDecodeError) as error:
        print("roots-freeze-production: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
