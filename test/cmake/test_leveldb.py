#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

"""Exercise MemEnv discovery and consumer linkage with isolated fixture libraries.

These are finder regressions, not evidence of compatibility with a real system
LevelDB release. No package is installed and no host search paths are changed.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def run(command, *, succeeds=True):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if (result.returncode == 0) != succeeds:
        raise AssertionError(f"Unexpected result for {command}:\n{result.stdout}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--generator", required=True)
    parser.add_argument("--modules", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="roots-leveldb-") as directory:
        root = Path(directory)
        headers = root / "include"
        (headers / "leveldb").mkdir(parents=True)
        (headers / "leveldb/filter_policy.h").write_text("// Finder fixture.\n", encoding="utf8")
        (headers / "leveldb/env.h").write_text(
            "#pragma once\nnamespace leveldb { struct Env { virtual ~Env() = default; static Env* Default(); }; }\n",
            encoding="utf8",
        )
        (headers / "memenv.h").write_text(
            '#include <leveldb/env.h>\nnamespace leveldb { Env* NewMemEnv(Env*); }\n', encoding="utf8"
        )
        (root / "env.cpp").write_text(
            '#include <leveldb/env.h>\nleveldb::Env* leveldb::Env::Default() { static Env env; return &env; }\n',
            encoding="utf8",
        )
        (root / "memenv.cpp").write_text(
            '#include <memenv.h>\nleveldb::Env* leveldb::NewMemEnv(Env*) { return new Env; }\n', encoding="utf8"
        )
        (root / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.22)\nproject(Fixture LANGUAGES CXX)\n"
            "include_directories(include)\n"
            "add_library(integrated STATIC env.cpp memenv.cpp)\n"
            "add_library(base STATIC env.cpp)\nadd_library(helper STATIC memenv.cpp)\n", encoding="utf8"
        )
        fixture = root / "fixture"
        run([args.cmake, "-S", str(root), "-B", str(fixture), "-G", args.generator,
             f"-DCMAKE_CXX_COMPILER={args.compiler}"])
        run([args.cmake, "--build", str(fixture), "--config", "Release", "-j2"])
        # CMake controls the filename and multi-config output directory.
        def library(name):
            matches = [p for p in fixture.rglob(f"*{name}*") if p.suffix in (".a", ".lib")]
            assert len(matches) == 1, matches
            return matches[0]

        consumer = root / "consumer"
        consumer.mkdir()
        (consumer / "main.cpp").write_text(
            '#include <memenv.h>\nint main() { delete leveldb::NewMemEnv(leveldb::Env::Default()); }\n',
            encoding="utf8",
        )
        (consumer / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.22)\nproject(Consumer LANGUAGES CXX)\n"
            "list(APPEND CMAKE_MODULE_PATH \"${MODULES}\")\nfind_package(LevelDB REQUIRED)\n"
            "add_executable(consumer main.cpp)\ntarget_link_libraries(consumer PRIVATE leveldb)\n", encoding="utf8"
        )
        for name, base, helper, succeeds in [
            ("integrated", library("integrated"), "", True),
            ("split", library("base"), library("helper"), True),
            ("missing-helper", library("base"), "LevelDB_MemEnv_LIBRARY_RELEASE-NOTFOUND", False),
            ("broken-debug-helper", library("integrated"), "", False),
        ]:
            build = root / name
            command = [args.cmake, "-S", str(consumer), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release",
                       "-G", args.generator,
                       f"-DCMAKE_CXX_COMPILER={args.compiler}", f"-DMODULES={args.modules}",
                       f"-DCMAKE_FIND_ROOT_PATH={fixture}", "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY",
                       f"-DLevelDB_INCLUDE_DIR={headers}", f"-DLevelDB_MemEnv_INCLUDE_DIR={headers}",
                       f"-DLevelDB_LIBRARY_RELEASE={base}",
                       f"-DLevelDB_LIBRARY_DEBUG={library('base') if name == 'broken-debug-helper' else 'LevelDB_LIBRARY_DEBUG-NOTFOUND'}",
                       f"-DLevelDB_MemEnv_LIBRARY_RELEASE={helper}",
                       "-DLevelDB_MemEnv_LIBRARY_DEBUG=LevelDB_MemEnv_LIBRARY_DEBUG-NOTFOUND"]
            run(command, succeeds=succeeds)
            if succeeds:
                run([args.cmake, "--build", str(build), "--config", "Release", "-j2"])
            print(f"PASS: {name} discovery and {'consumer linkage' if succeeds else 'rejection'}")

        # Reuse one cache while switching between integrated and split packages.
        # A cached empty helper from an integrated package must not suppress
        # discovery of the separate helper after selecting another library.
        helper_library = library("helper")
        shutil.copyfile(helper_library, fixture / helper_library.name.replace("helper", "memenv"))
        build = root / "reconfigured"
        command = [args.cmake, "-S", str(consumer), "-B", str(build),
                   "-DCMAKE_BUILD_TYPE=Release", "-G", args.generator,
                   f"-DCMAKE_CXX_COMPILER={args.compiler}", f"-DMODULES={args.modules}",
                   f"-DCMAKE_LIBRARY_PATH={fixture}",
                   f"-DCMAKE_FIND_ROOT_PATH={fixture}", "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY",
                   f"-DLevelDB_INCLUDE_DIR={headers}", f"-DLevelDB_MemEnv_INCLUDE_DIR={headers}",
                   "-DLevelDB_LIBRARY_DEBUG=LevelDB_LIBRARY_DEBUG-NOTFOUND",
                   "-DLevelDB_MemEnv_LIBRARY_DEBUG=LevelDB_MemEnv_LIBRARY_DEBUG-NOTFOUND"]
        for index, name in enumerate(("integrated", "base", "integrated", "base")):
            # Reproduce the cache entry written by the old finder as well.
            legacy_cache = ["-DLevelDB_MemEnv_LIBRARY_RELEASE="] if index == 0 else []
            run(command + [f"-DLevelDB_LIBRARY_RELEASE={library(name)}"] + legacy_cache)
            run([args.cmake, "--build", str(build), "--config", "Release", "-j2"])
        print("PASS: cache reconfiguration integrated/split discovery and consumer linkage")


if __name__ == "__main__":
    main()
