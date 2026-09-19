"""Tests for the immutable post-candidate Roots 29.4 atom inventory."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "contrib/devtools/roots-post-candidate-inventory.py"
INVENTORY = ROOT / "contrib/roots/post-candidate-inventory-29.4.json"
BASE = "dd3050a6101a071a0b642ba71ab7eaddbe4cf5b8"
HEAD = "e2387f0d975121869064e55eb0afb99e7639120b"


def load_tool():
    spec = importlib.util.spec_from_file_location("roots_post_candidate_inventory", TOOL)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


INVENTORY_TOOL = load_tool()


def invoke(*arguments: object, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [sys.executable, str(TOOL), *[str(argument) for argument in arguments]],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if check and result.returncode:
        raise AssertionError(result.stderr)
    return result


class PostCandidateInventoryTest(unittest.TestCase):
    def build(self, directory: Path, name: str = "inventory.json") -> Path:
        output = directory / name
        report = json.loads(invoke("build", "--repository", ROOT, "--output", output).stdout)
        self.assertEqual(report["decision"], "accepted")
        self.assertEqual(report["units"], 15)
        self.assertEqual(report["atoms"], 193)
        return output

    def test_exact_fifteen_commit_inventory_is_deterministic(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            first = self.build(directory, "first.json")
            second = self.build(directory, "second.json")
            self.assertEqual(first.read_bytes(), second.read_bytes())
            value = json.loads(first.read_text(encoding="utf-8"))
            self.assertEqual(value["base_commit"], "sha1:" + BASE)
            self.assertEqual(value["head_commit"], "sha1:" + HEAD)
            self.assertEqual(len(value["units"]), 15)
            self.assertEqual(len(value["atoms"]), 193)
            self.assertTrue(any(atom["disposition"] == "obsolete" for atom in value["atoms"]))
            self.assertTrue(any(atom["disposition"] == "adapted" for atom in value["atoms"]))

    def test_verify_recomputes_all_locked_atom_data(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = self.build(Path(temporary))
            report = json.loads(invoke("verify", "--repository", ROOT, "--output", output).stdout)
            self.assertEqual(report["decision"], "accepted")
            value = json.loads(output.read_text(encoding="utf-8"))
            value["atoms"][0]["new_blob_sha256"] = "sha256:" + "0" * 64
            output.write_text(json.dumps(value, sort_keys=True) + "\n", encoding="utf-8")
            rejected = invoke("verify", "--repository", ROOT, "--output", output, check=False)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("inventory", rejected.stderr)

    def test_checked_in_inventory_matches_the_locked_git_objects(self):
        report = json.loads(invoke("verify", "--repository", ROOT, "--output", INVENTORY).stdout)
        self.assertEqual(report["decision"], "accepted")
        self.assertEqual(report["units"], 15)
        self.assertEqual(report["atoms"], 193)

    def test_rejects_mutable_or_wrong_range_endpoints(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "inventory.json"
            for option, value in (("--base", HEAD), ("--head", BASE), ("--candidate", "HEAD")):
                rejected = invoke("build", "--repository", ROOT, option, value, "--output", output, check=False)
                self.assertNotEqual(rejected.returncode, 0)
                self.assertIn("endpoint", rejected.stderr)

    def test_existing_output_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = self.build(Path(temporary))
            rejected = invoke("build", "--repository", ROOT, "--output", output, check=False)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("overwrite", rejected.stderr)

    def test_validator_rejects_duplicate_unowned_dependency_and_critical_atoms(self):
        value = json.loads(INVENTORY.read_text(encoding="utf-8"))
        value.pop("inventory_sha256")
        duplicate = json.loads(json.dumps(value))
        duplicate["atoms"].append(dict(duplicate["atoms"][0]))
        with self.assertRaises(INVENTORY_TOOL.InventoryError):
            INVENTORY_TOOL.validate_inventory(duplicate)
        unowned = json.loads(json.dumps(value))
        unowned["atoms"][0]["adaptation_id"] = "roots-post-candidate-missing-v1"
        with self.assertRaises(INVENTORY_TOOL.InventoryError):
            INVENTORY_TOOL.validate_inventory(unowned)
        dependency = json.loads(json.dumps(value))
        dependency["units"][1]["dependencies"] = []
        with self.assertRaises(INVENTORY_TOOL.InventoryError):
            INVENTORY_TOOL.validate_inventory(dependency)
        critical = json.loads(json.dumps(value))
        critical["atoms"][0]["path"] = "src/validation.cpp"
        critical["atoms"][0]["risk"] = "low"
        with self.assertRaises(INVENTORY_TOOL.InventoryError):
            INVENTORY_TOOL.validate_inventory(critical)


if __name__ == "__main__":
    unittest.main()
