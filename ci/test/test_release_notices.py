#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
NOTICE_TOOL = ROOT / "ci/release/notices.py"
REPACK_TOOL = ROOT / "ci/release/repack.py"
SPEC = importlib.util.spec_from_file_location("release_notices", NOTICE_TOOL)
NOTICES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(NOTICES)


class ReleaseNoticesTest(unittest.TestCase):
    def descriptor(self, work, platform):
        # These are executable provider-interface fixtures, not real release inputs.
        receipt = work / "build-receipt.json"
        receipt.write_text('{"version": "fixture-1", "provider": "test"}\n')
        license_file = work / "LICENSE"
        license_file.write_bytes(b"exact fixture dependency license\n")
        entries = {component: {"version": "fixture-1", "source": "fixture:build-receipt.json",
                               "receipt": receipt.name, "files": [license_file.name]}
                   for component in NOTICES.required_components(platform)}
        path = work / "descriptor.json"
        path.write_text(json.dumps(entries))
        return path

    def collect(self, work, platform):
        descriptor = self.descriptor(work, platform)
        destination = work / "collected"
        subprocess.run([sys.executable, NOTICE_TOOL, "collect", platform, descriptor, destination], check=True)
        return destination / "index.json"

    def test_collect_preserves_tracked_and_dependency_bytes(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            index_path = self.collect(work, "linux-x86_64")
            index = json.loads(index_path.read_text())
            for component, source in NOTICES.TRACKED_NOTICES.items():
                self.assertEqual((index_path.parent / f"tracked/{component}.txt").read_bytes(), (ROOT / source).read_bytes())
            for entry in index["components"].values():
                self.assertEqual((index_path.parent / entry["files"][0]).read_bytes(), (work / "LICENSE").read_bytes())
                self.assertEqual((index_path.parent / entry["evidence"]).read_bytes(), (work / "build-receipt.json").read_bytes())

    def test_missing_receipt_component_or_notice_rejected_before_output(self):
        for problem in ("component", "receipt", "notice"):
            with self.subTest(problem=problem), tempfile.TemporaryDirectory() as temporary_dir:
                work = Path(temporary_dir)
                descriptor = self.descriptor(work, "darwin-arm64")
                metadata = json.loads(descriptor.read_text())
                if problem == "component":
                    del metadata["openssl"]
                elif problem == "receipt":
                    metadata["qt"]["receipt"] = "absent"
                else:
                    metadata["qt"]["files"] = []
                descriptor.write_text(json.dumps(metadata))
                result = subprocess.run([sys.executable, NOTICE_TOOL, "collect", "darwin-arm64", descriptor, work / "collected"], capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((work / "collected").exists())

    def test_independent_source_provenance_does_not_require_runner_receipt(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            descriptor = self.descriptor(work, "linux-x86_64")
            metadata = json.loads(descriptor.read_text())
            for entry in metadata.values():
                entry["evidence"] = entry.pop("receipt")
                entry["source_sha256"] = "a" * 64
                entry["provenance_type"] = "pinned-source-archive"
            descriptor.write_text(json.dumps(metadata))
            NOTICES.collect("linux-x86_64", descriptor, work / "collected")
            index = json.loads((work / "collected/index.json").read_text())
            self.assertEqual(index["components"]["qt"]["source_sha256"], "a" * 64)
            self.assertNotIn("receipt", index["components"]["qt"])

    def test_unknown_version_requires_explicit_stability_provenance(self):
        for stable in (False, True):
            with self.subTest(stable=stable), tempfile.TemporaryDirectory() as temporary_dir:
                work = Path(temporary_dir)
                descriptor = self.descriptor(work, "linux-x86_64")
                metadata = json.loads(descriptor.read_text())
                metadata["qt"]["version"] = "unknown"
                if stable:
                    metadata["qt"]["provenance_type"] = "source-version-stability"
                    metadata["qt"]["notice_stable_versions"] = ["fixture-1", "fixture-2"]
                descriptor.write_text(json.dumps(metadata))
                if stable:
                    NOTICES.collect("linux-x86_64", descriptor, work / "collected")
                else:
                    with self.assertRaisesRegex(ValueError, "Unknown version"):
                        NOTICES.collect("linux-x86_64", descriptor, work / "collected")

    def test_staging_rejects_altered_notice_and_symlink_escape(self):
        for problem in ("altered", "escape"):
            with self.subTest(problem=problem), tempfile.TemporaryDirectory() as temporary_dir:
                work = Path(temporary_dir)
                index_path = self.collect(work, "linux-aarch64")
                index = json.loads(index_path.read_text())
                path = index_path.parent / "tracked/leveldb.txt"
                if problem == "altered":
                    path.write_bytes(b"different")
                else:
                    outside = work / "outside"
                    path.parent.rename(outside)
                    path.parent.symlink_to(outside, target_is_directory=True)
                root = work / "package"
                root.mkdir()
                with self.assertRaises(ValueError):
                    NOTICES.stage(root, index_path, "linux-aarch64")
                self.assertFalse((root / "notices").exists())
                self.assertIn("tracked/leveldb.txt", index["files"])

    def test_unsafe_notice_paths_rejected(self):
        for path in ("", "/absolute", "a//b", "a/../b", "./a", "a\\b", "nul\x00"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                NOTICES.safe_path(path)

    def test_archive_notice_tampering_rejected(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            inputs = self.collect(work, "windows-x86_64")
            root = "bitcoin-roots-30.3-roots.1"
            archive = work / "tampered.zip"
            with zipfile.ZipFile(archive, "w") as package:
                for path in inputs.parent.rglob("*"):
                    if path.is_file():
                        content = b"altered" if path.name == "leveldb.txt" else path.read_bytes()
                        package.writestr(f"{root}/notices/{path.relative_to(inputs.parent).as_posix()}", content)
            with self.assertRaisesRegex(ValueError, "Notice bytes differ"):
                NOTICES.validate_archive(archive, root, "windows-x86_64")

    def test_wrong_platform_bundle_rejected(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            inputs = self.collect(work, "darwin-arm64")
            root = work / "package"
            root.mkdir()
            with self.assertRaisesRegex(ValueError, "platform mismatch"):
                NOTICES.stage(root, inputs, "darwin-x86_64")
            self.assertFalse((root / "notices").exists())

    def test_repack_refuses_dangling_destination_symlink(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            inputs = self.collect(work, "windows-x86_64")
            source = work / "original.zip"
            with zipfile.ZipFile(source, "w") as package:
                package.writestr("bitcoin-roots-30.3-roots.1/bin/bitcoind", b"payload")
            destination = work / "repacked.zip"
            target = work / "must-not-be-created.zip"
            destination.symlink_to(target)
            result = subprocess.run([sys.executable, REPACK_TOOL, source, destination,
                                     "bitcoin-roots-30.3-roots.1", inputs, "windows-x86_64"], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(destination.is_symlink())
            self.assertFalse(target.exists())

    def test_repack_rejects_changed_or_nonregular_existing_copying(self):
        for kind in ("changed", "symlink", "directory"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary_dir:
                work = Path(temporary_dir)
                inputs = self.collect(work, "windows-x86_64")
                source = work / "original.zip"
                root = "bitcoin-roots-30.3-roots.1"
                with zipfile.ZipFile(source, "w") as package:
                    package.writestr(f"{root}/bin/bitcoind", b"binary")
                    info = zipfile.ZipInfo(f"{root}/COPYING" + ("/" if kind == "directory" else ""))
                    info.create_system = 3
                    info.external_attr = (0o120777 if kind == "symlink" else 0o40755 if kind == "directory" else 0o100644) << 16
                    package.writestr(info, "bin/bitcoind" if kind == "symlink" else b"wrong COPYING")
                destination = work / "assembled.zip"
                result = subprocess.run([sys.executable, REPACK_TOOL, source, destination, root,
                                         inputs, "windows-x86_64"], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("COPYING", result.stderr)
                self.assertFalse(destination.exists())

    def test_existing_canonical_copying_is_preserved_but_notices_are_not_replaced(self):
        for existing_notices in (False, True):
            with self.subTest(existing_notices=existing_notices), tempfile.TemporaryDirectory() as temporary_dir:
                work = Path(temporary_dir)
                inputs = self.collect(work, "windows-x86_64")
                source = work / "original.zip"
                root = "bitcoin-roots-30.3-roots.1"
                with zipfile.ZipFile(source, "w") as package:
                    package.writestr(f"{root}/bin/bitcoind", b"binary")
                    copying = zipfile.ZipInfo(f"{root}/COPYING", (2019, 1, 2, 3, 4, 6))
                    copying.create_system = 3
                    copying.external_attr = 0o100640 << 16
                    package.writestr(copying, (ROOT / "COPYING").read_bytes())
                    if existing_notices:
                        package.writestr(f"{root}/notices/previous.txt", b"existing notice")
                destination = work / "assembled.zip"
                result = subprocess.run([sys.executable, REPACK_TOOL, source, destination, root,
                                         inputs, "windows-x86_64"], capture_output=True, text=True)
                if existing_notices:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("already contains notices", result.stderr)
                    self.assertFalse(destination.exists())
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(json.loads(result.stdout)["unchanged_members"], 2)
                    with zipfile.ZipFile(destination) as package:
                        self.assertEqual(package.read(copying.filename), (ROOT / "COPYING").read_bytes())
                        self.assertEqual(package.getinfo(copying.filename).external_attr, copying.external_attr)
                        self.assertEqual(package.getinfo(copying.filename).date_time, copying.date_time)

    def test_repack_all_platforms_preserves_binary_modes_and_app_links(self):
        for platform in sorted(NOTICES.PLATFORMS):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as temporary_dir:
                work = Path(temporary_dir)
                inputs = self.collect(work, platform)
                root = "bitcoin-roots-30.3-roots.1"
                source = work / ("original.tar.gz" if platform.startswith("linux") else "original.zip")
                destination = work / ("repacked.tar.gz" if platform.startswith("linux") else "repacked.zip")
                payload = b"binary bytes including signature fixture\x00\xff"
                member_name = f"{root}/Bitcoin-Qt.app/Contents/MacOS/Bitcoin-Qt" if platform.startswith("darwin") else f"{root}/bin/bitcoind"
                if platform.startswith("linux"):
                    with tarfile.open(source, "w:gz") as package:
                        info = tarfile.TarInfo(member_name)
                        info.mode = 0o755
                        info.mtime = 123456
                        info.size = len(payload)
                        package.addfile(info, io.BytesIO(payload))
                else:
                    with zipfile.ZipFile(source, "w") as package:
                        info = zipfile.ZipInfo(member_name, (2020, 1, 2, 3, 4, 6))
                        info.create_system = 3
                        info.external_attr = 0o100755 << 16
                        package.writestr(info, payload)
                        if platform.startswith("windows"):
                            # PowerShell archives may carry no Unix mode at all.
                            package.filelist[-1].external_attr = 0
                        if platform.startswith("darwin"):
                            link = zipfile.ZipInfo(f"{root}/Bitcoin-Qt.app/Contents/MacOS/current")
                            link.create_system = 3
                            link.external_attr = 0o120755 << 16
                            package.writestr(link, "Bitcoin-Qt")
                result = subprocess.run([sys.executable, REPACK_TOOL, source, destination, root, inputs, platform], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                proof = json.loads(result.stdout)
                self.assertTrue(proof["payload_bytes_modes_links_unchanged"])
                self.assertEqual(proof["unchanged_members"], 2 if platform.startswith("darwin") else 1)
                NOTICES.validate_archive(destination, root, platform)
                if destination.suffix == ".zip":
                    with zipfile.ZipFile(destination) as package:
                        self.assertEqual(package.read(member_name), payload)
                        self.assertEqual(package.getinfo(member_name).external_attr >> 16,
                                         0 if platform.startswith("windows") else 0o100755)
                        if platform.startswith("darwin"):
                            self.assertEqual(package.read(link.filename), b"Bitcoin-Qt")
                        added = [name for name in package.namelist() if "/COPYING" in name or "/notices/" in name]
                        self.assertTrue(all(".app/" not in name for name in added))
                else:
                    with tarfile.open(destination) as package:
                        self.assertEqual(package.extractfile(member_name).read(), payload)
                        self.assertEqual(package.getmember(member_name).mode, 0o755)
                retry = subprocess.run([sys.executable, REPACK_TOOL, source, destination, root, inputs, platform], capture_output=True)
                self.assertNotEqual(retry.returncode, 0)


if __name__ == "__main__":
    unittest.main()
