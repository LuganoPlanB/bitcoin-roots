#!/usr/bin/env python3
"""Build and test an exact tree produced by trusted Roots replay."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


OID = re.compile(r"[0-9a-f]{40}")


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


def build_and_test(repository, expected_tree, build_directory, jobs):
    validate_candidate(repository, expected_tree)
    if not build_directory.is_absolute() or build_directory.exists() or build_directory.is_symlink():
        raise ValueError("candidate build directory must be a new absolute path")
    if jobs < 1 or jobs > 4:
        raise ValueError("candidate build parallelism is invalid")
    environment = dict(os.environ)
    environment.update({"LC_ALL": "C", "LANG": "C", "TZ": "UTC"})
    commands = (
        ["cmake", "-S", str(repository), "-B", str(build_directory), "-DENABLE_WALLET=ON", "-DWITH_BDB=OFF", "-DBUILD_GUI=OFF", "-DBUILD_TESTS=ON"],
        ["cmake", "--build", str(build_directory), "-j", str(jobs)],
        ["ctest", "--test-dir", str(build_directory), "--output-on-failure"],
        [sys.executable, str(build_directory / "test/functional/test_runner.py"), f"--jobs={jobs}", "feature_block.py", "p2p_segwit.py", "mempool_datacarrier.py"],
    )
    for command in commands:
        result = subprocess.run(command, check=False, env=environment)
        if result.returncode:
            raise ValueError(f"candidate acceptance command failed: {command[0]}")
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
