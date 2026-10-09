# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Fresh out-of-source builds from immutable Git exports, without binary caches."""

import hashlib
from pathlib import Path
import tarfile
import time

CMAKE_OPTIONS = (
    "-DCMAKE_BUILD_TYPE=Release", "-DENABLE_WALLET=OFF", "-DENABLE_IPC=OFF", "-DBUILD_GUI=OFF",
    "-DBUILD_TESTS=OFF", "-DBUILD_BENCH=OFF", "-DBUILD_FUZZ_BINARY=OFF", "-DWITH_CCACHE=OFF",
    "-DCMAKE_C_COMPILER_LAUNCHER=", "-DCMAKE_CXX_COMPILER_LAUNCHER=",
)


def extract_source(archive, directory, error):
    directory.mkdir()
    root = directory.resolve()
    with tarfile.open(archive) as stream:
        for member in stream:
            path = directory / member.name
            if not path.resolve().is_relative_to(root):
                raise error("source archive path escapes snapshot")
            if member.isdir():
                path.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                path.parent.mkdir(parents=True, exist_ok=True)
                with stream.extractfile(member) as source, path.open("xb") as destination:
                    while chunk := source.read(1024 * 1024):
                        destination.write(chunk)
                path.chmod(0o755 if member.mode & 0o111 else 0o644)
            elif member.issym():
                if not (path.parent / member.linkname).resolve().is_relative_to(root):
                    raise error("source archive symlink escapes snapshot")
                path.parent.mkdir(parents=True, exist_ok=True)
                path.symlink_to(member.linkname)
            else:
                raise error("unsupported source archive entry")


def build_source(source, directory, timeout, jobs, command, error, record):
    directory = Path(directory)
    directory.mkdir()
    archive = directory / "source.tar"
    snapshot = directory / "source"
    build = directory / "build"
    evidence = record["build"] = {"status": "building", "source_commit": source["commit"],
                                  "builder_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                                  "source_tree": source["tree"], "commands": [],
                                  "configuration": list(CMAKE_OPTIONS), "jobs": jobs,
                                  "cache": "none; fresh directory; compiler launchers disabled"}
    started = time.monotonic()
    deadline = started + timeout

    def execute(args):
        step = {"argv": args, "status": "running"}
        evidence["commands"].append(step)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise error("pinned-source build timed out")
        try:
            output = command(args, timeout=remaining)
            step["status"] = "passed"
            # Fixed flags and no secrets are used for these commands. Retain only
            # bounded tails, not a whole build log in the run report.
            step["diagnostic_tail"] = output[-4096:]
        except Exception as failure:
            step["status"] = "failed"
            step["error"] = str(failure) if isinstance(failure, error) else type(failure).__name__
            step["diagnostic_tail"] = getattr(failure, "diagnostic_tail", "")
            raise
        return output

    try:
        execute(["git", "-C", source["path"], "archive", "--format=tar", "--output=" + str(archive), source["commit"]])
        evidence["archive_sha256"] = hashlib.sha256(archive.read_bytes()).hexdigest()
        extract_source(archive, snapshot, error)
        execute(["cmake", "-S", str(snapshot), "-B", str(build), *CMAKE_OPTIONS])
        execute(["cmake", "--build", str(build), "--target", "bitcoind", "-j", str(jobs)])
        cache = (build / "CMakeCache.txt").read_text()
        evidence["cmake_cache_sha256"] = hashlib.sha256(cache.encode()).hexdigest()
        keys = ("CMAKE_CXX_COMPILER", "CMAKE_C_COMPILER", "CMAKE_BUILD_TYPE", "CMAKE_CXX_FLAGS", "CMAKE_C_FLAGS")
        evidence["effective_configuration"] = {line.split(":", 1)[0]: line.split("=", 1)[1]
                                               for line in cache.splitlines()
                                               if ":" in line and "=" in line and line.split(":", 1)[0] in keys}
        for key in ("CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER"):
            compiler = evidence["effective_configuration"].get(key)
            if not compiler:
                raise error("build compiler identity is missing")
            evidence.setdefault("compiler_versions", {})[key] = execute([compiler, "--version"])[:4096]
        evidence["snapshot_path"] = str(snapshot)
        evidence["status"] = "built-in-run"
        return build / "bin" / "bitcoind", evidence
    except Exception:
        evidence["status"] = "failed"
        raise
    finally:
        evidence["duration_seconds"] = round(time.monotonic() - started, 6)
