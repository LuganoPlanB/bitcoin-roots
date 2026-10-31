#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

"""Check MiniUPnPc discovery boundaries with an isolated vcpkg layout.

The MSVC discovery branch is simulated on non-MSVC hosts.
"""

import argparse
from pathlib import Path
import subprocess
import tempfile
import unittest


class MiniUPnPcFinderTest(unittest.TestCase):
    def configure(self, *, version=17, library=True):
        with tempfile.TemporaryDirectory(prefix="roots-miniupnpc-") as directory:
            root = Path(directory)
            prefix = root / "package"
            headers = prefix / "include/miniupnpc"
            headers.mkdir(parents=True)
            (headers / "miniupnpc.h").write_text(
                f"#define MINIUPNPC_API_VERSION {version}\n", encoding="utf8"
            )
            fixture = root / "fixture"
            fixture.mkdir()
            (fixture / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.22)\nproject(Fixture LANGUAGES CXX)\n"
                f'set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "{(prefix / "lib").as_posix()}")\n'
                f'set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "{(prefix / "lib").as_posix()}")\n'
                "add_library(miniupnpc STATIC api.cpp)\n", encoding="utf8"
            )
            (fixture / "api.cpp").write_text("int fixture() { return 0; }\n", encoding="utf8")
            if library:
                for command in [
                    [ARGS.cmake, "-S", str(fixture), "-B", str(root / "fixture-build"), "-G", ARGS.generator,
                     f"-DCMAKE_CXX_COMPILER={ARGS.compiler}"],
                    [ARGS.cmake, "--build", str(root / "fixture-build"), "--config", "Release", "-j2"],
                ]:
                    result = subprocess.run(command, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            source = root / "source"
            source.mkdir()
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.22)\nproject(Consumer LANGUAGES CXX)\n"
                'list(APPEND CMAKE_MODULE_PATH "${MODULES}")\nset(MSVC TRUE)\n'
                "find_package(MiniUPnPc REQUIRED)\n"
                'get_target_property(defs MiniUPnPc::MiniUPnPc INTERFACE_COMPILE_DEFINITIONS)\n'
                'if(NOT "USE_UPNP=1" IN_LIST defs)\nmessage(FATAL_ERROR "Missing UPnP definition")\nendif()\n',
                encoding="utf8",
            )
            return subprocess.run(
                [ARGS.cmake, "-S", str(source), "-B", str(root / "build"), "-G", ARGS.generator,
                 f"-DCMAKE_CXX_COMPILER={ARGS.compiler}", f"-DMODULES={ARGS.modules}",
                 f"-DMiniUPnPc_INCLUDE_DIR={prefix / 'include'}"], capture_output=True, text=True
            )

    def test_supported_api_with_library_is_found(self):
        result = self.configure()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_old_api_is_rejected(self):
        result = self.configure(version=16)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("MiniUPnPc_API_VERSION_OK", result.stdout + result.stderr)

    def test_headers_without_library_are_rejected(self):
        result = self.configure(library=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("MiniUPnPc_LIBRARY", result.stdout + result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--generator", required=True)
    parser.add_argument("--modules", required=True)
    ARGS, unittest_args = parser.parse_known_args()
    unittest.main(argv=[__file__, *unittest_args])
