#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "ci/release/prepare-release.sh"
PUBLIC_KEY = ROOT / "contrib/release/bitcoin-roots-release-key.asc"


class PrepareReleaseTest(unittest.TestCase):
    def test_prepares_sorted_manifest_and_ignores_old_checksums(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            downloads = work / "downloads"
            output = work / "output"
            (downloads / "linux").mkdir(parents=True)
            (downloads / "darwin-x86_64").mkdir()
            (downloads / "darwin-arm64").mkdir()
            packages = {
                downloads / "linux/bitcoin-roots-linux-x86_64.tar.gz": b"linux package",
                downloads / "darwin-x86_64/bitcoin-roots-darwin-x86_64.zip": b"macOS x86_64 package",
                downloads / "darwin-arm64/bitcoin-roots-darwin-arm64.zip": b"macOS arm64 package",
            }
            for path, content in packages.items():
                path.write_bytes(content)
            (downloads / "linux/bitcoin-roots-linux-x86_64.tar.gz.sha256").write_text("ignored\n")
            (downloads / "darwin-x86_64/SHA256SUMS").write_text("ignored\n")

            subprocess.run(
                [SCRIPT, downloads, output, PUBLIC_KEY, "v29.3.0-roots.1", "3"],
                check=True,
            )

            manifest = (output / "SHA512SUMS").read_text()
            self.assertIn("# Bitcoin Roots release: v29.3.0-roots.1\n", manifest)
            for line in PUBLIC_KEY.read_text().splitlines():
                self.assertIn(f"# {line}\n", manifest)
            checksum_lines = [line for line in manifest.splitlines() if not line.startswith("#")]
            expected = [
                f"{hashlib.sha512(b'macOS arm64 package').hexdigest()}  bitcoin-roots-darwin-arm64.zip",
                f"{hashlib.sha512(b'macOS x86_64 package').hexdigest()}  bitcoin-roots-darwin-x86_64.zip",
                f"{hashlib.sha512(b'linux package').hexdigest()}  bitcoin-roots-linux-x86_64.tar.gz",
            ]
            self.assertEqual(checksum_lines, expected)
            self.assertEqual(
                sorted(path.name for path in output.iterdir()),
                [
                    "SHA512SUMS",
                    "bitcoin-roots-darwin-arm64.zip",
                    "bitcoin-roots-darwin-x86_64.zip",
                    "bitcoin-roots-linux-x86_64.tar.gz",
                ],
            )
            subprocess.run(["sha512sum", "--check", "SHA512SUMS"], cwd=output, check=True)

    def test_rejects_duplicate_package_names(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            downloads = work / "downloads"
            output = work / "output"
            (downloads / "one").mkdir(parents=True)
            (downloads / "two").mkdir()
            (downloads / "one/package.zip").write_bytes(b"one")
            (downloads / "two/package.zip").write_bytes(b"two")

            result = subprocess.run(
                [SCRIPT, downloads, output, PUBLIC_KEY, "test", "2"],
                capture_output=True,
                text=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Duplicate release package name: package.zip", result.stderr)


if __name__ == "__main__":
    unittest.main()
