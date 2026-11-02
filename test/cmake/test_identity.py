#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Exercise the production Roots release version block without Git metadata."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--cmake", default="cmake")
args, unittest_args = parser.parse_known_args()


class BuildIdentityTest(unittest.TestCase):
    def configure(self, tag=None):
        config = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        start = re.search(r"^set\(CLIENT_VERSION_STRING ", config, re.MULTILINE)
        self.assertIsNotNone(start)
        version_block = config[start.start():].split("#=============================", 1)[0]
        metadata = "\n".join(
            re.search(rf"set\({name} [^)]+\)", config).group(0)
            for name in ("CLIENT_NAME", "CLIENT_VERSION_MAJOR", "CLIENT_VERSION_MINOR", "CLIENT_VERSION_BUILD", "CLIENT_VERSION_RC")
        )
        with tempfile.TemporaryDirectory(prefix="roots-build-identity-") as directory:
            source = Path(directory)
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.22)\n"
                "project(IdentityTest LANGUAGES NONE)\n"
                + metadata + "\n" + version_block
                + f'configure_file("{(ROOT / "cmake/bitcoin-build-config.h.in").as_posix()}" '
                '"${CMAKE_BINARY_DIR}/identity.h" @ONLY)\n', encoding="utf-8",
            )
            command = [args.cmake, "-S", str(source), "-B", str(source / "build")]
            if tag is not None:
                command.append(f"-DCLIENT_VERSION_TAG={tag}")
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            header = source / "build/identity.h"
            return result, header.read_text(encoding="utf-8") if header.exists() else ""

    def test_release_version_without_git(self):
        for tag in ("v32.1-roots.1", "v32.1-roots.6"):
            with self.subTest(tag=tag):
                result, header = self.configure(tag)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f'#define CLIENT_VERSION_STRING "{tag[1:]}"', header)
                self.assertIn("#define CLIENT_VERSION_MAJOR 32", header)
                self.assertIn("#define CLIENT_VERSION_MINOR 1", header)
                self.assertIn('#define CLIENT_NAME "Bitcoin Roots"', header)

    def test_wrong_base_and_invalid_release(self):
        for tag in ("v31.1-roots.6", "v32x1-roots.1", "v32.1-roots.0", "v32.1-knots.1", ""):
            with self.subTest(tag=tag):
                result, _ = self.configure(tag)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("CLIENT_VERSION_TAG must be a Roots release tag", result.stderr)

    def test_default_version(self):
        result, header = self.configure()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('#define CLIENT_VERSION_STRING "32.1-roots.1"', header)


if __name__ == "__main__":
    unittest.main(argv=[__file__] + unittest_args)
