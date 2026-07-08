#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import hashlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "ci/release/prepare-release.sh"
ARCHIVE_TOOL = ROOT / "ci/release/archive.py"
PUBLIC_KEY = ROOT / "contrib/release/bitcoin-roots-release-key.asc"
RELEASE_WORKFLOW = ROOT / ".github/workflows/release.yml"


class PrepareReleaseTest(unittest.TestCase):
    def write_package(self, path, root, content):
        member = f"{root}/bin/bitcoin-qt"
        if path.name.endswith(".tar.gz"):
            with tarfile.open(path, mode="w:gz") as package:
                info = tarfile.TarInfo(member)
                info.size = len(content)
                package.addfile(info, io.BytesIO(content))
        else:
            with zipfile.ZipFile(path, mode="w") as package:
                package.writestr(member, content)

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
            archive_root = "bitcoin-roots-29.3.0-roots.1"
            for path, content in packages.items():
                self.write_package(path, archive_root, content)
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
            archive_hashes = {
                path.name: hashlib.sha512(path.read_bytes()).hexdigest() for path in packages
            }
            expected = [
                f"{archive_hashes['bitcoin-roots-darwin-arm64.zip']}  bitcoin-roots-darwin-arm64.zip",
                f"{archive_hashes['bitcoin-roots-darwin-x86_64.zip']}  bitcoin-roots-darwin-x86_64.zip",
                f"{archive_hashes['bitcoin-roots-linux-x86_64.tar.gz']}  bitcoin-roots-linux-x86_64.tar.gz",
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

    def test_archive_root_uses_release_version_or_commit(self):
        cases = [
            ({"RELEASE_TAG": "v29.3-roots.1"}, "bitcoin-roots-29.3-roots.1"),
            ({"RELEASE_TAG": "v30.0+roots.1"}, "bitcoin-roots-30.0+roots.1"),
            ({"GITHUB_SHA": "0123456789abcdef"}, "bitcoin-roots-git-0123456789ab"),
        ]
        for environment, expected in cases:
            with self.subTest(environment=environment):
                result = subprocess.run(
                    [sys.executable, ARCHIVE_TOOL, "root-name"],
                    env={**os.environ, "RELEASE_TAG": "", "GITHUB_SHA": "", **environment},
                    capture_output=True,
                    text=True,
                    check=True,
                )
                self.assertEqual(result.stdout.strip(), expected)

    def test_rejects_archive_without_versioned_root(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            downloads = work / "downloads"
            output = work / "output"
            downloads.mkdir()
            package = downloads / "bitcoin-roots-windows-x86_64.zip"
            self.write_package(package, "bitcoin-roots-wrong-version", b"package")

            result = subprocess.run(
                [SCRIPT, downloads, output, PUBLIC_KEY, "v29.3-roots.1", "1"],
                capture_output=True,
                text=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Expected root directory bitcoin-roots-29.3-roots.1", result.stderr)

    def test_release_workflow_wraps_every_platform_archive(self):
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        self.assertEqual(workflow.count("ci/release/archive.py root-name"), 3)
        self.assertEqual(workflow.count("ci/release/archive.py validate"), 3)
        self.assertIn('--transform "flags=r;s|^\\.|${archive_root}|"', workflow)
        self.assertIn('unzip -q "${packages[0]}" -d "${staging_parent}/${archive_root}"', workflow)
        self.assertIn('zip -qry "${archive}" "${archive_root}"', workflow)


if __name__ == "__main__":
    unittest.main()
