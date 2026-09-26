#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import hashlib
import io
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "ci/release/prepare-release.sh"
ARCHIVE_TOOL = ROOT / "ci/release/archive.py"
PUBLIC_KEY = ROOT / "contrib/release/bitcoin-roots-release-key.asc"


class PrepareReleaseTest(unittest.TestCase):
    def write_package(self, path, root, content):
        member = f"{root}/bin/bitcoind"
        if path.name.endswith(".tar.gz"):
            with tarfile.open(path, mode="w:gz") as package:
                info = tarfile.TarInfo(member)
                info.size = len(content)
                package.addfile(info, io.BytesIO(content))
        else:
            with zipfile.ZipFile(path, mode="w") as package:
                package.writestr(member, content)

    def test_prepares_sorted_manifest(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            downloads = work / "downloads"
            output = work / "output"
            (downloads / "linux").mkdir(parents=True)
            (downloads / "darwin").mkdir()
            packages = {
                downloads / "linux/bitcoin-roots-linux-x86_64.tar.gz": b"linux package",
                downloads / "darwin/bitcoin-roots-darwin-arm64.zip": b"macOS package",
            }
            archive_root = "bitcoin-roots-29.4-roots.1"
            for path, content in packages.items():
                self.write_package(path, archive_root, content)
            patch = downloads / "bitcoin-roots-29.4-roots.1.patch"
            patch.write_text("From patch-series\n", encoding="utf-8")

            subprocess.run([SCRIPT, downloads, output, PUBLIC_KEY, "v29.4-roots.1", "2"], check=True)

            manifest = (output / "SHA512SUMS").read_text(encoding="utf-8")
            self.assertIn("# Bitcoin Roots release: v29.4-roots.1\n", manifest)
            checksum_lines = [line for line in manifest.splitlines() if not line.startswith("#")]
            expected = [
                f"{hashlib.sha512(path.read_bytes()).hexdigest()}  {path.name}"
                for path in sorted([*packages, patch])
            ]
            self.assertEqual(checksum_lines, expected)
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
                [SCRIPT, downloads, output, PUBLIC_KEY, "v29.4-roots.1", "2"],
                capture_output=True, text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Duplicate release package name: package.zip", result.stderr)

    def test_requires_exactly_one_nonempty_patch_series(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            downloads = work / "downloads"
            output = work / "output"
            downloads.mkdir()
            archive_root = "bitcoin-roots-29.4-roots.1"
            self.write_package(downloads / "package.tar.gz", archive_root, b"package")

            missing = subprocess.run(
                [SCRIPT, downloads, output, PUBLIC_KEY, "v29.4-roots.1", "1"],
                capture_output=True, text=True,
            )
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn("Expected one release patch series, found 0", missing.stderr)

            (downloads / "one.patch").write_text("one\n", encoding="utf-8")
            (downloads / "two.patch").write_text("two\n", encoding="utf-8")
            duplicate = subprocess.run(
                [SCRIPT, downloads, output, PUBLIC_KEY, "v29.4-roots.1", "1"],
                capture_output=True, text=True,
            )
            self.assertNotEqual(duplicate.returncode, 0)
            self.assertIn("Expected one release patch series, found 2", duplicate.stderr)

            (downloads / "two.patch").unlink()
            (downloads / "one.patch").write_bytes(b"")
            empty = subprocess.run(
                [SCRIPT, downloads, output, PUBLIC_KEY, "v29.4-roots.1", "1"],
                capture_output=True, text=True,
            )
            self.assertNotEqual(empty.returncode, 0)
            self.assertIn("Release patch series is empty", empty.stderr)

    def test_archive_root_uses_roots_tag_or_commit(self):
        cases = [
            ({"RELEASE_TAG": "v29.4-roots.1"}, "bitcoin-roots-29.4-roots.1"),
            ({"RELEASE_TAG": "v29.4rc1-roots.1"}, "bitcoin-roots-29.4rc1-roots.1"),
            ({"GITHUB_SHA": "0123456789abcdef"}, "bitcoin-roots-git-0123456789ab"),
        ]
        for environment, expected in cases:
            with self.subTest(environment=environment):
                result = subprocess.run(
                    ["python3", ARCHIVE_TOOL, "root-name"],
                    env={"RELEASE_TAG": "", "GITHUB_SHA": "", **environment},
                    capture_output=True, text=True, check=True,
                )
                self.assertEqual(result.stdout.strip(), expected)

    def test_rejects_unsafe_archive_members(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            archive = work / "unsafe.tar.gz"
            with tarfile.open(archive, mode="w:gz") as package:
                for name, kind in [
                    ("bitcoin-roots-29.4-roots.1/../escape", tarfile.REGTYPE),
                    ("bitcoin-roots-29.4-roots.1/link", tarfile.SYMTYPE),
                    ("bitcoin-roots-29.4-roots.1/hard-link", tarfile.LNKTYPE),
                    ("bitcoin-roots-29.4-roots.1/fifo", tarfile.FIFOTYPE),
                ]:
                    info = tarfile.TarInfo(name)
                    info.type = kind
                    if kind in {tarfile.SYMTYPE, tarfile.LNKTYPE}:
                        info.linkname = "outside"
                    else:
                        info.size = 1
                    package.addfile(info, io.BytesIO(b"x") if kind == tarfile.REGTYPE else None)
            result = subprocess.run(
                ["python3", ARCHIVE_TOOL, "validate", archive, "bitcoin-roots-29.4-roots.1"],
                capture_output=True, text=True,
            )
            self.assertNotEqual(result.returncode, 0)

            zip_archive = work / "symlink.zip"
            with zipfile.ZipFile(zip_archive, mode="w") as package:
                info = zipfile.ZipInfo("bitcoin-roots-29.4-roots.1/link")
                info.external_attr = 0o120777 << 16
                package.writestr(info, "outside")
            result = subprocess.run(
                ["python3", ARCHIVE_TOOL, "validate", zip_archive, "bitcoin-roots-29.4-roots.1"],
                capture_output=True, text=True,
            )
            self.assertNotEqual(result.returncode, 0)

    def test_rejects_wrong_root_empty_payload_and_backslashes(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            cases = {
                "wrong.zip": ["bitcoin-roots-other/bin/bitcoind"],
                "empty.zip": ["bitcoin-roots-29.4-roots.1/"],
                "nested-directory.zip": ["bitcoin-roots-29.4-roots.1/bin/"],
                "backslash.zip": ["bitcoin-roots-29.4-roots.1\\bin\\bitcoind"],
            }
            for name, members in cases.items():
                archive = work / name
                with zipfile.ZipFile(archive, mode="w") as package:
                    for member in members:
                        package.writestr(member, b"x")
                with self.subTest(archive=name):
                    result = subprocess.run(
                        ["python3", ARCHIVE_TOOL, "validate", archive, "bitcoin-roots-29.4-roots.1"],
                        capture_output=True, text=True,
                    )
                    self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
