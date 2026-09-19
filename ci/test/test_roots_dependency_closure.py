"""Regression coverage for source-locked dependency closure evidence."""

from __future__ import annotations

import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "contrib/devtools/roots-dependency-closure.py"
MANIFEST = ROOT / "contrib/roots/post-candidate-replay-29.4.json"
EVIDENCE = ROOT / "contrib/roots/dependency-closure-29.4.json"


def run(*args: object) -> subprocess.CompletedProcess[str]:
    return subprocess.run([sys.executable, str(TOOL), *map(str, args)], cwd=ROOT, capture_output=True, text=True, check=False)


class DependencyClosureTest(unittest.TestCase):
    def test_checked_evidence_is_exact_and_complete(self) -> None:
        result = run("verify", "--repository", ROOT, "--manifest", MANIFEST, "--output", EVIDENCE)
        self.assertEqual(result.returncode, 0, result.stderr)
        value = json.loads(EVIDENCE.read_text(encoding="utf-8"))
        self.assertEqual(len(value["closure"]), 155)
        obsolete = [entry for entry in value["closure"] if entry["disposition"] == "obsolete"]
        self.assertEqual([entry["path"] for entry in obsolete], [
            "ci/test/test_roots_hardening_handoff.py",
            "contrib/roots/replay-29.4-proposal/incremental-oracle.bash",
        ])
        overlay_only = [entry for entry in value["closure"] if entry["source_blob"] is None]
        self.assertEqual([entry["path"] for entry in overlay_only], [
            "contrib/roots/construct-canonical-29.4.bash",
            "contrib/roots/create-local-integration-baseline.bash",
            "contrib/roots/local-integration-baseline-29.4.json",
            "contrib/roots/promotion-29.4.json",
            "contrib/roots/validate-local-integration-baseline.py",
        ])
        self.assertTrue(all(entry["disposition"] == "adapted" for entry in overlay_only))
        self.assertTrue(all(entry["provenance"].startswith("overlay:sha256:") for entry in overlay_only))
        excluded_methods = [item["method"] for item in value["module_matrix"]["excluded_methods"]]
        self.assertTrue(all("*" not in method for method in excluded_methods))
        historical = []
        for module, case in (
            ("test_roots_llm_contract", "RootsLlmContractTest"),
            ("test_roots_lineage", "RootsLineageTest"),
            ("test_roots_maintainer_runbook", "MaintainerRunbookTest"),
            ("test_roots_replay", "RootsReplayTest"),
        ):
            source = (ROOT / "ci/test" / f"{module}.py").read_text(encoding="utf-8")
            historical.extend(
                f"ci.test.{module}.{case}.{name}"
                for name in re.findall(r"^    def (_historical_[A-Za-z0-9_]+)\(self\):", source, re.MULTILINE)
            )
        self.assertCountEqual(excluded_methods, historical)

    def test_stale_entry_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "closure.json"
            output.write_bytes(EVIDENCE.read_bytes())
            value = json.loads(output.read_text(encoding="utf-8"))
            value["closure"][0]["sha256"] = "sha256:" + "0" * 64
            output.write_text(json.dumps(value), encoding="utf-8")
            result = run("verify", "--repository", ROOT, "--manifest", MANIFEST, "--output", output)
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
