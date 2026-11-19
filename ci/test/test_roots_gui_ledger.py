# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying file COPYING.
"""A partial GUI build must retain exact static test-to-capability bindings."""

import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "src/roots-gui/tests/model/capability_ledger.py"
SPEC = importlib.util.spec_from_file_location("gui_capability_ledger", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class PartialBuildLedgerTest(unittest.TestCase):
    def test_ctest_omits_unbuilt_command_but_declared_binding_is_checked(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            declaration = 'add_test(NAME registered COMMAND missing_engine "fixture.case")'
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.22)\nproject(ledger LANGUAGES NONE)\n"
                "enable_testing()\n" + declaration + "\n", encoding="utf-8")
            (source / "case.cpp").write_text('Test(engine, "fixture_case");\n', encoding="utf-8")
            build = source / "build"
            subprocess.run(["cmake", "-S", str(source), "-B", str(build)], check=True,
                           capture_output=True, text=True, timeout=15)
            result = subprocess.check_output(
                ["ctest", "--test-dir", str(build), "--show-only=json-v1"],
                text=True, timeout=10)
            test = json.loads(result)["tests"][0]
            self.assertEqual(test["name"], "registered")
            self.assertNotIn("command", test)
            entry = dict(kind="engine", file="case.cpp", id="fixture_case", commandArgument="missing_engine")
            MODULE.verify_case(source, entry, MODULE.declared_command(declaration, "registered"))
            with self.assertRaises(ValueError):
                MODULE.verify_case(source, entry, MODULE.declared_command(
                    declaration.replace("missing_engine", "wrong_engine"), "registered"))
            with self.assertRaises(ValueError):
                MODULE.verify_case(source, entry, ["wrong_built_engine"])

    def test_target_file_generator_expression_retains_exact_target(self):
        command = MODULE.declared_command(
            'add_test(NAME native COMMAND "${Python3_EXECUTABLE}" --binary "$<TARGET_FILE:owned_capture>")',
            "native")
        self.assertIn("owned_capture", command)
        with self.assertRaises(ValueError):
            MODULE.declared_command("add_test(NAME other COMMAND wrong)", "native")


if __name__ == "__main__":
    unittest.main()
