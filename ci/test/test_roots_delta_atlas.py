#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Regression tests for deterministic delta-atlas input normalization."""

from __future__ import annotations

import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "contrib/devtools/roots-delta-atlas.py"
LEDGER = ROOT / "contrib/roots/lineage-ledger.json"


def load_module():
    specification = importlib.util.spec_from_file_location("roots_delta_atlas", SCRIPT)
    assert specification and specification.loader
    module = importlib.util.module_from_spec(specification)
    sys.modules[specification.name] = module
    specification.loader.exec_module(module)
    return module


ATLAS = load_module()


class RootsDeltaAtlasTest(unittest.TestCase):
    def run_tool(self, *arguments, check=True):
        return subprocess.run([sys.executable, SCRIPT, *arguments], cwd=ROOT, text=True,
                              capture_output=True, check=check)

    def write_snapshot(self, root: Path, contents="alpha\n", target="payload.txt"):
        root.mkdir()
        (root / "payload.txt").write_text(contents, encoding="utf-8", newline="")
        (root / "link").symlink_to(target)

    def init_git(self, root: Path):
        subprocess.run(["git", "init", root], check=True, capture_output=True)
        subprocess.run(["git", "-C", root, "config", "user.name", "Roots test"], check=True)
        subprocess.run(["git", "-C", root, "config", "user.email", "roots@test.invalid"], check=True)
        (root / "payload.txt").write_text("alpha\n", encoding="utf-8")
        (root / "link").symlink_to("payload.txt")
        subprocess.run(["git", "-C", root, "add", "payload.txt", "link"], check=True)
        subprocess.run(["git", "-C", root, "commit", "-m", "fixture"], check=True, capture_output=True)

    def commit(self, repository: Path, message: str):
        subprocess.run(["git", "-C", repository, "add", "-A"], check=True)
        subprocess.run(["git", "-C", repository, "commit", "-m", message], check=True, capture_output=True)
        return subprocess.run(["git", "-C", repository, "rev-parse", "HEAD"], text=True,
                              capture_output=True, check=True).stdout.strip()

    def test_snapshot_mode_is_explicitly_unavailable_and_content_is_stable(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            temporary = Path(temporary_dir)
            first, second = temporary / "first", temporary / "second"
            self.write_snapshot(first)
            self.write_snapshot(second)
            (second / "payload.txt").chmod(stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)
            first_manifest, second_manifest = ATLAS.snapshot_manifest(first), ATLAS.snapshot_manifest(second)
            self.assertEqual(first_manifest, second_manifest)
            self.assertEqual(first_manifest["mode_status"], "unavailable")
            result = ATLAS.compare(first_manifest, second_manifest)
            self.assertEqual(result["channels"], {"content": "same", "path_type": "same", "symlink_target": "same"})

    def test_snapshot_channel_mutations_are_precise(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            temporary = Path(temporary_dir)
            base, changed = temporary / "base", temporary / "changed"
            self.write_snapshot(base)
            self.write_snapshot(changed, contents="beta\n", target="other.txt")
            result = ATLAS.compare(ATLAS.snapshot_manifest(base), ATLAS.snapshot_manifest(changed))
            self.assertEqual(result["channels"]["content"], "different")
            self.assertEqual(result["channels"]["symlink_target"], "different")
            (changed / "link").unlink()
            (changed / "link").mkdir()
            result = ATLAS.compare(ATLAS.snapshot_manifest(base), ATLAS.snapshot_manifest(changed))
            self.assertEqual(result["channels"]["path_type"], "different")

    def test_git_manifest_reports_authoritative_executable_and_symlink_channels(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            repository = Path(temporary_dir) / "repo"
            self.init_git(repository)
            before = ATLAS.git_manifest(repository, "HEAD")
            (repository / "payload.txt").chmod(stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)
            (repository / "link").unlink()
            (repository / "link").symlink_to("other.txt")
            subprocess.run(["git", "-C", repository, "add", "payload.txt", "link"], check=True)
            subprocess.run(["git", "-C", repository, "commit", "-m", "changed"], check=True, capture_output=True)
            after = ATLAS.git_manifest(repository, "HEAD")
            result = ATLAS.compare(before, after)
            self.assertEqual(before["mode_status"], "authoritative")
            self.assertEqual(result["channels"]["executable_bit"], "different")
            self.assertEqual(result["channels"]["symlink_target"], "different")

    def make_archive(self, path: Path, order: list[tuple[str, bytes]]):
        with tarfile.open(path, "w") as archive:
            for name, contents in order:
                member = tarfile.TarInfo(name)
                member.size = len(contents)
                archive.addfile(member, io.BytesIO(contents))

    def test_archive_order_and_raw_line_endings_are_observable(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            temporary = Path(temporary_dir)
            first, reordered, newline_changed = temporary / "first.tar", temporary / "reordered.tar", temporary / "newline.tar"
            self.make_archive(first, [("a.txt", b"one\n"), ("b.txt", b"two\n")])
            self.make_archive(reordered, [("b.txt", b"two\n"), ("a.txt", b"one\n")])
            self.make_archive(newline_changed, [("a.txt", b"one\r\n"), ("b.txt", b"two\n")])
            self.assertEqual(ATLAS.compare(ATLAS.archive_manifest(first), ATLAS.archive_manifest(reordered))["channels"]["archive_member_order"], "different")
            self.assertEqual(ATLAS.compare(ATLAS.archive_manifest(first), ATLAS.archive_manifest(newline_changed))["channels"]["content"], "different")

    def test_input_lock_is_byte_stable_and_rejects_changed_snapshot(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            temporary = Path(temporary_dir)
            core_293, core_294 = temporary / "core-293", temporary / "core-294"
            self.write_snapshot(core_293, "29.3\n")
            self.write_snapshot(core_294, "29.4\n")
            first, second = temporary / "first.json", temporary / "second.json"
            # CLI paths are intentionally relative; execute from the fixture directory.
            relative_inputs = ("--snapshot", "core-29.3-snapshot=core-293", "--snapshot", "core-29.4-snapshot=core-294")
            ledger = temporary / "ledger.json"
            ledger.write_bytes(LEDGER.read_bytes())
            result = subprocess.run([sys.executable, SCRIPT, "lock-inputs", "--ledger", "ledger.json", *relative_inputs, "--output", "first.json"], cwd=temporary, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([sys.executable, SCRIPT, "lock-inputs", "--ledger", "ledger.json", *relative_inputs, "--output", "second.json"], cwd=temporary, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            verified = subprocess.run([sys.executable, SCRIPT, "verify-input-lock", "first.json", "--ledger", "ledger.json", *relative_inputs], cwd=temporary, text=True, capture_output=True)
            self.assertEqual(verified.returncode, 0, verified.stderr)
            (core_294 / "payload.txt").write_text("changed\n", encoding="utf-8")
            stale = subprocess.run([sys.executable, SCRIPT, "verify-input-lock", "first.json", "--ledger", "ledger.json", *relative_inputs], cwd=temporary, text=True, capture_output=True)
            self.assertNotEqual(stale.returncode, 0)
            self.assertIn("stale input lock", stale.stderr)
            renamed = subprocess.run([sys.executable, SCRIPT, "lock-inputs", "--ledger", "ledger.json", "--snapshot", "renamed=core-293", "--snapshot", "core-29.4-snapshot=core-294", "--output", "renamed.json"], cwd=temporary, text=True, capture_output=True)
            self.assertNotEqual(renamed.returncode, 0)
            self.assertIn("legacy lock requires", renamed.stderr)

    def test_input_lock_accepts_caller_selected_releases_and_reorganized_snapshots(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            temporary = Path(temporary_dir)
            before, after = temporary / "before-major", temporary / "after-major"
            self.write_snapshot(before, "before\n")
            self.write_snapshot(after, "after\n")
            ledger = temporary / "ledger.json"
            ledger.write_text(json.dumps({"releases": [
                {"id": "core-major", "peeled_commit": "sha1:" + "1" * 40, "tree": "sha1:" + "2" * 40, "tag_ref": "refs/tags/core-major"},
                {"id": "roots-next", "peeled_commit": "sha1:" + "3" * 40, "tree": "sha1:" + "4" * 40, "tag_ref": "refs/tags/roots-next"},
            ]}), encoding="utf-8")
            original_directory = Path.cwd()
            os.chdir(temporary)
            try:
                lock = ATLAS.input_lock(
                    ledger,
                    ["before-reorganized=before-major", "after-reorganized=after-major"],
                    ["core-major", "roots-next"],
                )
            finally:
                os.chdir(original_directory)
            self.assertEqual(set(lock["git_inputs"]), {"core-major", "roots-next"})
            self.assertEqual([item["id"] for item in lock["snapshot_inputs"]], ["after-reorganized", "before-reorganized"])
            os.chdir(temporary)
            try:
                with self.assertRaises(ATLAS.AtlasError):
                    ATLAS.input_lock(ledger, ["before-reorganized=before-major"], ["missing-release"])
                with self.assertRaises(ATLAS.AtlasError):
                    ATLAS.input_lock(ledger, ["before-reorganized=before-major"], ["core-major", "core-major"])
                for required_releases in ([], [""], [{"core-major": "invalid"}], [["core-major"]]):
                    with self.assertRaises(ATLAS.AtlasError):
                        ATLAS.input_lock(
                            ledger,
                            ["before-reorganized=before-major"],
                            required_releases,
                        )
            finally:
                os.chdir(original_directory)

    def test_unsafe_paths_and_duplicate_archive_members_fail(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            archive = Path(temporary_dir) / "unsafe.tar"
            self.make_archive(archive, [("../escape", b"no")])
            with self.assertRaises(ATLAS.AtlasError):
                ATLAS.archive_manifest(archive)
            duplicate = Path(temporary_dir) / "duplicate.tar"
            self.make_archive(duplicate, [("same.txt", b"one"), ("same.txt", b"two")])
            with self.assertRaises(ATLAS.AtlasError):
                ATLAS.archive_manifest(duplicate)

    def test_path_case_collisions_fail_before_comparison(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            root = Path(temporary_dir) / "case-collision"
            root.mkdir()
            (root / "File.txt").write_text("one", encoding="utf-8")
            (root / "file.txt").write_text("two", encoding="utf-8")
            with self.assertRaises(ATLAS.AtlasError):
                ATLAS.snapshot_manifest(root)

    def test_changed_records_accounts_for_added_and_deleted_paths(self):
        before = {"records": [{"path": "deleted.txt", "sha256": "sha256:before", "type": "file"}, {"path": "kept.txt", "sha256": "sha256:same", "type": "file"}]}
        after = {"records": [{"path": "added.txt", "sha256": "sha256:after", "type": "file"}, {"path": "kept.txt", "sha256": "sha256:same", "type": "file"}]}
        self.assertEqual(
            [(item["path"], item["status"]) for item in ATLAS.changed_records(before, after)],
            [("added.txt", "added"), ("deleted.txt", "deleted")],
        )

    def test_input_assignment_cannot_escape_the_working_directory(self):
        with self.assertRaises(ATLAS.AtlasError):
            ATLAS.parse_assignment("core-29.3-snapshot=../outside")

    def test_layer_partition_composes_ordered_deltas_and_records_overlap(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            repository = Path(temporary_dir) / "repo"
            subprocess.run(["git", "init", repository], check=True, capture_output=True)
            subprocess.run(["git", "-C", repository, "config", "user.name", "Roots test"], check=True)
            subprocess.run(["git", "-C", repository, "config", "user.email", "roots@test.invalid"], check=True)
            (repository / "shared.txt").write_text("core\n", encoding="utf-8")
            core = self.commit(repository, "core")
            (repository / "shared.txt").write_text("knots\n", encoding="utf-8")
            (repository / "knots.txt").write_text("inherited\n", encoding="utf-8")
            parent = self.commit(repository, "knots")
            (repository / "shared.txt").write_text("roots-first\n", encoding="utf-8")
            (repository / "roots.txt").write_text("first\n", encoding="utf-8")
            first = self.commit(repository, "first roots")
            (repository / "shared.txt").write_text("knots\n", encoding="utf-8")
            (repository / "roots.txt").write_text("later\n", encoding="utf-8")
            target = self.commit(repository, "later roots")
            ledger = Path(temporary_dir) / "ledger.json"
            ledger.write_text(json.dumps({"releases": [{"id": "core-29.3", "peeled_commit": f"sha1:{core}"}, {"id": "roots-29.3-roots.1", "peeled_commit": f"sha1:{target}"}]}), encoding="utf-8")
            result = ATLAS.layer_partition(repository, ledger, first, parent)
            self.assertEqual(result["unexplained_direct_paths"], [])
            self.assertEqual([layer["id"] for layer in result["layers"]], ["core_to_knots", "knots_to_first_roots", "first_roots_to_target"])
            self.assertIn({"layers": ["core_to_knots", "knots_to_first_roots", "first_roots_to_target"], "path": "shared.txt"}, result["overlapping_ownership"])
            self.assertIn({"layers": ["knots_to_first_roots", "first_roots_to_target"], "path": "shared.txt", "relation": "reverted"}, result["first_roots_later_relations"])
            ledger.write_text(json.dumps({"releases": [{"id": "base-major", "peeled_commit": f"sha1:{core}"}, {"id": "target-major", "peeled_commit": f"sha1:{target}"}]}), encoding="utf-8")
            generalized = ATLAS.layer_partition(repository, ledger, first, parent, "base-major", "target-major")
            self.assertEqual(generalized["inputs"]["core"], core)
            self.assertEqual(generalized["inputs"]["roots_target"], target)
            ledger.write_text(json.dumps({"releases": [{"id": "core-29.3", "peeled_commit": f"sha1:{core}"}, {"id": "roots-29.3-roots.1", "peeled_commit": f"sha1:{target}"}]}), encoding="utf-8")
            report = Path(temporary_dir) / "partition.json"
            report.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n", encoding="utf-8")
            verified = subprocess.run([sys.executable, SCRIPT, "verify-layer-partition", "partition.json", "--repository", "repo", "--ledger", "ledger.json", "--knots-parent", parent, "--first-roots", first], cwd=temporary_dir, text=True, capture_output=True)
            self.assertEqual(verified.returncode, 0, verified.stderr)
            (repository / "unlocked.txt").write_text("moved\n", encoding="utf-8")
            moved = self.commit(repository, "moved target")
            ledger.write_text(json.dumps({"releases": [{"id": "core-29.3", "peeled_commit": f"sha1:{core}"}, {"id": "roots-29.3-roots.1", "peeled_commit": f"sha1:{moved}"}]}), encoding="utf-8")
            stale = subprocess.run([sys.executable, SCRIPT, "verify-layer-partition", "partition.json", "--repository", "repo", "--ledger", "ledger.json", "--knots-parent", parent, "--first-roots", first], cwd=temporary_dir, text=True, capture_output=True)
            self.assertNotEqual(stale.returncode, 0)
            self.assertIn("stale layer partition", stale.stderr)

    def test_layer_partition_rejects_a_non_parent_first_roots_anchor(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            repository = Path(temporary_dir) / "repo"
            self.init_git(repository)
            core = subprocess.run(["git", "-C", repository, "rev-parse", "HEAD"], text=True, capture_output=True, check=True).stdout.strip()
            (repository / "second.txt").write_text("second\n", encoding="utf-8")
            parent = self.commit(repository, "parent")
            (repository / "third.txt").write_text("third\n", encoding="utf-8")
            target = self.commit(repository, "target")
            ledger = Path(temporary_dir) / "ledger.json"
            ledger.write_text(json.dumps({"releases": [{"id": "core-29.3", "peeled_commit": f"sha1:{core}"}, {"id": "roots-29.3-roots.1", "peeled_commit": f"sha1:{target}"}]}), encoding="utf-8")
            with self.assertRaises(ATLAS.AtlasError):
                ATLAS.layer_partition(repository, ledger, target, core)

    def test_inventory_metadata_distinguishes_text_generated_subtree_and_binary(self):
        text = ATLAS.inventory_metadata("src/example.cpp", b"@@ -1 +1 @@\n+class Example {};\n")
        self.assertFalse(text["binary"])
        self.assertEqual(text["hunk_count"], 1)
        self.assertIn("Example", text["symbols"])
        generated = ATLAS.inventory_metadata("doc/man/bitcoind.1", b"binary")
        self.assertEqual(generated["generated_status"], "generated")
        subtree = ATLAS.inventory_metadata("src/leveldb/db.cc", b"@@\n")
        self.assertEqual(subtree["subtree"], "src/leveldb")
        binary = ATLAS.inventory_metadata("share/qt/icon.png", b"\x89PNG")
        self.assertTrue(binary["binary"])

    def test_classification_covers_each_record_and_queues_sensitive_areas(self):
        inventory = {"schema_version": 1, "layers": [{"id": "core_to_knots", "record_count": 2, "records": [{"path": "src/policy/policy.cpp"}, {"path": "doc/man/bitcoind.1"}]}]}
        result = ATLAS.classify_inventory(inventory)
        self.assertEqual(len(result["records"]), 2)
        self.assertEqual(result["records"][0]["classification"]["risk"], "critical")
        self.assertEqual(result["records"][1]["classification"]["primary_purpose"], "generated-output")
        self.assertEqual(result["review_queues"]["critical"], ["src/policy/policy.cpp"])
        inventory["layers"][0]["record_count"] = 1
        with self.assertRaises(ATLAS.AtlasError):
            ATLAS.classify_inventory(inventory)

    def test_atlas_report_requires_complete_fresh_classification(self):
        lock = {"schema_version": 1}
        partition = {"schema_version": 1, "layers": [{"id": "core_to_knots", "record_count": 1}], "direct_core_to_roots": {"records": [{"path": "src/policy/a.cpp"}]}, "unexplained_direct_paths": []}
        inventory = {"schema_version": 1, "layers": [{"id": "core_to_knots", "record_count": 1, "records": [{"path": "src/policy/a.cpp"}]}]}
        classification = ATLAS.classify_inventory(inventory)
        report = ATLAS.publish_atlas(lock, partition, inventory, classification)
        self.assertEqual(report["coverage"]["direct_paths"], 1)
        classification["input_inventory_digest"] = "sha256:" + "0" * 64
        with self.assertRaises(ATLAS.AtlasError):
            ATLAS.publish_atlas(lock, partition, inventory, classification)

    def test_checked_in_atlas_schemas_bind_version_one(self):
        for name in ("atlas-input-lock", "atlas-layer-partition", "atlas-granularity", "atlas-classification", "atlas-report"):
            schema = json.loads((ROOT / "contrib/roots" / f"{name}.schema.json").read_text(encoding="utf-8"))
            self.assertEqual(schema["properties"]["schema_version"]["const"], 1)


if __name__ == "__main__":
    unittest.main()
