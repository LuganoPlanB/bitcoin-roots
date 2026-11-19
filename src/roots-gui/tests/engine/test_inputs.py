# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Offline source delivery and fail-closed preparation regressions."""

import hashlib
import io
import json
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile
import unittest

sys.dont_write_bytecode = True
GUI = Path(__file__).resolve().parents[2]
BUNDLED = GUI / "third_party"
sys.path.insert(0, str(BUNDLED))
sys.path.insert(0, str(GUI / "tests"))
from prepare_test_inputs import prepare
from vendor_test_inputs import package
from support.runtime import validated_scratch


class Inputs(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(
            prefix="test-input-contract-", dir=validated_scratch()
        )
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "bundle"
        (self.source / "test_inputs").mkdir(parents=True)
        for name in [
            "test-inputs.tar.gz",
            "test-inputs-lock.json",
            "test_inputs/manifest.json",
        ]:
            shutil.copyfile(BUNDLED / name, self.source / name)
        self.destination = self.root / "prepared"

    def manifest(self, change):
        path = self.source / "test_inputs/manifest.json"
        data = json.loads(path.read_text())
        change(data)
        path.write_text(json.dumps(data))
        lock = json.loads((self.source / "test-inputs-lock.json").read_text())
        lock["manifestSha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        (self.source / "test-inputs-lock.json").write_text(json.dumps(lock))

    def archive(self, entries):
        path = self.source / "test-inputs.tar.gz"
        with tarfile.open(path, "w:gz") as archive:
            for name, data in entries:
                info = tarfile.TarInfo(name)
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
        lock = json.loads((self.source / "test-inputs-lock.json").read_text())
        lock["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        (self.source / "test-inputs-lock.json").write_text(json.dumps(lock))

    def run_prepare(self):
        return prepare(self.destination, source=self.source)

    def test_original_reproducible(self):
        result = self.run_prepare()
        self.assertEqual(result["files"], 37)
        self.assertEqual(
            result["engineRevision"], "7c808483d6f0a69b9d0843d5f4a141f0a9f74a58"
        )
        manifest = json.loads((self.destination / "manifest.json").read_text())
        for item in manifest["files"]:
            self.assertEqual(
                hashlib.sha256(
                    (self.destination / item["path"]).read_bytes()
                ).hexdigest(),
                item["sha256"],
            )
        self.assertEqual(
            (self.destination / "manifest.json").read_bytes(),
            (BUNDLED / "test_inputs/manifest.json").read_bytes(),
        )
        for name in [
            "engine/imgui_test_engine/LICENSE.txt",
            "engine/imgui_test_engine/thirdparty/Str/Str.h",
            "imkit/LICENSE",
            "imgui/LICENSE.txt",
        ]:
            self.assertTrue((self.destination / name).is_file())
        output = self.root / "repacked.tar.gz"
        package(self.destination, output)
        self.assertEqual(
            output.read_bytes(), (BUNDLED / "test-inputs.tar.gz").read_bytes()
        )

    def test_idempotent(self):
        self.run_prepare()
        before = {
            p: p.stat().st_mtime_ns for p in self.destination.rglob("*") if p.is_file()
        }
        self.run_prepare()
        self.assertEqual(before, {p: p.stat().st_mtime_ns for p in before})

    def test_tampered_prepared(self):
        self.run_prepare()
        (self.destination / "engine/imgui_test_engine/LICENSE.txt").write_text("tamper")
        with self.assertRaisesRegex(ValueError, "prepared test source differs"):
            self.run_prepare()

    def test_extra_prepared(self):
        self.run_prepare()
        (self.destination / "extra").write_text("synthetic")
        with self.assertRaisesRegex(ValueError, "file set differs"):
            self.run_prepare()

    def test_archive_hash(self):
        with (self.source / "test-inputs.tar.gz").open("ab") as file:
            file.write(b"tamper")
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            self.run_prepare()
        self.assertFalse(self.destination.exists())

    def test_manifest_hash(self):
        with (self.source / "test_inputs/manifest.json").open("a") as file:
            file.write(" ")
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            self.run_prepare()

    def test_original_pin(self):
        self.manifest(lambda m: m.update(engineRevision="wrong"))
        with self.assertRaisesRegex(ValueError, "upstream test source pin differs"):
            self.run_prepare()

    def test_duplicate_archive(self):
        self.archive([("same", b"x"), ("same", b"x")])
        with self.assertRaisesRegex(ValueError, "archive file set differs"):
            self.run_prepare()
        self.assertFalse(self.destination.exists())

    def test_unsafe_member(self):
        self.archive([("../escape", b"x")])
        self.manifest(
            lambda m: m.update(
                files=[dict(path="../escape", sha256=hashlib.sha256(b"x").hexdigest())]
            )
        )
        with self.assertRaisesRegex(ValueError, "unsafe test archive member"):
            self.run_prepare()
        self.assertFalse((self.root / "escape").exists())

    def test_empty_manifest(self):
        self.manifest(lambda m: m.update(files=[]))
        with self.assertRaisesRegex(ValueError, "nonempty and unique"):
            self.run_prepare()

    def test_source_destination(self):
        with self.assertRaisesRegex(ValueError, "outside the source repository"):
            prepare(GUI / "forbidden-input-fixture")
        self.assertFalse((GUI / "forbidden-input-fixture").exists())

    def test_symlink_prepared(self):
        self.run_prepare()
        for name in ["manifest.json", "engine/imgui_test_engine/LICENSE.txt"]:
            with self.subTest(name=name):
                target = self.destination / name
                data = target.read_bytes()
                outside = self.root / "symlink-target"
                outside.write_bytes(data)
                target.unlink()
                target.symlink_to(outside)
                with self.assertRaisesRegex(
                    ValueError, "prepared test (source|manifest) differs"
                ):
                    self.run_prepare()
                target.unlink()
                target.write_bytes(data)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(Inputs)
    selected = suite.countTestCases()
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    print(
        json.dumps(
            dict(
                selected=selected,
                executed=result.testsRun,
                passed=result.testsRun - len(result.failures) - len(result.errors),
                failed=len(result.failures) + len(result.errors),
                skipped=len(result.skipped),
            )
        )
    )
    raise SystemExit(
        int(
            not result.wasSuccessful()
            or selected != 12
            or result.testsRun != selected
            or bool(result.skipped)
        )
    )
