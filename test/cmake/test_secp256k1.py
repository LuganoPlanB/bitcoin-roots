#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

"""Exercise required system secp256k1 API probes and package-cache changes.

Fixture libraries validate discovery, not real distribution compatibility.
"""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


APIS = {
    "secp256k1_ellswift.h": ["secp256k1_ellswift_create"],
    "secp256k1_extrakeys.h": ["secp256k1_xonly_pubkey_parse"],
    "secp256k1_recovery.h": ["secp256k1_ecdsa_recover"],
    "secp256k1_schnorrsig.h": ["secp256k1_schnorrsig_verify"],
    "secp256k1_musig.h": ["secp256k1_musig_pubkey_agg", "secp256k1_musig_pubkey_get"],
}


class SystemSecp256k1Test(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="roots-secp256k1-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.source = self.root / "source"
        self.source.mkdir()
        (self.source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.22)\nproject(Consumer LANGUAGES CXX)\n"
            'list(APPEND CMAKE_MODULE_PATH "${MODULES}")\ninclude(CheckSystemSecp256k1)\n'
            "add_executable(consumer main.cpp)\ntarget_link_libraries(consumer PRIVATE secp256k1)\n",
            encoding="utf8",
        )
        (self.source / "main.cpp").write_text(
            '#include <secp256k1_musig.h>\nint main() { return secp256k1_musig_pubkey_get(); }\n',
            encoding="utf8",
        )

    def run_command(self, command, *, environment=None, succeeds=True):
        result = subprocess.run(command, env=environment, capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, succeeds, result.stdout + result.stderr)
        return result

    def package(self, name, *, missing=None):
        prefix = self.root / name
        headers = prefix / "include"
        headers.mkdir(parents=True)
        symbols = []
        for header, functions in APIS.items():
            present = [symbol for symbol in functions if symbol != missing]
            symbols.extend(present)
            (headers / header).write_text(
                "#pragma once\n" + "".join(f"int {symbol}();\n" for symbol in present),
                encoding="utf8",
            )
        (prefix / "api.cpp").write_text(
            "".join(f"int {symbol}() {{ return 0; }}\n" for symbol in symbols), encoding="utf8"
        )
        (prefix / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.22)\nproject(Fixture LANGUAGES CXX)\n"
            "add_library(secp256k1 STATIC api.cpp)\n", encoding="utf8"
        )
        build = prefix / "lib"
        self.run_command([ARGS.cmake, "-S", str(prefix), "-B", str(build), "-G", ARGS.generator,
                          f"-DCMAKE_CXX_COMPILER={ARGS.compiler}"])
        self.run_command([ARGS.cmake, "--build", str(build), "--config", "Release", "-j2"])
        libraries = [p for p in build.rglob("*secp256k1*") if p.suffix in (".a", ".lib")]
        self.assertEqual(len(libraries), 1)
        metadata = prefix / "pkgconfig"
        metadata.mkdir()
        (metadata / "libsecp256k1.pc").write_text(
            f"Name: libsecp256k1\nDescription: isolated finder fixture\nVersion: 0.0.0\n"
            f"Libs: -L{libraries[0].parent} -lsecp256k1\nCflags: -I{headers}\n", encoding="utf8"
        )
        return metadata

    def configure(self, package, *, build="consumer", succeeds=True):
        environment = {**os.environ, "PKG_CONFIG_LIBDIR": str(package), "PKG_CONFIG_PATH": str(package)}
        result = self.run_command(
            [ARGS.cmake, "-S", str(self.source), "-B", str(self.root / build), "-G", ARGS.generator,
             f"-DCMAKE_CXX_COMPILER={ARGS.compiler}", f"-DMODULES={ARGS.modules}"],
            environment=environment, succeeds=succeeds,
        )
        if succeeds:
            self.run_command([ARGS.cmake, "--build", str(self.root / build), "--config", "Release", "-j2"])
        return result

    def test_required_apis_link_consumer(self):
        self.configure(self.package("complete"))

    def test_missing_required_apis_are_rejected(self):
        for functions in APIS.values():
            for symbol in functions:
                with self.subTest(symbol=symbol):
                    result = self.configure(self.package(symbol, missing=symbol), build=symbol, succeeds=False)
                    self.assertIn(f"APIs: {symbol}.", " ".join((result.stdout + result.stderr).split()))

    def test_package_change_refreshes_existing_cache(self):
        complete = self.package("complete")
        legacy = self.package("legacy", missing="secp256k1_musig_pubkey_get")
        self.configure(complete)
        result = self.configure(legacy, succeeds=False)
        self.assertIn("secp256k1_musig_pubkey_get", result.stdout + result.stderr)
        self.configure(complete)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--generator", required=True)
    parser.add_argument("--modules", required=True)
    ARGS, unittest_args = parser.parse_known_args()
    unittest.main(argv=[__file__, *unittest_args])
