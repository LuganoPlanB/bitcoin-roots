#!/usr/bin/env python3
"""Focused safety tests for the offline Roots replay CLI."""

from __future__ import annotations

import importlib.util
import json
from concurrent.futures import ThreadPoolExecutor
import hashlib
import os
from pathlib import Path
import signal
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "contrib/devtools/roots-replay.py"


def load_module():
    spec = importlib.util.spec_from_file_location("roots_replay", SCRIPT)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


REPLAY = load_module()


def git(repository: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(repository), *args], check=True, text=True, capture_output=True).stdout.strip()


def caller_evidence(repository: Path) -> dict[str, object]:
    """Test-only caller facts not retained in replay state or reports."""
    hooks = repository / ".git" / "hooks"
    hook_hashes = {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in hooks.iterdir()
        if path.is_file()
    }
    return {
        "refs": git(repository, "show-ref", "--head"),
        "config": git(repository, "config", "--local", "--list", "--show-origin"),
        "hooks": hook_hashes,
        "worktrees": git(repository, "worktree", "list", "--porcelain"),
    }


class RootsReplayTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.repository = self.root / "repo"
        subprocess.run(["git", "init", str(self.repository)], check=True, capture_output=True)
        git(self.repository, "config", "user.email", "test@example.invalid")
        git(self.repository, "config", "user.name", "Replay test")
        (self.repository / "one.txt").write_text("one\n", encoding="utf-8")
        git(self.repository, "add", "one.txt")
        git(self.repository, "commit", "-m", "initial")
        self.revision = git(self.repository, "rev-parse", "HEAD")
        self.tree = git(self.repository, "rev-parse", "HEAD^{tree}")

    def tearDown(self):
        self.temp.cleanup()

    def materials_for_manifest(self, directory: Path) -> Path:
        manifest = json.loads((ROOT / "contrib/roots/adaptation-manifest-29.3.json").read_text(encoding="utf-8"))
        path = directory / "replay-materials.json"
        path.write_text(json.dumps({"schema_version": 1, "materials": {unit["application"]["reference"]: {"mechanism": "manual"} for unit in manifest["units"]}}), encoding="utf-8")
        return path

    def fixture_manifest(self, directory: Path) -> tuple[Path, Path]:
        """Small L3-valid manual fixture plus its exact materials envelope."""
        manifest = json.loads((ROOT / "contrib/roots/adaptation-manifest-29.3.json").read_text(encoding="utf-8"))
        manifest["units"] = [manifest["units"][0]]
        manifest["units"][0]["id"] = "roots-test-fixture"
        manifest["units"][0]["application"]["reference"] = "fixture:manual"
        manifest["units"][0]["dependencies"] = []
        path = directory / "adaptation-manifest-29.3.json"
        path.write_text(json.dumps(manifest), encoding="utf-8")
        materials = directory / "replay-materials.json"
        materials.write_text(json.dumps({"schema_version": 1, "materials": {"fixture:manual": {"mechanism": "manual"}}}), encoding="utf-8")
        return path, materials

    def clone_equivalent_source(self, name: str) -> Path:
        clone = self.root / name
        subprocess.run(["git", "clone", str(self.repository), str(clone)], check=True, capture_output=True)
        git(clone, "reset", "--hard", self.revision)
        return clone

    def test_equivalent_sources_have_distinct_paths_and_same_lock(self):
        first, second = self.clone_equivalent_source("source-one"), self.clone_equivalent_source("source-two")
        self.assertNotEqual(first.resolve(), second.resolve())
        self.assertEqual(git(first, "rev-parse", "HEAD"), git(second, "rev-parse", "HEAD"))
        self.assertEqual(git(first, "write-tree"), git(second, "write-tree"))

    def test_equivalent_public_manual_replays_have_identical_state(self):
        first, second = self.clone_equivalent_source("source-a"), self.clone_equivalent_source("source-b")
        outputs = []
        for index, source in enumerate((first, second)):
            root = self.root / f"fixture-{index}"; root.mkdir(); manifest, materials = self.fixture_manifest(root); states = root / "states"; states.mkdir()
            subprocess.run([sys.executable, str(SCRIPT), "replay", "--repository", str(source), "--revision", self.revision, "--expected-tree", self.tree, "--manifest", str(manifest), "--materials", str(materials), "--materials-root", str(root), "--state-directory", str(states), "--apply"], check=True, capture_output=True, text=True)
            outputs.append((states / "replay-state-0001.json").read_bytes())
        self.assertEqual(*outputs)

    def test_two_clean_clone_public_replay_report_and_export_are_byte_identical(self):
        """Exercise the complete public contract from distinct absolute paths."""
        base_revision, base_tree = self.revision, self.tree
        (self.repository / "one.txt").write_text("replayed\n", encoding="utf-8")
        git(self.repository, "add", "one.txt")
        git(self.repository, "commit", "-m", "replay fixture")
        candidate_revision = git(self.repository, "rev-parse", "HEAD")
        candidate_tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        patch = subprocess.run(["git", "-C", str(self.repository), "diff", "--binary", "--full-index", base_revision, candidate_revision], check=True, capture_output=True).stdout
        git(self.repository, "reset", "--hard", base_revision)
        patch_digest = "sha256:" + __import__("hashlib").sha256(patch).hexdigest()
        artifacts = []
        for index, source in enumerate((self.clone_equivalent_source("public-a"), self.clone_equivalent_source("public-b"))):
            fixture = self.root / f"public-fixture-{index}"
            fixture.mkdir()
            (fixture / "fixture.patch").write_bytes(patch)
            manifest, materials = self.fixture_manifest(fixture)
            value = json.loads(manifest.read_text(encoding="utf-8"))
            value["units"][0]["application"] = {"mechanism": "patch", "reference": "fixture:patch"}
            manifest.write_text(json.dumps(value), encoding="utf-8")
            materials.write_text(json.dumps({"schema_version": 1, "materials": {"fixture:patch": {"mechanism": "patch", "patch": "fixture.patch", "expected_patch_sha256": patch_digest, "expected_before_tree": base_tree, "expected_after_tree": candidate_tree}}}), encoding="utf-8")
            states, review = fixture / "states", fixture / "review"
            states.mkdir(); review.mkdir()
            before_snapshot = REPLAY._repository_snapshot(source.resolve())
            before_evidence = caller_evidence(source)
            replay = [sys.executable, str(SCRIPT), "replay", "--repository", str(source), "--revision", base_revision, "--expected-tree", base_tree, "--manifest", str(manifest), "--materials", str(materials), "--materials-root", str(fixture), "--state-directory", str(states), "--apply"]
            subprocess.run(replay, check=True, capture_output=True, text=True)
            state = states / "replay-state-0001.json"
            subprocess.run([sys.executable, str(SCRIPT), "report", "--state", str(state), "--output-directory", str(review)], check=True, capture_output=True, text=True)
            subprocess.run([sys.executable, str(SCRIPT), "export-patches", "--repository", str(states / "owned-candidate"), "--state", str(state), "--output", str(fixture / "replay-generated-series.patch")], check=True, capture_output=True, text=True)
            self.assertEqual(REPLAY._repository_snapshot(source.resolve()), before_snapshot)
            self.assertEqual(caller_evidence(source), before_evidence)
            artifacts.append(tuple(path.read_bytes() for path in (state, review / "replay-review.json", review / "replay-review.txt", fixture / "replay-generated-series.patch")))
        self.assertEqual(*artifacts)

    def test_fixture_manifest_and_materials_are_exactly_validated(self):
        manifest, materials = self.fixture_manifest(self.root)
        units = REPLAY.read_manifest_units(manifest)
        self.assertEqual(units[0]["reference"], "fixture:manual")
        self.assertEqual(REPLAY.read_replay_materials(materials, units)["schema_version"], 1)

    def test_fixture_public_replay_apply_persists_boundary_and_source(self):
        manifest, materials = self.fixture_manifest(self.root)
        states = self.root / "fixture-states"; states.mkdir()
        before = REPLAY._repository_snapshot(self.repository.resolve())
        result = subprocess.run([sys.executable, str(SCRIPT), "replay", "--repository", str(self.repository), "--revision", self.revision, "--expected-tree", self.tree, "--manifest", str(manifest), "--materials", str(materials), "--materials-root", str(self.root), "--state-directory", str(states), "--apply"], text=True, capture_output=True, check=True)
        self.assertEqual(json.loads(result.stdout)["outcomes"][0]["outcome"], "manual")
        self.assertTrue((states / "replay-state-0001.json").is_file())
        self.assertEqual(REPLAY._repository_snapshot(self.repository.resolve()), before)

    def test_owned_candidate_and_evidence_are_restrictive_under_permissive_umask(self):
        manifest, materials = self.fixture_manifest(self.root)
        states = self.root / "permissive-umask-states"
        states.mkdir()
        command = [
            sys.executable, str(SCRIPT), "replay", "--repository", str(self.repository),
            "--revision", self.revision, "--expected-tree", self.tree,
            "--manifest", str(manifest), "--materials", str(materials),
            "--materials-root", str(self.root), "--state-directory", str(states), "--apply",
        ]
        previous_umask = os.umask(0)
        try:
            subprocess.run(command, check=True, capture_output=True, text=True)
        finally:
            os.umask(previous_umask)
        for path in (states / "owned-candidate", states / "owned-candidate.json", states / "replay-state-0001.json"):
            mode = stat.S_IMODE(path.stat().st_mode)
            self.assertEqual(mode & 0o077, 0, path)

    def test_fixture_public_patch_replay(self):
        before_revision, before_tree = self.revision, self.tree
        (self.repository / "one.txt").write_text("two\n", encoding="utf-8"); git(self.repository, "add", "one.txt"); git(self.repository, "commit", "-m", "patch fixture")
        after_revision, after_tree = git(self.repository, "rev-parse", "HEAD"), git(self.repository, "write-tree")
        payload = subprocess.run(["git", "-C", str(self.repository), "diff", "--binary", "--full-index", before_revision, after_revision], check=True, capture_output=True).stdout
        fixture = self.root / "fixture.patch"; fixture.write_bytes(payload); git(self.repository, "reset", "--hard", before_revision)
        manifest, materials = self.fixture_manifest(self.root)
        value = json.loads(manifest.read_text()); value["units"][0]["application"] = {"mechanism": "patch", "reference": "fixture:patch"}; manifest.write_text(json.dumps(value))
        digest = "sha256:" + __import__("hashlib").sha256(payload).hexdigest()
        materials.write_text(json.dumps({"schema_version": 1, "materials": {"fixture:patch": {"mechanism":"patch", "patch":"fixture.patch", "expected_patch_sha256":digest, "expected_before_tree":before_tree, "expected_after_tree":after_tree}}}))
        states = self.root / "patch-states"; states.mkdir(); before = REPLAY._repository_snapshot(self.repository.resolve())
        result = subprocess.run([sys.executable,str(SCRIPT),"replay","--repository",str(self.repository),"--revision",before_revision,"--expected-tree",before_tree,"--manifest",str(manifest),"--materials",str(materials),"--materials-root",str(self.root),"--state-directory",str(states),"--apply"],capture_output=True,text=True,check=True)
        self.assertEqual(json.loads(result.stdout)["outcomes"][0]["outcome"], "applied"); self.assertEqual(git(states / "owned-candidate", "write-tree"), after_tree); self.assertEqual(REPLAY._repository_snapshot(self.repository.resolve()), before)

    def test_fixture_public_data_replay(self):
        source = self.root / "fixture.data"; source.write_text("payload\n")
        digest = "sha256:" + __import__("hashlib").sha256(source.read_bytes()).hexdigest()
        (self.repository / "data.txt").write_bytes(source.read_bytes()); git(self.repository,"add","data.txt"); after = git(self.repository,"write-tree"); git(self.repository,"reset","--hard","HEAD")
        manifest, materials = self.fixture_manifest(self.root); value=json.loads(manifest.read_text()); value["units"][0]["application"]={"mechanism":"module/data","reference":"fixture:data"}; manifest.write_text(json.dumps(value))
        materials.write_text(json.dumps({"schema_version":1,"materials":{"fixture:data":{"mechanism":"module/data","source":"fixture.data","destination":"data.txt","expected_sha256":digest,"expected_before_tree":self.tree,"expected_after_tree":after}}}))
        states=self.root/"data-states"; states.mkdir(); before=REPLAY._repository_snapshot(self.repository.resolve())
        result=subprocess.run([sys.executable,str(SCRIPT),"replay","--repository",str(self.repository),"--revision",self.revision,"--expected-tree",self.tree,"--manifest",str(manifest),"--materials",str(materials),"--materials-root",str(self.root),"--state-directory",str(states),"--apply"],text=True,capture_output=True,check=True)
        self.assertEqual(json.loads(result.stdout)["outcomes"][0]["outcome"],"applied"); self.assertEqual(git(states/"owned-candidate","write-tree"),after); self.assertEqual(REPLAY._repository_snapshot(self.repository.resolve()),before)

    def test_verify_is_read_only_and_records_locked_input(self):
        before = git(self.repository, "status", "--porcelain=v1")
        lock = REPLAY.verify(self.repository.resolve(), self.revision, self.tree)
        self.assertEqual(lock["revision"], self.revision)
        self.assertEqual(lock["tree"], self.tree)
        self.assertEqual(git(self.repository, "status", "--porcelain=v1"), before)

    def test_full_object_ids_and_tree_identity_are_required(self):
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.verify(self.repository.resolve(), self.revision[:12], self.tree)
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.verify(self.repository.resolve(), self.revision, "0" * 40)

    def test_dirty_repository_and_replace_mechanism_fail_closed(self):
        (self.repository / "one.txt").write_text("changed\n", encoding="utf-8")
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.verify(self.repository.resolve(), self.revision, self.tree)
        (self.repository / "one.txt").write_text("one\n", encoding="utf-8")
        git(self.repository, "replace", self.revision, self.revision)
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.verify(self.repository.resolve(), self.revision, self.tree)

    def test_plan_requires_new_explicit_external_output(self):
        output = self.root / "output"
        output.mkdir()
        destination = output / "replay-plan.json"
        result = REPLAY.plan(self.repository.resolve(), self.revision, self.tree, destination)
        self.assertEqual(result["replay_state"], "not-created")
        self.assertTrue(destination.exists())
        REPLAY.validate_plan(json.loads(destination.read_text(encoding="utf-8")))
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.plan(self.repository.resolve(), self.revision, self.tree, destination)

    def test_plan_digest_and_schema_shape_fail_closed(self):
        value = {
            "schema_version": 1,
            "tool": "roots-replay",
            "input": {"schema_version": 1, "revision": self.revision, "tree": self.tree, "repository_snapshot": {}},
            "operations": ["verify", "plan"],
            "replay_state": "not-created",
        }
        value["digest"] = REPLAY.digest(value)
        REPLAY.validate_plan(value)
        value["input"]["tree"] = "0" * 40
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.validate_plan(value)

    def test_hostile_paths_and_bare_repositories_are_rejected(self):
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY._directory("relative", "repository")
        bare = self.root / "bare"
        subprocess.run(["git", "init", "--bare", str(bare)], check=True, capture_output=True)
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.verify(bare.resolve(), self.revision, self.tree)

    def test_candidate_commands_require_their_explicit_evidence_inputs(self):
        for command in ("report", "export-patches"):
            result = subprocess.run(
                [sys.executable, str(SCRIPT), command, "--repository", str(self.repository), "--revision", self.revision, "--expected-tree", self.tree],
                text=True,
                capture_output=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("error:", result.stderr)
            self.assertEqual(git(self.repository, "status", "--porcelain=v1"), "")

    def test_replay_and_resume_cli_use_an_owned_ordered_candidate(self):
        units = ROOT / "contrib/roots/adaptation-manifest-29.3.json"
        states = self.root / "states"
        states.mkdir()
        materials = self.materials_for_manifest(self.root)
        before_evidence = caller_evidence(self.repository)
        base = [sys.executable, str(SCRIPT), "replay", "--repository", str(self.repository), "--revision", self.revision, "--expected-tree", self.tree, "--manifest", str(units), "--materials", str(materials), "--materials-root", str(self.root), "--state-directory", str(states), "--apply"]
        result = subprocess.run(base, text=True, capture_output=True, check=True)
        self.assertEqual(len(json.loads(result.stdout)["outcomes"]), 16)
        self.assertTrue((states / "owned-candidate" / ".git").exists())
        self.assertTrue((states / "replay-state-0016.json").exists())
        # A state whose first boundary is retained can be resumed only after
        # re-verifying the independently supplied source lock and units file.
        resumed = subprocess.run([sys.executable, str(SCRIPT), "resume", "--repository", str(self.repository), "--revision", self.revision, "--expected-tree", self.tree, "--manifest", str(units), "--materials", str(materials), "--materials-root", str(self.root), "--state", str(states / "replay-state-0016.json"), "--state-directory", str(states)], text=True, capture_output=True, check=True)
        self.assertEqual(len(json.loads(resumed.stdout)["outcomes"]), 16)
        self.assertEqual(caller_evidence(self.repository), before_evidence)

    def test_public_replay_preflight_is_non_mutating_and_concurrent_apply_fails_closed(self):
        manifest = ROOT / "contrib/roots/adaptation-manifest-29.3.json"
        states = self.root / "concurrent-states"
        states.mkdir()
        materials = self.materials_for_manifest(self.root)
        before_evidence = caller_evidence(self.repository)
        command = [sys.executable, str(SCRIPT), "replay", "--repository", str(self.repository), "--revision", self.revision, "--expected-tree", self.tree, "--manifest", str(manifest), "--materials", str(materials), "--materials-root", str(self.root), "--state-directory", str(states)]
        preflight = subprocess.run(command, text=True, capture_output=True, check=True)
        self.assertEqual(json.loads(preflight.stdout)["mode"], "preflight")
        self.assertFalse((states / "owned-candidate").exists())
        with ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(lambda _: subprocess.run(command + ["--apply"], text=True, capture_output=True), range(2)))
        diagnostics = [(result.returncode, result.stderr) for result in results]
        contents = sorted(path.name for path in states.iterdir())
        self.assertEqual(sum(result.returncode == 0 for result in results), 1, (diagnostics, contents))
        self.assertTrue((states / "owned-candidate").is_dir(), contents)
        self.assertTrue((states / "owned-candidate.json").is_file(), contents)
        self.assertFalse((states / "owned-candidate.lock").exists(), contents)
        state = REPLAY.read_run_state(states / "replay-state-0016.json")
        REPLAY.validate_resume(state, state["input_lock"], state["manifest_digest"], state["candidate_tree"])
        self.assertEqual(git(self.repository, "status", "--porcelain=v1"), "")
        self.assertEqual(caller_evidence(self.repository), before_evidence)

    def test_manifest_rejects_missing_application_material_before_candidate_creation(self):
        manifest = self.root / "adaptation-manifest-29.3.json"
        value = json.loads((ROOT / "contrib/roots/adaptation-manifest-29.3.json").read_text(encoding="utf-8"))
        value["units"][0]["application"]["mechanism"] = "patch"
        manifest.write_text(json.dumps(value), encoding="utf-8")
        states = self.root / "bad-manifest-states"
        states.mkdir()
        result = subprocess.run([sys.executable, str(SCRIPT), "replay", "--repository", str(self.repository), "--revision", self.revision, "--expected-tree", self.tree, "--manifest", str(manifest), "--state-directory", str(states), "--apply"], text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((states / "owned-candidate").exists())

    def test_material_join_rejects_omission_extra_and_stale_fields(self):
        manifest, materials = self.fixture_manifest(self.root)
        units = REPLAY.read_manifest_units(manifest)
        value = json.loads(materials.read_text(encoding="utf-8"))
        value["materials"] = {}
        materials.write_text(json.dumps(value), encoding="utf-8")
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.read_replay_materials(materials, units)
        value["materials"] = {"fixture:manual": {"mechanism": "manual"}, "extra": {"mechanism": "manual"}}
        materials.write_text(json.dumps(value), encoding="utf-8")
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.read_replay_materials(materials, units)
        value["materials"] = {"fixture:manual": {"mechanism": "manual", "stale": True}}
        materials.write_text(json.dumps(value), encoding="utf-8")
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.read_replay_materials(materials, units)

    def test_owned_replay_interrupt_cleans_exact_candidate_only(self):
        states = self.root / "interrupted-states"
        states.mkdir()
        lock = {"schema_version": 1, "revision": self.revision, "tree": self.tree, "repository_snapshot": REPLAY._repository_snapshot(self.repository.resolve())}
        units = [{"id": "roots-first", "mechanism": "manual", "reason": "review", "dependencies": []}]
        with mock.patch.object(REPLAY, "replay_units", side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                REPLAY.replay_owned(self.repository.resolve(), self.revision, self.tree, units, states)
        self.assertFalse((states / "owned-candidate").exists())
        self.assertFalse((states / "owned-candidate.json").exists())
        self.assertEqual(git(self.repository, "status", "--porcelain=v1"), "")

    def test_sigterm_after_owned_candidate_creation_cleans_only_owned_paths(self):
        states = self.root / "signal-replay-states"
        states.mkdir()
        before = caller_evidence(self.repository)
        unit = {"id": "roots-first", "mechanism": "manual", "reason": "review", "dependencies": []}
        worker = f'''import importlib.util, os, signal
spec = importlib.util.spec_from_file_location("replay_worker", {str(SCRIPT)!r})
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
def interrupt(*_args):
    os.kill(os.getpid(), signal.SIGTERM)
module.replay_units = interrupt
module.replay_owned(module.Path({str(self.repository.resolve())!r}), {self.revision!r}, {self.tree!r}, [{unit!r}], module.Path({str(states)!r}))
'''
        result = subprocess.run([sys.executable, "-c", worker], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((states / "owned-candidate").exists())
        self.assertFalse((states / "owned-candidate.json").exists())
        self.assertEqual(caller_evidence(self.repository), before)

    def test_sigterm_during_resume_cleans_only_owned_paths(self):
        states = self.root / "signal-resume-states"
        states.mkdir()
        unit = {"id": "roots-first", "mechanism": "manual", "reason": "review", "dependencies": []}
        REPLAY.replay_owned(self.repository.resolve(), self.revision, self.tree, [unit], states)
        before = caller_evidence(self.repository)
        worker = f'''import importlib.util, os, signal
spec = importlib.util.spec_from_file_location("resume_worker", {str(SCRIPT)!r})
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
def interrupt(*_args):
    os.kill(os.getpid(), signal.SIGTERM)
module.resume_units = interrupt
module.resume_owned(module.Path({str(self.repository.resolve())!r}), {self.revision!r}, {self.tree!r}, [{unit!r}], module.Path({str(states / "replay-state-0001.json")!r}), module.Path({str(states)!r}))
'''
        result = subprocess.run([sys.executable, "-c", worker], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((states / "owned-candidate").exists())
        self.assertFalse((states / "owned-candidate.json").exists())
        self.assertTrue((states / "replay-state-0001.json").exists())
        self.assertEqual(caller_evidence(self.repository), before)

    def test_path_independent_state_and_report_bytes(self):
        lock = {"schema_version": 1, "revision": self.revision, "tree": self.tree, "repository_snapshot": {"branch": "master", "head": self.revision, "index_tree": self.tree, "status": ""}}
        first = REPLAY.make_run_state(lock, "sha256:" + "c" * 64, ["roots-first"], [], self.tree)
        second = REPLAY.make_run_state(dict(lock), "sha256:" + "c" * 64, ["roots-first"], [], self.tree)
        self.assertEqual(REPLAY.canonical_json(first), REPLAY.canonical_json(second))

    def test_resume_rejects_materials_digest_drift(self):
        manifest = ROOT / "contrib/roots/adaptation-manifest-29.3.json"
        states = self.root / "materials-drift"
        states.mkdir()
        materials = self.materials_for_manifest(self.root)
        base = [sys.executable, str(SCRIPT), "replay", "--repository", str(self.repository), "--revision", self.revision, "--expected-tree", self.tree, "--manifest", str(manifest), "--materials", str(materials), "--materials-root", str(self.root), "--state-directory", str(states), "--apply"]
        subprocess.run(base, check=True, capture_output=True, text=True)
        value = json.loads(materials.read_text(encoding="utf-8")); value["materials"][next(iter(value["materials"]))]["extra"] = "drift"; materials.write_text(json.dumps(value), encoding="utf-8")
        result = subprocess.run([sys.executable, str(SCRIPT), "resume", "--repository", str(self.repository), "--revision", self.revision, "--expected-tree", self.tree, "--manifest", str(manifest), "--materials", str(materials), "--materials-root", str(self.root), "--state", str(states / "replay-state-0016.json"), "--state-directory", str(states)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)

    def test_resume_rejects_valid_manifest_drift(self):
        manifest = ROOT / "contrib/roots/adaptation-manifest-29.3.json"
        states = self.root / "manifest-drift"
        states.mkdir()
        materials = self.materials_for_manifest(self.root)
        base = [sys.executable, str(SCRIPT), "replay", "--repository", str(self.repository), "--revision", self.revision, "--expected-tree", self.tree, "--manifest", str(manifest), "--materials", str(materials), "--materials-root", str(self.root), "--state-directory", str(states), "--apply"]
        subprocess.run(base, check=True, capture_output=True, text=True)
        before_source, candidate = REPLAY._repository_snapshot(self.repository.resolve()), states / "owned-candidate"
        before_tree = git(candidate, "write-tree")
        manifest = self.root / "adaptation-manifest-29.3.json"
        value = json.loads((ROOT / "contrib/roots/adaptation-manifest-29.3.json").read_text(encoding="utf-8"))
        value["units"][0]["purpose"] = "Changed but schema-valid purpose"
        manifest.write_text(json.dumps(value), encoding="utf-8")
        self.assertEqual(len(REPLAY.read_manifest_units(manifest)), 16)
        result = subprocess.run([sys.executable, str(SCRIPT), "resume", "--repository", str(self.repository), "--revision", self.revision, "--expected-tree", self.tree, "--manifest", str(manifest), "--materials", str(materials), "--materials-root", str(self.root), "--state", str(states / "replay-state-0016.json"), "--state-directory", str(states)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0); self.assertIn("drifted", result.stderr)
        self.assertEqual(REPLAY._repository_snapshot(self.repository.resolve()), before_source); self.assertEqual(git(candidate, "write-tree"), before_tree)

    def test_stage_candidate_requires_all_guards_and_updates_only_target_branch(self):
        git(self.repository, "branch", "integration/replay")
        git(self.repository, "checkout", "integration/replay")
        expected_head = git(self.repository, "rev-parse", "HEAD")
        (self.repository / "two.txt").write_text("two\n", encoding="utf-8")
        git(self.repository, "add", "two.txt")
        git(self.repository, "commit", "-m", "candidate")
        candidate = git(self.repository, "rev-parse", "HEAD")
        candidate_tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        git(self.repository, "reset", "--hard", expected_head)
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.stage_candidate(self.repository.resolve(), "integration/replay", expected_head, candidate, candidate_tree, "no")
        result = REPLAY.stage_candidate(self.repository.resolve(), "integration/replay", expected_head, candidate, candidate_tree, "I_STAGE_THE_EXACT_CANDIDATE")
        self.assertEqual(result["head"], candidate)
        self.assertEqual(git(self.repository, "rev-parse", "HEAD"), candidate)
        self.assertEqual(git(self.repository, "symbolic-ref", "--short", "HEAD"), "integration/replay")

    def test_stage_candidate_rejects_protected_branch_and_moved_head(self):
        git(self.repository, "branch", "integration/replay")
        git(self.repository, "checkout", "integration/replay")
        expected_head = git(self.repository, "rev-parse", "HEAD")
        candidate_tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.stage_candidate(self.repository.resolve(), "main", expected_head, expected_head, candidate_tree, "I_STAGE_THE_EXACT_CANDIDATE")
        (self.repository / "later.txt").write_text("later\n", encoding="utf-8")
        git(self.repository, "add", "later.txt")
        git(self.repository, "commit", "-m", "move head")
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.stage_candidate(self.repository.resolve(), "integration/replay", expected_head, expected_head, candidate_tree, "I_STAGE_THE_EXACT_CANDIDATE")

    def test_patch_unit_requires_exact_digest_and_tree_boundaries(self):
        before_tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        (self.repository / "one.txt").write_text("two\n", encoding="utf-8")
        patch = self.root / "unit.patch"
        patch.write_text(subprocess.run(["git", "-C", str(self.repository), "diff"], check=True, text=True, capture_output=True).stdout, encoding="utf-8")
        (self.repository / "one.txt").write_text("one\n", encoding="utf-8")
        patch_digest = "sha256:" + __import__("hashlib").sha256(patch.read_bytes()).hexdigest()
        subprocess.run(["git", "-C", str(self.repository), "apply", "--index", str(patch)], check=True)
        after_tree = git(self.repository, "write-tree")
        git(self.repository, "reset", "--hard", "HEAD")
        result = REPLAY.apply_patch_unit(self.repository.resolve(), "roots-test-unit", patch, patch_digest, before_tree, after_tree)
        self.assertEqual(result["outcome"], "applied")
        self.assertEqual(git(self.repository, "write-tree"), after_tree)
        git(self.repository, "commit", "-m", "apply unit")
        self.assertEqual(REPLAY.apply_patch_unit(self.repository.resolve(), "roots-test-unit", patch, patch_digest, before_tree, after_tree)["outcome"], "already-present")
        git(self.repository, "reset", "--hard", "HEAD^")
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.apply_patch_unit(self.repository.resolve(), "roots-test-unit", patch, "sha256:" + "0" * 64, before_tree, after_tree)
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.apply_patch_unit(self.repository.resolve(), "roots-test-unit", patch, patch_digest, before_tree, "0" * 40)
        self.assertEqual(git(self.repository, "rev-parse", "HEAD^{tree}"), before_tree)
        self.assertEqual(git(self.repository, "status", "--porcelain=v1"), "")

    def test_manual_unit_records_boundary_without_candidate_mutation(self):
        before = git(self.repository, "rev-parse", "HEAD")
        self.assertEqual(REPLAY.manual_unit("roots-policy-port", "semantic review required")["outcome"], "manual")
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.manual_unit("Roots/unsafe", "semantic review required")
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.manual_unit("roots-policy-port", "two lines\nare unsafe")
        self.assertEqual(git(self.repository, "rev-parse", "HEAD"), before)

    def test_absorbed_unit_requires_explicit_matching_tree_lock(self):
        tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        self.assertEqual(REPLAY.absorbed_unit(self.repository.resolve(), "roots-upstream-fix", tree, "reviewed upstream equivalence")["outcome"], "absorbed")
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.absorbed_unit(self.repository.resolve(), "roots-upstream-fix", "0" * 40, "reviewed upstream equivalence")

    def test_sequence_requires_unique_dependency_order_before_application(self):
        units = [{"id": "roots-foundation", "dependencies": []}, {"id": "roots-feature", "dependencies": ["roots-foundation"]}]
        self.assertEqual(REPLAY.sequence_units(units), ["roots-foundation", "roots-feature"])
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.sequence_units(list(reversed(units)))
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.sequence_units([units[0], units[0]])

    def test_content_addressed_run_state_rejects_resume_drift(self):
        tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        lock = {"revision": self.revision, "tree": tree}
        manifest = "sha256:" + "1" * 64
        state = REPLAY.make_run_state(lock, manifest, ["roots-foundation"], [{"unit": "roots-foundation", "outcome": "applied", "tree": tree}], tree)
        REPLAY.validate_resume(state, lock, manifest, tree)
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.validate_resume(state, {"revision": "0" * 40, "tree": tree}, manifest, tree)
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.validate_resume(state, lock, "sha256:" + "2" * 64, tree)
        state["completed_units"][0]["outcome"] = "absorbed"
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.validate_resume(state, lock, manifest, tree)
        state = REPLAY.make_run_state(lock, manifest, ["roots-foundation"], [{"unit": "roots-foundation", "outcome": "applied", "tree": tree}], tree)
        directory = self.root / "state"
        directory.mkdir()
        path = directory / "replay-state.json"
        REPLAY.write_run_state(path, state)
        self.assertEqual(REPLAY.read_run_state(path), state)
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.write_run_state(path, state)

    def test_multi_unit_replay_persists_each_manual_boundary(self):
        tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        states = self.root / "states"
        states.mkdir()
        units = [
            {"id": "roots-first", "mechanism": "manual", "reason": "review", "dependencies": []},
            {"id": "roots-second", "mechanism": "manual", "reason": "review", "dependencies": ["roots-first"]},
        ]
        outcomes = REPLAY.replay_units(self.repository.resolve(), units, {"revision": self.revision, "tree": tree}, "sha256:" + "3" * 64, states)
        self.assertEqual([item["outcome"] for item in outcomes], ["manual", "manual"])
        self.assertTrue((states / "replay-state-0001.json").is_file())
        self.assertTrue((states / "replay-state-0002.json").is_file())

    def test_resume_requires_matching_prefix_and_continues_suffix(self):
        tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        states = self.root / "resume-states"
        states.mkdir()
        units = [
            {"id": "roots-first", "mechanism": "manual", "reason": "review", "dependencies": []},
            {"id": "roots-second", "mechanism": "manual", "reason": "review", "dependencies": ["roots-first"]},
        ]
        lock, manifest = {"revision": self.revision, "tree": tree}, "sha256:" + "4" * 64
        initial = REPLAY.make_run_state(lock, manifest, REPLAY.sequence_units(units), [{"unit": "roots-first", "outcome": "manual", "tree": tree}], tree)
        previous = states / "replay-state-0001.json"
        REPLAY.write_run_state(previous, initial)
        outcomes = REPLAY.resume_units(self.repository.resolve(), units, lock, manifest, previous, states)
        self.assertEqual([item["unit"] for item in outcomes], ["roots-first", "roots-second"])
        self.assertTrue((states / "replay-state-0002.json").is_file())
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.resume_units(self.repository.resolve(), units, {"revision": "0" * 40, "tree": tree}, manifest, previous, states)

    def test_resume_failure_resets_and_does_not_publish_next_boundary(self):
        tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        states = self.root / "failed-resume-states"
        states.mkdir()
        units = [
            {"id": "roots-first", "mechanism": "manual", "reason": "review", "dependencies": []},
            {"id": "roots-bad", "mechanism": "unknown", "dependencies": ["roots-first"]},
        ]
        lock, manifest = {"revision": self.revision, "tree": tree}, "sha256:" + "5" * 64
        state = REPLAY.make_run_state(lock, manifest, REPLAY.sequence_units(units), [{"unit": "roots-first", "outcome": "manual", "tree": tree}], tree)
        previous = states / "replay-state-0001.json"
        REPLAY.write_run_state(previous, state)
        before = git(self.repository, "rev-parse", "HEAD")
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.resume_units(self.repository.resolve(), units, lock, manifest, previous, states)
        self.assertEqual(git(self.repository, "rev-parse", "HEAD"), before)
        self.assertFalse((states / "replay-state-0002.json").exists())

    def test_replay_failure_after_applied_unit_resets_to_initial_tree(self):
        before_tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        (self.repository / "one.txt").write_text("replayed\n", encoding="utf-8")
        patch = self.root / "sequence.patch"
        patch.write_text(subprocess.run(["git", "-C", str(self.repository), "diff"], check=True, text=True, capture_output=True).stdout, encoding="utf-8")
        (self.repository / "one.txt").write_text("one\n", encoding="utf-8")
        patch_digest = "sha256:" + __import__("hashlib").sha256(patch.read_bytes()).hexdigest()
        subprocess.run(["git", "-C", str(self.repository), "apply", "--index", str(patch)], check=True)
        after_tree = git(self.repository, "write-tree")
        git(self.repository, "reset", "--hard", "HEAD")
        states = self.root / "sequence-failure-states"
        states.mkdir()
        units = [
            {"id": "roots-first", "mechanism": "patch", "patch": str(patch), "expected_patch_sha256": patch_digest, "expected_before_tree": before_tree, "expected_after_tree": after_tree, "dependencies": []},
            {"id": "roots-second", "mechanism": "unknown", "dependencies": ["roots-first"]},
        ]
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.replay_units(self.repository.resolve(), units, {"revision": self.revision, "tree": before_tree}, "sha256:" + "6" * 64, states)
        self.assertEqual(git(self.repository, "rev-parse", "HEAD^{tree}"), before_tree)
        self.assertEqual(git(self.repository, "status", "--porcelain=v1"), "")
        self.assertTrue((states / "replay-state-0001.json").is_file())
        self.assertFalse((states / "replay-state-0002.json").exists())

    def test_inspect_and_abandon_redact_paths_and_retain_state(self):
        tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        directory = self.root / "inspect"
        directory.mkdir()
        state = REPLAY.make_run_state({"repository": "/private/path", "revision": self.revision, "tree": tree}, "sha256:" + "7" * 64, ["roots-first"], [{"unit": "roots-first", "outcome": "manual", "tree": tree}], tree)
        path = directory / "replay-state.json"
        REPLAY.write_run_state(path, state)
        summary = REPLAY.inspect_run(path)
        self.assertNotIn("repository", json.dumps(summary))
        marker = REPLAY.abandon_run(path, directory / "replay-abandoned.json")
        self.assertEqual(marker["action"], "abandoned")
        self.assertTrue(path.exists())
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.abandon_run(path, directory / "replay-abandoned.json")

    def test_concurrent_state_writers_have_one_durable_winner(self):
        tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        directory = self.root / "concurrent"
        directory.mkdir()
        state = REPLAY.make_run_state({"revision": self.revision, "tree": tree}, "sha256:" + "8" * 64, ["roots-first"], [], tree)
        path = directory / "replay-state.json"
        def write_once():
            try:
                REPLAY.write_run_state(path, state)
                return True
            except REPLAY.ReplayError:
                return False
        with ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(lambda _: write_once(), range(2)))
        self.assertEqual(results.count(True), 1)
        self.assertEqual(REPLAY.read_run_state(path), state)

    def test_resume_rejects_tool_environment_and_candidate_tree_drift(self):
        directory = self.root / "resume-drift"
        directory.mkdir()
        state = REPLAY.make_run_state({"revision": self.revision, "tree": self.tree}, "sha256:" + "9" * 64, ["roots-first"], [], self.tree)
        state["environment"] = {"LC_ALL": "en_US.UTF-8"}
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.validate_resume(state, {"revision": self.revision, "tree": self.tree}, "sha256:" + "9" * 64, self.tree)
        state = REPLAY.make_run_state({"revision": self.revision, "tree": self.tree}, "sha256:" + "9" * 64, ["roots-first"], [], self.tree)
        path = directory / "replay-state.json"
        REPLAY.write_run_state(path, state)
        (self.repository / "drift.txt").write_text("drift\\n", encoding="utf-8")
        git(self.repository, "add", "drift.txt")
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.resume_units(self.repository.resolve(), [{"id": "roots-first", "mechanism": "manual", "reason": "review", "dependencies": []}], state["input_lock"], state["manifest_digest"], path, directory)

    def test_review_bundle_is_stable_and_redacts_repository_path(self):
        directory = self.root / "review"
        directory.mkdir()
        state = REPLAY.make_run_state({"repository": "/private/input", "revision": self.revision, "tree": self.tree}, "sha256:" + "a" * 64, ["roots-first"], [{"unit": "roots-first", "outcome": "manual", "tree": self.tree}], self.tree)
        path = directory / "replay-state.json"
        REPLAY.write_run_state(path, state)
        output = self.root / "bundle"
        output.mkdir()
        result = REPLAY.review_bundle(path, output)
        payload = (output / "replay-review.json").read_bytes()
        self.assertEqual(result["json"], "replay-review.json")
        self.assertNotIn(b"/private/input", payload)
        self.assertEqual(payload, (output / "replay-review.json").read_bytes())
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.review_bundle(path, output)

    def test_generated_patch_export_is_tree_locked(self):
        (self.repository / "two.txt").write_text("two\\n", encoding="utf-8")
        git(self.repository, "add", "two.txt")
        git(self.repository, "commit", "-m", "candidate")
        candidate_tree = git(self.repository, "write-tree")
        directory = self.root / "export"
        directory.mkdir()
        state = REPLAY.make_run_state({"revision": self.revision, "tree": self.tree}, "sha256:" + "b" * 64, ["roots-first"], [{"unit": "roots-first", "outcome": "applied", "tree": candidate_tree}], candidate_tree)
        path = directory / "replay-state.json"
        REPLAY.write_run_state(path, state)
        result = REPLAY.export_patch_series(self.repository.resolve(), path, directory / "replay-generated-series.patch")
        payload = (directory / "replay-generated-series.patch").read_bytes()
        self.assertIn(b"[ROOTS-REPLAY-GENERATED]", payload)
        self.assertTrue(result["sha256"].startswith("sha256:"))
        clone = self.root / "round-trip"
        subprocess.run(["git", "clone", str(self.repository), str(clone)], check=True, capture_output=True)
        git(clone, "reset", "--hard", self.revision)
        git(clone, "config", "user.email", "test@example.invalid")
        git(clone, "config", "user.name", "Replay test")
        subprocess.run(["git", "-C", str(clone), "am", str(directory / "replay-generated-series.patch")], check=True, capture_output=True)
        self.assertEqual(git(clone, "write-tree"), candidate_tree)

    def test_data_unit_requires_content_and_tree_locks(self):
        before_tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        source = self.root / "data-input"
        source.write_text("payload\n", encoding="utf-8")
        source_digest = "sha256:" + __import__("hashlib").sha256(source.read_bytes()).hexdigest()
        (self.repository / "data.txt").write_bytes(source.read_bytes())
        git(self.repository, "add", "data.txt")
        after_tree = git(self.repository, "write-tree")
        git(self.repository, "reset", "--hard", "HEAD")
        result = REPLAY.apply_data_unit(self.repository.resolve(), "roots-data-unit", source, "data.txt", source_digest, before_tree, after_tree)
        self.assertEqual(result["tree"], after_tree)
        git(self.repository, "reset", "--hard", "HEAD")
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.apply_data_unit(self.repository.resolve(), "roots-data-unit", source, "../escape", source_digest, before_tree, after_tree)

    def test_allowlisted_generator_and_typed_dispatch_are_tree_locked(self):
        before_tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        (self.repository / "input.txt").write_bytes(b"one\r\ntwo\r\n")
        git(self.repository, "add", "input.txt")
        git(self.repository, "commit", "-m", "generator input")
        before_tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        source_bytes = (self.repository / "input.txt").read_bytes()
        output_bytes = source_bytes.replace(b"\r\n", b"\n")
        (self.repository / "output.txt").write_bytes(output_bytes)
        git(self.repository, "add", "output.txt")
        after_tree = git(self.repository, "write-tree")
        git(self.repository, "reset", "--hard", "HEAD")
        unit = {
            "id": "roots-generated-output", "mechanism": "generator", "generator": "normalize-lf", "source": "input.txt", "destination": "output.txt",
            "expected_source_sha256": "sha256:" + __import__("hashlib").sha256(source_bytes).hexdigest(),
            "expected_output_sha256": "sha256:" + __import__("hashlib").sha256(output_bytes).hexdigest(),
            "expected_before_tree": before_tree, "expected_after_tree": after_tree,
        }
        self.assertEqual(REPLAY.apply_typed_unit(self.repository.resolve(), unit)["outcome"], "applied")
        self.assertEqual(git(self.repository, "write-tree"), after_tree)
        unit["generator"] = "untrusted-command"
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.apply_typed_unit(self.repository.resolve(), unit)

    def test_commit_unit_uses_locked_single_parent_binary_diff(self):
        before = git(self.repository, "rev-parse", "HEAD")
        before_tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        (self.repository / "one.txt").write_text("commit payload\n", encoding="utf-8")
        git(self.repository, "add", "one.txt")
        git(self.repository, "commit", "-m", "candidate unit")
        commit = git(self.repository, "rev-parse", "HEAD")
        after_tree = git(self.repository, "rev-parse", "HEAD^{tree}")
        patch_digest = "sha256:" + __import__("hashlib").sha256(REPLAY._git_bytes(self.repository.resolve(), "diff", "--binary", "--full-index", before, commit)).hexdigest()
        git(self.repository, "reset", "--hard", before)
        result = REPLAY.apply_commit_unit(self.repository.resolve(), "roots-commit-unit", commit, patch_digest, before_tree, after_tree)
        self.assertEqual(result["tree"], after_tree)
        git(self.repository, "reset", "--hard", before)
        with self.assertRaises(REPLAY.ReplayError):
            REPLAY.apply_commit_unit(self.repository.resolve(), "roots-commit-unit", commit, "sha256:" + "0" * 64, before_tree, after_tree)
        self.assertEqual(git(self.repository, "rev-parse", "HEAD"), before)


if __name__ == "__main__":
    unittest.main()
