#!/usr/bin/env python3
"""Build and test an exact tree produced by trusted Roots replay."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


OID = re.compile(r"[0-9a-f]{40}")
LAUNCHER = re.compile(r"^CMAKE_(?:C|CXX)_COMPILER_LAUNCHER:.*=ccache$", re.MULTILINE)


def output(command):
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode:
        raise ValueError("candidate Git verification failed")
    return result.stdout.strip()


def validate_candidate(repository, expected_tree):
    if not repository.is_absolute() or not repository.is_dir() or repository.is_symlink():
        raise ValueError("candidate repository is invalid")
    if not OID.fullmatch(expected_tree):
        raise ValueError("candidate tree lock is invalid")
    actual = output(["git", "-C", str(repository), "write-tree"])
    if actual != expected_tree:
        raise ValueError("candidate tree differs from replay lock")


def verify_launchers(build_directory):
    cache = build_directory / "CMakeCache.txt"
    if not cache.is_file() or len(LAUNCHER.findall(cache.read_text(encoding="utf-8"))) != 2:
        raise ValueError("candidate CMake compiler launchers are not ccache")


def candidate_environment(build_directory):
    """Candidate commands receive only an isolated, writable ccache path."""
    return {"PATH": os.environ.get("PATH", ""), "HOME": os.devnull, "CCACHE_DIR": str(build_directory / ".ccache"), "LC_ALL": "C", "LANG": "C", "TZ": "UTC", "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": os.devnull, "GIT_NO_REPLACE_OBJECTS": "1", "GIT_TERMINAL_PROMPT": "0"}


def build_and_test(repository, expected_tree, build_directory, jobs):
    validate_candidate(repository, expected_tree)
    if not build_directory.is_absolute() or build_directory.exists() or build_directory.is_symlink():
        raise ValueError("candidate build directory must be a new absolute path")
    if jobs < 1 or jobs > 4:
        raise ValueError("candidate build parallelism is invalid")
    # Candidate-controlled CMake and tests never inherit workflow credentials.
    environment = candidate_environment(build_directory)
    commands = (
        ["cmake", "-S", str(repository), "-B", str(build_directory), "-DCMAKE_C_COMPILER_LAUNCHER=ccache", "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache", "-DENABLE_WALLET=ON", "-DWITH_BDB=OFF", "-DBUILD_GUI=OFF", "-DBUILD_TESTS=ON"],
        ["cmake", "--build", str(build_directory), "-j", str(jobs)],
        ["ctest", "--test-dir", str(build_directory), "--output-on-failure"],
        [sys.executable, str(build_directory / "test/functional/test_runner.py"), f"--jobs={jobs}", "feature_block.py", "p2p_segwit.py", "mempool_datacarrier.py"],
    )
    for index, command in enumerate(commands):
        result = subprocess.run(command, check=False, env=environment)
        if result.returncode:
            raise ValueError(f"candidate acceptance command failed: {command[0]}")
        if index == 0:
            verify_launchers(build_directory)
    validate_candidate(repository, expected_tree)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--expected-tree", required=True)
    parser.add_argument("--build-directory", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    try:
        build_and_test(args.repository, args.expected_tree, args.build_directory, args.jobs)
    except (OSError, ValueError) as error:
        raise SystemExit(f"roots-trusted-candidate-build: {error}") from error


if __name__ == "__main__":
    main()
