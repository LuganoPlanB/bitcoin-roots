#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import hashlib
import io
import os
import shutil
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
import zipfile

import test_release_notices as notice_fixtures


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "ci/release/prepare-release.sh"
ARCHIVE_TOOL = ROOT / "ci/release/archive.py"
PUBLIC_KEY = ROOT / "contrib/release/bitcoin-roots-release-key.asc"


class PrepareReleaseTest(unittest.TestCase):
    def write_package(self, path, root, content, copying=None, notices=True):
        member = f"{root}/bin/bitcoind"
        copying = (ROOT / "COPYING").read_bytes() if copying is None else copying
        notice_members = {}
        platform = path.name.removeprefix("bitcoin-roots-").removesuffix(".tar.gz").removesuffix(".zip")
        if notices and platform in notice_fixtures.NOTICES.PLATFORMS:
            with tempfile.TemporaryDirectory() as temporary_dir:
                work = Path(temporary_dir)
                descriptor = notice_fixtures.ReleaseNoticesTest().descriptor(work, platform)
                notice_fixtures.NOTICES.collect(platform, descriptor, work / "collected")
                notice_members = {f"{root}/notices/{file.relative_to(work / 'collected').as_posix()}": file.read_bytes()
                                  for file in (work / "collected").rglob("*") if file.is_file()}
        if path.name.endswith(".tar.gz"):
            with tarfile.open(path, mode="w:gz") as package:
                info = tarfile.TarInfo(member)
                info.size = len(content)
                package.addfile(info, io.BytesIO(content))
                if copying:
                    info = tarfile.TarInfo(f"{root}/COPYING")
                    info.size = len(copying)
                    package.addfile(info, io.BytesIO(copying))
                for name, data in notice_members.items():
                    info = tarfile.TarInfo(name)
                    info.size = len(data)
                    package.addfile(info, io.BytesIO(data))
        else:
            with zipfile.ZipFile(path, mode="w") as package:
                package.writestr(member, content)
                if copying:
                    package.writestr(f"{root}/COPYING", copying)
                for name, data in notice_members.items():
                    package.writestr(name, data)

    def test_final_gate_rejects_missing_faulty_or_wrong_platform_notices(self):
        for fault in ("missing", "faulty-index", "wrong-platform"):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as temporary_dir:
                downloads = Path(temporary_dir) / "downloads"
                downloads.mkdir()
                archive = downloads / "bitcoin-roots-darwin-arm64.zip"
                root = "bitcoin-roots-30.3-roots.1"
                self.write_package(archive, root, b"binary", notices=False)
                if fault != "missing":
                    work = Path(temporary_dir)
                    platform = "darwin-x86_64" if fault == "wrong-platform" else "darwin-arm64"
                    descriptor = notice_fixtures.ReleaseNoticesTest().descriptor(work, platform)
                    notice_fixtures.NOTICES.collect(platform, descriptor, work / "collected")
                    with zipfile.ZipFile(archive, "a") as package:
                        for file in (work / "collected").rglob("*"):
                            if file.is_file():
                                content = b'{}' if fault == "faulty-index" and file.name == "index.json" else file.read_bytes()
                                package.writestr(f"{root}/notices/{file.relative_to(work / 'collected').as_posix()}", content)
                (downloads / "series.patch").write_text("patch\n")
                output = Path(temporary_dir) / "output"
                result = subprocess.run([SCRIPT, downloads, output, PUBLIC_KEY, "v30.3-roots.1", "1"],
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((output / "SHA512SUMS").exists())
                self.assertFalse((output / archive.name).exists())

    def test_explicit_assembly_rejects_intermediate_package(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            downloads = work / "downloaded-artifacts"
            downloads.mkdir()
            self.write_package(downloads / "bitcoin-roots-linux-x86_64.tar.gz",
                               "bitcoin-roots-30.3-roots.1", b"binary", notices=False)
            (downloads / "series.patch").write_text("patch\n")
            result = subprocess.run([SCRIPT, downloads, work / "release-assets", PUBLIC_KEY,
                                     "v30.3-roots.1", "1"], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((work / "release-assets/SHA512SUMS").exists())

    def test_stages_copying_for_every_platform_archive(self):
        workflow = (ROOT / ".github/workflows/release.yml").read_text()
        # Both Linux and both macOS matrix jobs share their platform's block.
        self.assertEqual(workflow.count("ci/release/archive.py stage-copying"), 3)
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            for platform in ("linux-x86_64", "linux-aarch64", "darwin-x86_64", "darwin-arm64", "windows-x86_64"):
                with self.subTest(platform=platform):
                    root = work / platform / "bitcoin-roots-30.3-roots.1"
                    root.mkdir(parents=True)
                    (root / "payload").write_bytes(b"executable fixture")
                    subprocess.run(["python3", ARCHIVE_TOOL, "stage-copying", root], check=True)
                    self.assertEqual((root / "COPYING").read_bytes(), (ROOT / "COPYING").read_bytes())
                    archive = root.parent / ("package.tar.gz" if platform.startswith("linux") else "package.zip")
                    if platform.startswith("linux"):
                        with tarfile.open(archive, mode="w:gz") as package:
                            package.add(root, arcname=root.name)
                    else:
                        with zipfile.ZipFile(archive, mode="w") as package:
                            for path in root.iterdir():
                                package.write(path, f"{root.name}/{path.name}")
                    subprocess.run(["python3", ARCHIVE_TOOL, "validate", archive, root.name,
                                    "--copying", ROOT / "COPYING"], check=True)

    def test_actual_unix_workflow_stages_notices_and_prepares(self):
        lines = (ROOT / ".github/workflows/release.yml").read_text().splitlines()
        def packaging_block(name):
            start = lines.index(f"      - name: {name}")
            start = lines.index("        run: |", start) + 1
            end = start
            while end < len(lines) and (lines[end].startswith("          ") or not lines[end]):
                end += 1
            return "\n".join(line[10:] for line in lines[start:end])

        with tempfile.TemporaryDirectory() as temporary_dir:
            for platform in ("linux-x86_64", "linux-aarch64", "darwin-x86_64", "darwin-arm64"):
                with self.subTest(platform=platform):
                    work = Path(temporary_dir) / platform
                    (work / "runner").mkdir(parents=True)
                    out = work / "out"
                    out.mkdir()
                    environment = {**os.environ, "RUNNER_TEMP": str(work / "runner"),
                                   "BASE_OUTDIR": str(out), "BASE_BUILD_DIR": str(work / "runner"), "ARTIFACT_NAME": f"bitcoin-roots-{platform}",
                                   "RELEASE_TAG": "v30.3-roots.1", "GITHUB_WORKSPACE": str(work)}
                    if platform.startswith("linux"):
                        (out / "payload").write_bytes(b"binary fixture")
                        block = packaging_block("Package release artifacts")
                    else:
                        deploy = work / "ci/scratch/build-fixture"
                        deploy.mkdir(parents=True)
                        with zipfile.ZipFile(deploy / "Bitcoin-Roots.zip", mode="w") as package:
                            package.writestr("Bitcoin-Qt.app/Contents/MacOS/Bitcoin-Qt", b"binary fixture")
                        block = packaging_block("Stage release package")
                    descriptor = notice_fixtures.ReleaseNoticesTest().descriptor(work, platform)
                    notice_fixtures.NOTICES.collect(platform, descriptor, work / "runner/release-notices/notices")
                    subprocess.run(["bash", "-c", block], cwd=ROOT, env=environment, check=True)
                    archive = next((work / "runner/release-packages").iterdir())
                    member = "bitcoin-roots-30.3-roots.1/COPYING"
                    if platform.startswith("linux"):
                        with tarfile.open(archive) as package:
                            notice = package.extractfile(member).read()
                            self.assertEqual(package.extractfile("bitcoin-roots-30.3-roots.1/payload").read(), b"binary fixture")
                    else:
                        with zipfile.ZipFile(archive) as package:
                            notice = package.read(member)
                            self.assertEqual(package.read("bitcoin-roots-30.3-roots.1/Bitcoin-Qt.app/Contents/MacOS/Bitcoin-Qt"), b"binary fixture")
                    self.assertEqual(notice, (ROOT / "COPYING").read_bytes())
                    subprocess.run(["python3", ROOT / "ci/release/notices.py", "validate", archive,
                                    "bitcoin-roots-30.3-roots.1", platform], check=True)
                    downloads = work / "assembled-downloads"
                    downloads.mkdir()
                    assembled = downloads / archive.name
                    shutil.copyfile(archive, assembled)
                    (downloads / "series.patch").write_text("patch\n")
                    subprocess.run([SCRIPT, downloads, work / "release-assets", PUBLIC_KEY,
                                    "v30.3-roots.1", "1"], check=True)
                    self.assertTrue((work / "release-assets/SHA512SUMS").exists())

    def test_release_preparation_rejects_missing_or_mismatched_copying(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            for suffix in ("tar.gz", "zip"):
                for content in (b"", b"wrong notice"):
                    with self.subTest(suffix=suffix, copying=content):
                        downloads = work / f"{suffix}-{len(content)}"
                        downloads.mkdir()
                        self.write_package(downloads / f"package.{suffix}", "bitcoin-roots-30.3-roots.1", b"payload", copying=content)
                        (downloads / "series.patch").write_text("patch\n")
                        result = subprocess.run([SCRIPT, downloads, downloads / "output", PUBLIC_KEY,
                                                 "v30.3-roots.1", "1"], capture_output=True, text=True)
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn("COPYING", result.stderr)

    def test_copying_cannot_be_a_symlink_or_directory(self):
        root = "bitcoin-roots-30.3-roots.1"
        with tempfile.TemporaryDirectory() as temporary_dir:
            for kind in ("symlink", "directory", "directory_without_slash"):
                with self.subTest(kind=kind):
                    archive = Path(temporary_dir) / f"{kind}.zip"
                    with zipfile.ZipFile(archive, mode="w") as package:
                        package.writestr(f"{root}/notice", (ROOT / "COPYING").read_bytes())
                        info = zipfile.ZipInfo(f"{root}/COPYING" + ("/" if kind == "directory" else ""))
                        info.create_system = 3
                        info.external_attr = (0o120777 if kind == "symlink" else 0o40755) << 16
                        package.writestr(info, "notice" if kind == "symlink" else (ROOT / "COPYING").read_bytes())
                    result = subprocess.run(["python3", ARCHIVE_TOOL, "validate", archive, root,
                                             "--copying", ROOT / "COPYING"], capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("COPYING", result.stderr)

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

    def validate_zip_links(self, links, files):
        root = "bitcoin-roots-30.3-roots.1"
        with tempfile.TemporaryDirectory() as temporary_dir:
            archive = Path(temporary_dir) / "framework.zip"
            with zipfile.ZipFile(archive, mode="w") as package:
                for name in files:
                    info = zipfile.ZipInfo(f"{root}/{name}")
                    info.create_system = 3
                    info.external_attr = (0o40755 << 16) | 0x10 if name.endswith("/") else 0o100755 << 16
                    package.writestr(info, b"" if name.endswith("/") else b"payload")
                for name, target in links.items():
                    info = zipfile.ZipInfo(f"{root}/{name}")
                    info.create_system = 3
                    info.external_attr = 0o120777 << 16
                    package.writestr(info, target)
            return subprocess.run(
                ["python3", ARCHIVE_TOOL, "validate", archive, root],
                capture_output=True, text=True,
            )

    def test_accepts_internal_macos_framework_symlink_chains(self):
        framework = "Bitcoin-Qt.app/Contents/Frameworks/QtGui.framework"
        result = self.validate_zip_links(
            {f"{framework}/Versions/Current": "A",
             f"{framework}/Resources": "Versions/Current/Resources",
             f"{framework}/QtGui": "Versions/Current/QtGui"},
            ["Bitcoin-Qt.app/", "Bitcoin-Qt.app/Contents/", "Bitcoin-Qt.app/Contents/Frameworks/",
             f"{framework}/", f"{framework}/Versions/", f"{framework}/Versions/A/",
             f"{framework}/Versions/A/Resources/", f"{framework}/Versions/A/Resources/Info.plist",
             f"{framework}/Versions/A/QtGui"],
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_accepts_repeated_noncyclic_symlink_traversal(self):
        result = self.validate_zip_links(
            {"alias": "dir", "link": "alias/../alias/file"}, ["dir/", "dir/file"],
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_unsafe_dangling_and_cyclic_zip_symlinks(self):
        cases = [
            {"link": "/tmp/outside"},
            {"link": "../outside"},
            {"link": "C:/outside"},
            {"link": "..\\outside"},
            {"link": "outside\0"},
            {"link": ""},
            {"link": b"\xff"},
            {"link": "x" * 4097},
            {"link": "missing"},
            {"link": "other", "other": "link"},
            # Resolve alias before ..; lexical normalization alone misses this escape.
            {"alias": ".", "link": "alias/../outside"},
        ]
        for links in cases:
            with self.subTest(links=links):
                result = self.validate_zip_links(links, ["bin/bitcoind", "outside"])
                self.assertNotEqual(result.returncode, 0)

    def test_symlinks_alone_do_not_count_as_release_payload(self):
        result = self.validate_zip_links({"link": "."}, [])
        self.assertNotEqual(result.returncode, 0)

    def test_rejects_duplicate_and_special_zip_members(self):
        root = "bitcoin-roots-30.3-roots.1"
        with tempfile.TemporaryDirectory() as temporary_dir:
            for kind in ("duplicate", "fifo", "device"):
                with self.subTest(kind=kind):
                    archive = Path(temporary_dir) / f"{kind}.zip"
                    with zipfile.ZipFile(archive, mode="w") as package:
                        package.writestr(f"{root}/bin/bitcoind", b"payload")
                        info = zipfile.ZipInfo(f"{root}/bin/bitcoind" if kind == "duplicate" else f"{root}/special")
                        info.create_system = 3
                        mode = {"duplicate": 0o100755, "fifo": 0o10755, "device": 0o20755}[kind]
                        info.external_attr = mode << 16
                        package.writestr(info, b"payload")
                    result = subprocess.run(
                        ["python3", ARCHIVE_TOOL, "validate", archive, root],
                        capture_output=True, text=True,
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("Duplicate archive member" if kind == "duplicate" else "Unsupported archive member", result.stderr)


if __name__ == "__main__":
    unittest.main()
