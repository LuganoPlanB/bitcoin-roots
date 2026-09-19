#!/usr/bin/env python3
"""Tests for the fail-closed, read-only Roots promotion topology validator."""
from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "contrib/devtools/roots-promotion-contract.py"
SOURCE_SCRIPT = ROOT / "ci/release/validate-promotion-source.py"
spec = importlib.util.spec_from_file_location("promotion_contract", SCRIPT)
PROMOTION = importlib.util.module_from_spec(spec)
spec.loader.exec_module(PROMOTION)
source_spec = importlib.util.spec_from_file_location("promotion_source", SOURCE_SCRIPT)
SOURCE = importlib.util.module_from_spec(source_spec)
source_spec.loader.exec_module(SOURCE)


class PromotionContractTest(unittest.TestCase):
    def git(self, repository: Path, *args: str) -> str:
        return subprocess.run(["git", "-C", repository, *args], text=True, capture_output=True, check=True).stdout.strip()

    def fixture(self, root: Path) -> tuple[Path, Path, dict]:
        repository = root / "repository"
        subprocess.run(["git", "init", repository], check=True, capture_output=True)
        self.git(repository, "config", "user.name", "Roots test")
        self.git(repository, "config", "user.email", "roots@test.invalid")
        (repository / "base").write_text("Core\n", encoding="utf-8")
        self.git(repository, "add", "base")
        self.git(repository, "commit", "-m", "Core")
        core = self.git(repository, "rev-parse", "HEAD")
        core_tree = self.git(repository, "rev-parse", "HEAD^{tree}")
        self.git(repository, "tag", "-a", "v29.4", "-m", "v29.4")
        tag_object = self.git(repository, "rev-parse", "v29.4^{tag}")
        (repository / "doc").mkdir()
        (repository / "contrib/roots/replay-29.4-proposal").mkdir(parents=True)
        (repository / "CMakeLists.txt").write_text("set(CLIENT_VERSION_MAJOR 29)\nset(CLIENT_VERSION_MINOR 4)\n", encoding="utf-8")
        (repository / "doc/release-notes.md").write_text("Roots 29.4\n", encoding="utf-8")
        (repository / "contrib/roots/release-accounting.json").write_text('{"schema_version":2,"changes":[]}\n', encoding="utf-8")
        (repository / "roots").write_text("Roots\n", encoding="utf-8")
        self.git(repository, "add", ".")
        self.git(repository, "commit", "-m", "Roots adaptation")
        canonical = self.git(repository, "rev-parse", "HEAD")
        tree = self.git(repository, "rev-parse", "HEAD^{tree}")
        (repository / "contrib/roots/replay-29.4-proposal/acceptance-evidence.json").write_text(json.dumps({"candidate": {"tree": "sha1:" + tree}}), encoding="utf-8")
        self.git(repository, "branch", "integration/roots-29.4", canonical)
        self.git(repository, "branch", "roots/29.4", canonical)
        record = {"schema_version": 1, "core": {"tag": "v29.4", "tag_object": f"sha1:{tag_object}", "commit": f"sha1:{core}", "tree": f"sha1:{core_tree}"}, "candidate": {"tree": f"sha1:{tree}", "canonical_commit": f"sha1:{canonical}", "input_commit": f"sha1:{canonical}", "forbidden_old_trunk_commit": "sha1:" + "f" * 40}, "control_plane": {"branch": "refs/heads/codex/roots-29-4-release", "base_commit": "sha1:" + "e" * 40, "candidate_transfer": "exact-bundle-or-commit"}, "production": {"integration_ref": "refs/heads/integration/roots-29.4", "production_ref": "refs/heads/roots/29.4", "release_tag": "refs/tags/v29.4-roots.1", "head": f"sha1:{canonical}", "fast_forward_only": True}, "authorization": False}
        path = root / "promotion.json"
        path.write_text(json.dumps(record), encoding="utf-8")
        return repository, path, record

    def test_accepts_exact_topology_twice_in_neutral_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            repository, path, _ = self.fixture(Path(directory))
            for _ in range(2):
                result = subprocess.run(["env", "-i", "PATH=" + __import__("os").environ["PATH"], "LC_ALL=C", "LANG=C", "TZ=UTC", "GIT_CONFIG_NOSYSTEM=1", "GIT_CONFIG_GLOBAL=/dev/null", "GIT_NO_REPLACE_OBJECTS=1", "python3", SCRIPT, path, "--repository", repository], text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_changed_immutable_bindings_and_unsafe_topology(self):
        with tempfile.TemporaryDirectory() as directory:
            repository, path, record = self.fixture(Path(directory))
            mutations = (("core", "tag_object"), ("core", "commit"), ("core", "tree"), ("candidate", "tree"), ("candidate", "canonical_commit"), ("production", "head"))
            for section, field in mutations:
                with self.subTest(section=section, field=field):
                    mutated = copy.deepcopy(record)
                    mutated[section][field] = "sha1:" + "0" * 40
                    path.write_text(json.dumps(mutated), encoding="utf-8")
                    with self.assertRaises(PROMOTION.ContractError): PROMOTION.validate(path, repository)
            for field, value in (("authorization", True), ("candidate", {**record["candidate"], "input_commit": "sha1:" + "0" * 40}), ("production", {**record["production"], "integration_ref": "refs/heads/main"})):
                with self.subTest(field=field):
                    mutated = copy.deepcopy(record)
                    mutated[field] = value
                    path.write_text(json.dumps(mutated), encoding="utf-8")
                    with self.assertRaises(PROMOTION.ContractError): PROMOTION.validate(path, repository)
            path.write_text(json.dumps(record), encoding="utf-8")
            self.git(repository, "branch", "-f", "roots/29.4", "HEAD~1")
            with self.assertRaises(PROMOTION.ContractError): PROMOTION.validate(path, repository)

    def test_rejects_merges_old_trunk_vendor_alternates_and_host_overrides(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            repository, path, record = self.fixture(root)
            for case in ("merge", "vendor", "old", "alternates"):
                with self.subTest(case=case):
                    target, target_path, target_record = self.fixture(root / case)
                    if case == "merge":
                        primary = self.git(target, "branch", "--show-current")
                        self.git(target, "checkout", "-b", "side", "HEAD~1")
                        (target / "side").write_text("side\n", encoding="utf-8")
                        self.git(target, "add", "side"); self.git(target, "commit", "-m", "side")
                        self.git(target, "checkout", primary)
                        self.git(target, "merge", "--no-ff", "side", "-m", "merge")
                    elif case == "vendor":
                        (target / "vendor").mkdir(); (target / "vendor" / "copy").write_text("x", encoding="utf-8")
                        self.git(target, "add", "vendor"); self.git(target, "commit", "-m", "vendor")
                    elif case == "old":
                        target_record["candidate"]["forbidden_old_trunk_commit"] = "sha1:" + self.git(target, "rev-parse", "HEAD~1")
                    else:
                        git_dir = Path(self.git(target, "rev-parse", "--git-dir")); (target / git_dir / "objects/info").mkdir(exist_ok=True); (target / git_dir / "objects/info/alternates").write_text("/tmp/objects\n", encoding="utf-8")
                    if case in {"merge", "vendor"}:
                        canonical = self.git(target, "rev-parse", "HEAD")
                        target_record["candidate"].update({"canonical_commit": "sha1:" + canonical, "input_commit": "sha1:" + canonical, "tree": "sha1:" + self.git(target, "rev-parse", "HEAD^{tree}")})
                        target_record["production"]["head"] = "sha1:" + canonical
                        self.git(target, "branch", "-f", "integration/roots-29.4", canonical); self.git(target, "branch", "-f", "roots/29.4", canonical)
                    target_path.write_text(json.dumps(target_record), encoding="utf-8")
                    with self.assertRaises(PROMOTION.ContractError): PROMOTION.validate(target_path, target)
            with self.assertRaises(PROMOTION.ContractError):
                with mock.patch.dict("os.environ", {"GIT_DIR": "/tmp/nope"}, clear=False):
                    PROMOTION.validate(path, repository)

    def test_release_source_requires_authorization_and_exact_production_commit(self):
        with tempfile.TemporaryDirectory() as directory:
            repository, path, record = self.fixture(Path(directory))
            canonical = record["production"]["head"].removeprefix("sha1:")
            with self.assertRaises(SOURCE.CONTRACT.ContractError):
                SOURCE.validate(path, repository, canonical)
            record["authorization"] = True
            path.write_text(json.dumps(record), encoding="utf-8")
            SOURCE.validate(path, repository, canonical)
            with self.assertRaises(SOURCE.CONTRACT.ContractError):
                SOURCE.validate(path, repository, "0" * 40)
            for relative, content in (("CMakeLists.txt", "set(CLIENT_VERSION_MAJOR 29)\nset(CLIENT_VERSION_MINOR 3)\n"), ("doc/release-notes.md", "Roots 29.3\n"), ("contrib/roots/replay-29.4-proposal/acceptance-evidence.json", '{"candidate":{"tree":"sha1:' + "0" * 40 + '"}}'), ("contrib/roots/release-accounting.json", '{"schema_version":1,"changes":[]}')):
                with self.subTest(relative=relative):
                    target = repository / relative
                    original = target.read_text(encoding="utf-8")
                    target.write_text(content, encoding="utf-8")
                    with self.assertRaises(SOURCE.CONTRACT.ContractError):
                        SOURCE.validate(path, repository, canonical)
                    target.write_text(original, encoding="utf-8")

    def test_create_release_workflow_uses_exact_production_inputs(self):
        workflow = (ROOT / ".github/workflows/create-release.yml").read_text(encoding="utf-8")
        for required in ("production_ref:", "expected_commit:", "refs/heads/${{ inputs.production_ref }}", "validate-promotion-source.py", "refs/remotes/origin/$PRODUCTION_REF"):
            self.assertIn(required, workflow)
        self.assertNotIn("ref: main", workflow)
        self.assertIn("gh run list --workflow release.yml", workflow)
        self.assertIn("Existing release tag is lightweight", workflow)


if __name__ == "__main__":
    unittest.main()
