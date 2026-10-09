#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.

"""Fail-closed source and report gates for the external paired-node CI lane."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CASES = {"smoke": ["ordinary", "datacarrier", "invalid-block"],
         "full": ["ordinary", "datacarrier", "invalid-block", "sigop", "subdust", "lifecycle"]}
ADMISSIONS = {
    "ordinary": [("ordinary", True, True)],
    "datacarrier": [("at-83-bytes", True, True), ("aggregate-84-bytes", True, False)],
    "invalid-block": [],
    "sigop": [("2490-legacy-sigops", True, True), ("2505-legacy-sigops", False, False)],
    "subdust": [("below-penalty-threshold", True, False), ("at-penalty-threshold", True, True)],
    "lifecycle": [("persistence-control", True, True)],
}
OFFICIAL_CORE = "https://github.com/bitcoin/bitcoin"
MAX_REPORT_BYTES = 2 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def identity(value):
    require(isinstance(value, str) and re.fullmatch(r"[0-9a-f]{40}", value),
            "identity must be a full lowercase 40-hex commit")
    return value


def git(source, *args):
    result = subprocess.run(["git", "-C", str(source), *args], capture_output=True, text=True, timeout=60)
    require(result.returncode == 0, "Git source verification failed")
    require(len(result.stdout) <= MAX_REPORT_BYTES, "Git source output exceeds limit")
    return result.stdout.strip()


def generation(context_ref):
    require(isinstance(context_ref, str) and ".." not in context_ref, "invalid generation branch")
    match = re.fullmatch(r"(?:refs/heads/)?(?:roots|topic|integration|promote)/(\d+\.\d+(?:\.\d+)?)(?:[/\-][A-Za-z0-9_-]+)?", context_ref)
    require(match is not None, "unknown or ambiguous Core generation; use a reviewed generation branch")
    return match[1]


def candidate_generation(source, commit):
    metadata = git(source, "show", commit + ":CMakeLists.txt")
    components = [re.findall(r"(?m)^set\(CLIENT_VERSION_" + key + r" ([0-9]+)\)$", metadata)
                  for key in ("MAJOR", "MINOR")]
    require(all(len(values) == 1 for values in components), "ambiguous candidate generation metadata")
    return ".".join(values[0] for values in components)


def resolve_sources(roots_source, core_source, candidate, core_sha, context_ref, release_tag=""):
    identity(candidate)
    identity(core_sha)
    version = generation(context_ref)
    require(version == "30.3", "fixture expectations are qualified only for Core 30.3")
    require(candidate_generation(roots_source, candidate) == version, "candidate generation mismatch")
    require(git(roots_source, "rev-parse", "HEAD") == candidate, "candidate checkout mismatch")
    require(git(core_source, "rev-parse", "HEAD") == core_sha, "Core checkout mismatch")
    remote = git(core_source, "remote", "get-url", "origin").removesuffix(".git").rstrip("/")
    require(remote == OFFICIAL_CORE, "Core checkout must come from the official bitcoin/bitcoin repository")
    # This ref is fetched directly from official Core by the workflow, never from Roots tags.
    require(git(core_source, "rev-parse", "refs/tags/v" + version + "^{commit}") == core_sha,
            "Core SHA does not match the official generation tag")
    git(roots_source, "merge-base", "--is-ancestor", core_sha, candidate)
    canonical_ref = "refs/remotes/origin/roots/" + version
    canonical = identity(git(roots_source, "rev-parse", canonical_ref + "^{commit}"))
    git(roots_source, "merge-base", "--is-ancestor", core_sha, canonical)
    require(core_sha != candidate, "candidate has no Roots changes")
    if release_tag:
        require(re.fullmatch(r"v" + re.escape(version) + r"-roots\.\d+", release_tag), "release generation mismatch")
        # Reuse the established annotated-tag, exact canonical tip and linear-stack gate.
        result = subprocess.run([str(ROOT / "ci/release/validate-release-source.sh"), release_tag],
                                cwd=roots_source, capture_output=True, timeout=60,
                                env={**os.environ, "RELEASE_CORE_REF": core_sha,
                                     "RELEASE_CANONICAL_REF": canonical_ref})
        require(result.returncode == 0, "release-source validation failed")
    return {"candidate_sha": candidate, "core_sha": core_sha, "generation": version,
            "canonical_sha": canonical, "context_ref": context_ref,
            "tag_authentication": "not established; commit pinning only"}


def prepare_inputs(roots_source, candidate, context_ref):
    """Derive the official base from reviewed generation refs, never newest Core."""
    identity(candidate)
    require(git(roots_source, "rev-parse", "HEAD") == candidate, "candidate checkout mismatch")
    refs = git(roots_source, "for-each-ref", "--format=%(refname)", "refs/remotes/origin/roots/").splitlines()
    versions = []
    for ref in refs:
        match = re.fullmatch(r"refs/remotes/origin/roots/(\d+\.\d+(?:\.\d+)?)", ref)
        if match:
            versions.append(match[1])
    require(0 < len(versions) <= 10, "canonical generation inventory unavailable or unbounded")
    try:
        requested = generation(context_ref)
    except ValueError:
        # Committed package metadata narrows default-branch generation selection;
        # official tag pinning plus canonical/candidate ancestry still prove base.
        # A malformed generation-looking ref must not fall back silently.
        require(context_ref in ("main", "refs/heads/main"), "unknown generation context")
        requested = candidate_generation(roots_source, candidate)
        require(requested in versions, "candidate generation has no reviewed canonical branch")
    require(requested == "30.3", "fixture expectations are qualified only for Core 30.3")
    require(candidate_generation(roots_source, candidate) == requested, "candidate generation mismatch")
    matches = []
    for version in versions:
        if requested is not None and version != requested:
            continue
        fetched = "refs/compatibility/core/" + version
        git(roots_source, "fetch", "--no-tags", OFFICIAL_CORE + ".git", "refs/tags/v" + version + ":" + fetched)
        core_sha = identity(git(roots_source, "rev-parse", fetched + "^{commit}"))
        try:
            git(roots_source, "merge-base", "--is-ancestor", core_sha, candidate)
            git(roots_source, "merge-base", "--is-ancestor", core_sha, "refs/remotes/origin/roots/" + version)
        except ValueError:
            continue
        matches.append((version, core_sha))
    require(len(matches) == 1, "unknown or ambiguous matching Core generation")
    version, core_sha = matches[0]
    require(version == "30.3", "fixture expectations are qualified only for Core 30.3")
    return {"candidate_sha": candidate, "core_sha": core_sha, "context_ref": "roots/" + version}


def artifact_name(candidate, core_sha, profile):
    identity(candidate)
    identity(core_sha)
    require(profile in CASES, "unknown profile")
    return "block-compatibility-" + profile + "-" + candidate + "-" + core_sha


def file_digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_report(path):
    require(Path(path).stat().st_size <= MAX_REPORT_BYTES, "report exceeds limit")
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, "duplicate report key")
            result[key] = value
        return result
    return json.loads(Path(path).read_text(), object_pairs_hook=unique,
                      parse_constant=lambda _: require(False, "nonfinite report number"))


def validate_report(report, roots_source, core_source, candidate, core_sha, profile):
    identity(candidate)
    identity(core_sha)
    require(profile in CASES, "unknown profile")
    require(isinstance(report, dict), "report must be an object")
    require(type(report.get("schema_version")) is int and report.get("schema_version") == 1 and report.get("status") == "passed"
            and report.get("qualified") is True and "error" not in report, "report did not qualify")
    require(report.get("profile") == profile and report.get("required_cases") == CASES[profile], "profile accounting mismatch")
    cases = report.get("cases", [])
    require([case.get("id") for case in cases] == CASES[profile]
            and all(case.get("status") == "passed" for case in cases), "missing, skipped or failed cases")
    for case in cases:
        require(case.get("states"), "missing paired chain state")
        require(len(case.get("blocks", [])) == {"sigop": 2, "lifecycle": 0}.get(case["id"], 1),
                "missing block observations")
        expected = ADMISSIONS[case["id"]]
        require([(item["name"], item["expected_allowed"]["core"], item["expected_allowed"]["roots"])
                 for item in case.get("admission", [])] == expected, "fixture admission contract mismatch")
        for admission in case.get("admission", []):
            require(set(admission["expected_allowed"]) == {"core", "roots"}, "missing admission expectation")
            for label in ("core", "roots"):
                require(isinstance(admission["expected_allowed"][label], bool)
                        and admission["observed"][label]["allowed"] is admission["expected_allowed"][label],
                        "admission outcome mismatch")
        for block in case.get("blocks", []):
            require(set(block["outcomes"]) == {"core", "roots"}, "missing block outcome")
            require(block["expected_valid"] is (case["id"] != "invalid-block"), "wrong block expectation")
            require(all((outcome is None) if block["expected_valid"] else isinstance(outcome, str) and bool(outcome)
                        for outcome in block["outcomes"].values()), "divergent block acceptance")
    if profile == "full":
        lifecycle = cases[-1]
        require([step["fixture"] for step in lifecycle["transitions"]] == ["datacarrier", "subdust"],
                "missing lifecycle transitions")
        require(lifecycle["restart"]["control_persisted"] is True
                and lifecycle["restart"]["same_owned_datadirs"] is True, "restart verification incomplete")
    spec = importlib.util.spec_from_file_location("compatibility_build", ROOT / "contrib/roots/compatibility/build.py")
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    require(report.get("harness_sha256") == file_digest(roots_source / "contrib/roots/compatibility/compare.py"), "wrong harness revision")
    require(report.get("fixture_revision", {}).get("sha256") == file_digest(roots_source / "contrib/roots/compatibility/fixtures.py"), "wrong fixture revision")
    digests = []
    require(set(report.get("nodes", {})) == {"core", "roots"}, "wrong node inventory")
    for label, source, commit in (("core", core_source, core_sha), ("roots", roots_source, candidate)):
        node = report["nodes"][label]
        tree = git(source, "rev-parse", commit + "^{tree}")
        require(node["source"]["commit"] == commit and node["source"]["tree"] == tree, "source report mismatch")
        build = node["build"]
        binary = node["binary"]
        provenance = binary["provenance"]
        require(build["status"] == provenance["status"] == "built-in-run", "unverified binary provenance")
        require(build["source_commit"] == provenance["source_commit"] == commit
                and build["source_tree"] == provenance["source_tree"] == tree, "build identity mismatch")
        require(build["builder_sha256"] == file_digest(roots_source / "contrib/roots/compatibility/build.py"), "builder revision mismatch")
        require(build["configuration"] == list(builder.CMAKE_OPTIONS)
                and node["build_configuration"] == build["configuration"], "build configuration mismatch")
        require(build["archive_sha256"] == provenance["archive_sha256"], "export identity mismatch")
        for field in ("archive_sha256", "cmake_cache_sha256", "binary_sha256"):
            require(re.fullmatch(r"[0-9a-f]{64}", build[field]), "missing build digest")
        require(binary["sha256"] == build["binary_sha256"], "binary digest mismatch")
        commands = build["commands"]
        require(len(commands) == 5 and all(step["status"] == "passed" for step in commands), "incomplete build commands")
        require(commands[0]["argv"][0] == "git" and commands[0]["argv"][-1] == commit
                and commands[1]["argv"][0] == "cmake" and commands[2]["argv"][:2] == ["cmake", "--build"], "wrong build commands")
        require(node["cleanup"]["stopped"] is True and node["cleanup"]["returncode"] == 0, "node cleanup failed")
        digests.append(binary["sha256"])
    require(digests[0] != digests[1], "identical binaries cannot qualify")
    serialization = report["serialization"]
    require(serialization["source_commit"] == candidate and serialization["files"], "serialization source mismatch")
    for entry in serialization["files"]:
        path = Path(entry["path"])
        require(not path.is_absolute() and ".." not in path.parts
                and path.as_posix().startswith("test/functional/test_framework/"), "unsafe serialization path")
        require(entry["sha256"] == file_digest(roots_source / path), "serialization revision mismatch")


def prepare_artifacts(report_path, output):
    """Upload only bounded JSON; exclude raw command logs and any credential-like text."""
    output.mkdir(parents=True, exist_ok=False)
    status = os.environ.get("JOB_STATUS", "unknown")
    summary = {"job_status": status if status in ("success", "failure", "cancelled") else "unknown",
               "report": "missing-or-invalid"}
    for key in ("run_id", "run_attempt"):
        value = os.environ.get("GITHUB_" + key.upper(), "local")
        summary[key] = value if re.fullmatch(r"[0-9]{1,40}", value) else "local"
    for key in ("candidate_sha", "core_sha"):
        value = os.environ.get(key.upper(), "")
        if re.fullmatch(r"[0-9a-f]{40}", value):
            summary[key] = value
    if os.environ.get("PROFILE") in CASES:
        summary["profile"] = os.environ["PROFILE"]
    source_path = report_path.parent / "sources.json"
    try:
        require(not source_path.is_symlink(), "source evidence symlink refused")
        sources = load_report(source_path)
        identity(sources["candidate_sha"])
        identity(sources["core_sha"])
        identity(sources["canonical_sha"])
        require(generation(sources["context_ref"]) == sources["generation"], "source evidence mismatch")
        public_sources = {key: sources[key] for key in
                          ("candidate_sha", "core_sha", "canonical_sha", "generation", "context_ref")}
        public_sources["tag_authentication"] = "not established; commit pinning only"
        (output / "sources.json").write_text(json.dumps(public_sources, indent=2) + "\n")
    except (ValueError, OSError, TypeError, KeyError):
        pass
    try:
        require(not report_path.is_symlink(), "report symlink refused")
        report = load_report(report_path)
        require(isinstance(report, dict), "invalid report object")
        def sanitize(value, depth=0):
            require(depth <= 30, "report nesting exceeds limit")
            if isinstance(value, dict):
                return {key: sanitize(item, depth + 1) for key, item in value.items()
                        if re.fullmatch(r"[A-Za-z0-9_./-]{1,160}", key)
                        and not re.search(r"(?i)(authorization|password|rpcauth|cookie|token|secret)", key)}
            if isinstance(value, list):
                return [sanitize(item, depth + 1) for item in value]
            if isinstance(value, str):
                require(len(value) <= 16384, "report string exceeds limit")
                # Treat credential-bearing lines as opaque, including cookie paths.
                value = "\n".join("[redacted credential-like line]" if re.search(
                    r"(?i)(authorization|rpcpassword|rpcauth|cookie|gh[pousr]_|github_pat_|https?://[^\s/]+@)", line)
                                    else line for line in value.split("\n"))
            return value
        sanitized = sanitize(report)
        (output / "report.json").write_text(json.dumps(sanitized, indent=2) + "\n")
        summary["report"] = "bounded-sanitized"
    except (ValueError, OSError, TypeError, RecursionError):
        pass
    (output / "status.json").write_text(json.dumps(summary, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("inputs", "sources", "report", "name", "artifacts"))
    parser.add_argument("--candidate-sha", required=True)
    parser.add_argument("--core-sha")
    parser.add_argument("--roots-source", type=Path)
    parser.add_argument("--core-source", type=Path)
    parser.add_argument("--context-ref")
    parser.add_argument("--release-tag", default="")
    parser.add_argument("--profile", choices=CASES, default="smoke")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        if args.mode == "inputs":
            evidence = prepare_inputs(args.roots_source, args.candidate_sha, args.context_ref)
            require(args.output is not None, "GitHub output required")
            with args.output.open("a") as output:
                for key, value in evidence.items():
                    output.write(key + "=" + value + "\n")
        elif args.mode == "artifacts":
            prepare_artifacts(args.report, args.output)
        elif args.mode == "name":
            print(artifact_name(args.candidate_sha, args.core_sha, args.profile))
        elif args.mode == "sources":
            evidence = resolve_sources(args.roots_source, args.core_source, args.candidate_sha,
                                       args.core_sha, args.context_ref, args.release_tag)
            require(args.output is not None, "source evidence output required")
            args.output.write_text(json.dumps(evidence, indent=2) + "\n")
        else:
            validate_report(load_report(args.report), args.roots_source, args.core_source,
                            args.candidate_sha, args.core_sha, args.profile)
    except (ValueError, OSError, KeyError, TypeError, AttributeError, IndexError, RecursionError, subprocess.SubprocessError):
        # Never print untrusted report data, paths, Git credentials or subprocess output.
        print("Compatibility gate failed: invalid or unavailable source/evidence", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
