# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Standalone temp selection and fail-closed managed owner regressions."""

import sys

sys.dont_write_bytecode = True
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import validated_scratch


class ScratchContracts(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(
            prefix="roots-scratch-regression-", dir=validated_scratch()
        )
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.managed = self.root / "managed"
        self.managed.mkdir()
        self.marker = self.managed / "owner.json"
        self.packet = dict(
            directory=str(self.managed),
            markerPath=str(self.marker),
            ownerId="fixture-owner",
            workspacePath=str(self.root),
        )
        self.marker.write_text(
            json.dumps(dict(ownerId="fixture-owner", workspacePath=str(self.root)))
        )
        self.environment = dict(
            GESTALT_SESSION_TMP_CONTEXT=json.dumps(self.packet),
            TMPDIR=str(self.managed),
            TMP=str(self.managed),
            TEMP=str(self.managed),
        )

    def allocate(self, environment, expected):
        with (
            mock.patch.dict(os.environ, environment, clear=True),
            mock.patch.object(tempfile, "tempdir", None),
        ):
            before = dict(os.environ)
            folder = validated_scratch()
            self.assertEqual(folder, expected)
            with tempfile.TemporaryDirectory(
                prefix="actual-child-", dir=folder
            ) as child:
                self.assertEqual(Path(child).parent.resolve(), expected)
            self.assertEqual(dict(os.environ), before)

    def rejected(self, environment):
        with (
            mock.patch.dict(os.environ, environment, clear=True),
            mock.patch(
                "support.runtime.tempfile.gettempdir",
                side_effect=AssertionError(
                    "invalid managed context must not downgrade"
                ),
            ),
        ):
            before = dict(os.environ)
            with self.assertRaises((ValueError, OSError)):
                validated_scratch()
            self.assertEqual(dict(os.environ), before)

    def test_absent_explicit_tmpdir(self):
        self.allocate({"TMPDIR": str(self.root)}, self.root)

    def test_absent_explicit_temp_or_tmp(self):
        for name in ["TEMP", "TMP"]:
            with self.subTest(variable=name):
                self.allocate({name: str(self.root)}, self.root)

    def test_absent_default(self):
        with (
            mock.patch.dict(os.environ, {}, clear=True),
            mock.patch.object(tempfile, "tempdir", None),
        ):
            expected = Path(tempfile.gettempdir()).resolve()
        self.allocate({}, expected)

    def test_valid_managed_and_child_inheritance(self):
        self.allocate(self.environment, self.managed)
        with mock.patch.dict(os.environ, self.environment, clear=True):
            result = subprocess.run(
                [
                    sys.executable,
                    "-B",
                    "-c",
                    'import json,os;print(json.dumps({k:os.environ.get(k) for k in ["GESTALT_SESSION_TMP_CONTEXT","TMPDIR","TMP","TEMP"]}))',
                ],
                capture_output=True,
                text=True,
                timeout=5,
                check=True,
            )
            self.assertEqual(json.loads(result.stdout), self.environment)

    def test_invalid_present_payload(self):
        for value in ["", " ", "{", "null", "[]", "1", "{}"]:
            with self.subTest(payload=value):
                self.rejected(self.environment | {"GESTALT_SESSION_TMP_CONTEXT": value})

    def test_invalid_present_fields(self):
        for key in self.packet:
            for value in ["missing", "", None, 1]:
                with self.subTest(field=key, value=value):
                    packet = self.packet.copy()
                    if value == "missing":
                        packet.pop(key)
                    else:
                        packet[key] = value
                    self.rejected(
                        self.environment
                        | {"GESTALT_SESSION_TMP_CONTEXT": json.dumps(packet)}
                    )

    def test_invalid_marker(self):
        valid = self.marker.read_text()
        for value in [
            None,
            "",
            "{",
            "[]",
            "{}",
            json.dumps(dict(ownerId="wrong", workspacePath=str(self.root))),
            json.dumps(
                dict(ownerId="fixture-owner", workspacePath=str(self.root / "wrong"))
            ),
        ]:
            with self.subTest(marker=value):
                if value is None:
                    self.marker.unlink(missing_ok=True)
                else:
                    self.marker.write_text(value)
                self.rejected(self.environment)
        self.marker.write_text(valid)

    def test_invalid_managed_tmpdir(self):
        for value in [None, "", str(self.root)]:
            with self.subTest(tmpdir=value):
                environment = self.environment.copy()
                if value is None:
                    environment.pop("TMPDIR")
                else:
                    environment["TMPDIR"] = value
                self.rejected(environment)
        not_directory = self.root / "ordinary-file"
        not_directory.write_text("fixture")
        for folder in [self.root / "missing-directory", not_directory]:
            with self.subTest(unavailable=str(folder)):
                packet = self.packet | {"directory": str(folder)}
                self.rejected(
                    self.environment
                    | {
                        "TMPDIR": str(folder),
                        "GESTALT_SESSION_TMP_CONTEXT": json.dumps(packet),
                    }
                )

    def test_v2_short_runtime_owned_alias(self):
        runtime = self.root / "short-runtime"
        runtime.mkdir()
        stat = runtime.stat()
        binding = dict(
            directory=str(runtime),
            token="public-fixture-token",
            identity=f"{stat.st_dev}:{stat.st_ino}:{stat.st_ctime_ns}",
        )
        packet = self.packet | dict(schemaVersion=2, runtime=binding)
        self.marker.write_text(
            json.dumps(
                dict(
                    ownerId="fixture-owner",
                    workspacePath=str(self.root),
                    runtime=binding,
                )
            )
        )
        receipt = dict(
            schemaVersion=2,
            ownerId="fixture-owner",
            workspacePath=str(self.root),
            token=binding["token"],
            identity=binding["identity"],
        )
        runtime_marker = runtime / ".gestalt-runtime.json"
        runtime_marker.write_text(json.dumps(receipt))
        environment = self.environment | dict(
            GESTALT_SESSION_TMP_CONTEXT=json.dumps(packet),
            TMPDIR=str(runtime),
            TMP=str(runtime),
            TEMP=str(runtime),
        )
        self.allocate(environment, runtime)
        for key in ("token", "identity", "ownerId", "workspacePath", "schemaVersion"):
            with self.subTest(mismatched_runtime_field=key):
                runtime_marker.write_text(json.dumps(receipt | {key: "wrong"}))
                self.rejected(environment)
        runtime_marker.write_text(json.dumps(receipt))
        self.rejected(environment | {"TEMP": str(self.managed)})
        self.rejected(
            environment
            | {"GESTALT_SESSION_TMP_CONTEXT": json.dumps(packet | {"runtime": None})}
        )


class Result(unittest.TextTestResult):
    subcases = 0

    def addSubTest(self, test, subtest, error):
        self.subcases += 1
        super().addSubTest(test, subtest, error)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ScratchContracts)
    result = unittest.TextTestRunner(verbosity=2, resultclass=Result).run(suite)
    failed = {
        test.id().split(" (", 1)[0] for test, _ in result.errors + result.failures
    }
    executed = result.testsRun - len(result.skipped)
    print(
        json.dumps(
            dict(
                selected=9,
                executed=executed,
                passed=executed - len(failed),
                failed=len(failed),
                skipped=len(result.skipped),
                subcases=result.subcases,
            )
        )
    )
    raise SystemExit(int(not result.wasSuccessful() or executed != 9))
