"""Tests for the locked post-candidate infrastructure replay handler."""

from __future__ import annotations

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "contrib/devtools/roots-post-candidate-replay.py"
INVENTORY = ROOT / "contrib/roots/post-candidate-inventory-29.4.json"
MANIFEST = ROOT / "contrib/roots/post-candidate-replay-29.4.json"
OVERLAY = ROOT / "contrib/roots/replay-29.4-proposal/post-candidate-production-overlay.patch"


def invoke(*arguments: object, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([sys.executable, str(TOOL), *map(str, arguments)], cwd=ROOT, check=False, capture_output=True, text=True)
    if check and result.returncode:
        raise AssertionError(result.stderr)
    return result


class PostCandidateReplayTest(unittest.TestCase):
    def test_checked_in_manifest_recomputes_exactly(self):
        report = json.loads(invoke("verify", "--repository", ROOT, "--inventory", INVENTORY, "--manifest", MANIFEST).stdout)
        self.assertEqual(report["decision"], "accepted")
        self.assertEqual(report["paths"], 153)
        self.assertEqual(report["result_tree"], json.loads(MANIFEST.read_text(encoding="utf-8"))["handler"]["result_tree"])

    def test_two_fresh_owned_replays_match(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            reports = []
            for name in ("one", "two"):
                repository = directory / name
                subprocess.run(["git", "clone", "--quiet", "--no-local", str(ROOT), str(repository)], check=True)
                reports.append(json.loads(invoke("apply", "--repository", repository, "--inventory", INVENTORY, "--manifest", MANIFEST).stdout))
            self.assertEqual(reports[0], reports[1])

    def test_tampered_inventory_manifest_and_owned_ref_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            inventory = directory / "inventory.json"
            manifest = directory / "manifest.json"
            shutil.copyfile(INVENTORY, inventory)
            overlay_root = directory / "overlay-root"
            overlay = overlay_root / "contrib/roots/replay-29.4-proposal/post-candidate-production-overlay.patch"
            overlay.parent.mkdir(parents=True)
            shutil.copyfile(OVERLAY, overlay)
            shutil.copyfile(MANIFEST, manifest)
            value = json.loads(manifest.read_text(encoding="utf-8"))
            value["handler"]["patch_sha256"] = "sha256:" + "0" * 64
            manifest.write_text(json.dumps(value, sort_keys=True) + "\n", encoding="utf-8")
            self.assertNotEqual(invoke("verify", "--repository", ROOT, "--inventory", inventory, "--manifest", manifest, check=False).returncode, 0)
            shutil.copyfile(MANIFEST, manifest)
            overlay.write_bytes(overlay.read_bytes() + b"\n")
            self.assertNotEqual(invoke("verify", "--repository", ROOT, "--inventory", inventory, "--manifest", manifest, "--root", overlay_root, check=False).returncode, 0)
            shutil.copyfile(MANIFEST, manifest)
            for mutation in (
                lambda item: item["candidate"].update({"commit": "sha1:" + "0" * 40}),
                lambda item: item["handler"].update({"result_tree": "sha1:" + "0" * 40}),
                lambda item: item["handler"].update({"paths": list(reversed(item["handler"]["paths"]))}),
                lambda item: item["handler"].update({"kind": "substituted-tool"}),
            ):
                value = json.loads(MANIFEST.read_text(encoding="utf-8"))
                mutation(value)
                manifest.write_text(json.dumps(value, sort_keys=True) + "\n", encoding="utf-8")
                self.assertNotEqual(invoke("verify", "--repository", ROOT, "--inventory", inventory, "--manifest", manifest, check=False).returncode, 0)
            shutil.copyfile(MANIFEST, manifest)
            value = json.loads(inventory.read_text(encoding="utf-8"))
            value["atoms"][0]["path"] = "../unsafe"
            inventory.write_text(json.dumps(value, sort_keys=True) + "\n", encoding="utf-8")
            self.assertNotEqual(invoke("verify", "--repository", ROOT, "--inventory", inventory, "--manifest", manifest, check=False).returncode, 0)
            shutil.copyfile(INVENTORY, inventory)
            repository = directory / "repository"
            subprocess.run(["git", "clone", "--quiet", "--no-local", str(ROOT), str(repository)], check=True)
            arguments = ("apply", "--repository", repository, "--inventory", inventory, "--manifest", manifest, "--output-ref", "refs/roots/29.4/test")
            self.assertEqual(invoke(*arguments).returncode, 0)
            self.assertNotEqual(invoke(*arguments, check=False).returncode, 0)
            self.assertNotEqual(invoke("apply", "--repository", repository, "--inventory", inventory, "--manifest", manifest, "--output-ref", "refs/heads/main", check=False).returncode, 0)


if __name__ == "__main__":
    unittest.main()
