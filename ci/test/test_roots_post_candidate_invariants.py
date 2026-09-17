"""Negative fixtures for post-candidate runtime containment."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "contrib/devtools/roots-post-candidate-invariants.py"
MANIFEST = ROOT / "contrib/roots/post-candidate-replay-29.4.json"
EVIDENCE = ROOT / "contrib/roots/post-candidate-invariants-29.4.json"
SPEC = importlib.util.spec_from_file_location("invariants", TOOL)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load invariant module")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def run(*arguments: object, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([sys.executable, str(TOOL), *map(str, arguments)], cwd=ROOT, capture_output=True, text=True)
    if check and result.returncode:
        raise AssertionError(result.stderr)
    return result


class InvariantsTest(unittest.TestCase):
    def test_locked_evidence(self) -> None:
        self.assertEqual(run("verify", "--repository", ROOT, "--manifest", MANIFEST, "--output", EVIDENCE).returncode, 0)

    def test_direct_runtime_zone_classification(self) -> None:
        owned = {"doc/owned.md"}
        self.assertEqual(MODULE.classify_path("src/policy/policy.h", owned), "unowned_path")
        owned.update({"src/policy/policy.h", "src/validation.cpp", "src/chainparams.cpp", "src/script/interpreter.cpp"})
        self.assertEqual(MODULE.classify_path("src/policy/policy.h", owned), "policy_consensus")
        self.assertEqual(MODULE.classify_path("src/validation.cpp", owned), "consensus_validation")
        self.assertEqual(MODULE.classify_path("src/chainparams.cpp", owned), "chain_parameters")
        self.assertEqual(MODULE.classify_path("src/script/interpreter.cpp", owned), "serialization_script")

    def test_valid_shape_wrong_patch_digest_rejects(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            manifest = Path(temporary) / "manifest.json"
            value = json.loads(MANIFEST.read_text(encoding="utf-8"))
            value["handler"]["patch_sha256"] = "sha256:" + "0" * 64
            manifest.write_text(json.dumps(value), encoding="utf-8")
            self.assertNotEqual(run("verify", "--repository", ROOT, "--manifest", manifest, "--output", EVIDENCE, check=False).returncode, 0)


if __name__ == "__main__":
    unittest.main()
