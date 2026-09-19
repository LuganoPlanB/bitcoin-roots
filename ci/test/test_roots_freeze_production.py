"""Adversarial checks for the frozen private production identity."""

from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "contrib/devtools/roots-freeze-production.py"
RECORD = ROOT / "contrib/roots/frozen-production-29.4.json"


class FreezeProductionTest(unittest.TestCase):
    def run_tool(self, command: str, output: Path, root: Path = ROOT, repository: Path = ROOT, reference: str | None = None) -> subprocess.CompletedProcess[str]:
        arguments = [sys.executable, str(TOOL), command, "--repository", str(repository), "--root", str(root), "--output", str(output)]
        if reference is not None:
            arguments.extend(["--ref", reference])
        return subprocess.run(
            arguments,
            capture_output=True,
            check=False,
            text=True,
        )

    def generated_record(self, directory: Path) -> dict[str, object]:
        output = directory / "record.json"
        result = self.run_tool("build", output)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(output.read_text(encoding="utf-8"))

    def assert_rejected(self, record: dict[str, object], directory: Path, error: str) -> None:
        output = directory / "record.json"
        output.write_text(json.dumps(record), encoding="utf-8")
        result = self.run_tool("verify", output)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(error, result.stderr)

    def fresh_fixture(self, directory: Path) -> Path:
        fixture = directory / "fixture"
        subprocess.run(["git", "clone", "--local", str(ROOT), str(fixture)], check=True, capture_output=True)
        for relative in (
            "contrib/roots/post-candidate-inventory-29.4.json",
            "contrib/roots/post-candidate-replay-29.4.json",
            "contrib/roots/post-candidate-invariants-29.4.json",
            "contrib/roots/frozen-production-29.4.json",
        ):
            destination = fixture / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / relative, destination)
        return fixture

    def test_canonical_regeneration_is_byte_identical(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT / "contrib/roots") as temporary:
            directory = Path(temporary)
            first = directory / "first.json"
            second = directory / "second.json"
            self.assertEqual(self.run_tool("build", first).returncode, 0)
            self.assertEqual(self.run_tool("build", second).returncode, 0)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(self.run_tool("verify", RECORD).returncode, 0)

    def test_every_leaf_value_substitution_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT / "contrib/roots") as temporary:
            directory = Path(temporary)
            record = self.generated_record(directory)
            cases = [
                ("approvals", lambda value: value["approvals"].append("other"), "freeze field approvals differs"),
                ("candidate", lambda value: value.__setitem__("candidate_commit", "sha1:" + "0" * 40), "freeze field candidate_commit differs"),
                ("configuration inventory", lambda value: value["configuration_digests"].__setitem__("post-candidate-inventory-29.4.json", "sha256:" + "0" * 64), "freeze field configuration_digests.post-candidate-inventory-29.4.json differs"),
                ("configuration invariants", lambda value: value["configuration_digests"].__setitem__("post-candidate-invariants-29.4.json", "sha256:" + "0" * 64), "freeze field configuration_digests.post-candidate-invariants-29.4.json differs"),
                ("configuration replay", lambda value: value["configuration_digests"].__setitem__("post-candidate-replay-29.4.json", "sha256:" + "0" * 64), "freeze field configuration_digests.post-candidate-replay-29.4.json differs"),
                ("final", lambda value: value.__setitem__("final_commit", "sha1:" + "0" * 40), "freeze field final_commit differs"),
                ("git version", lambda value: value.__setitem__("git_version", "git version 0"), "freeze field git_version differs"),
                ("invariants", lambda value: value.__setitem__("invariants", "sha256:" + "0" * 64), "freeze field invariants differs"),
                ("limitations", lambda value: value["limitations"].append("other"), "freeze field limitations differs"),
                ("link inventory", lambda value: value["links"].__setitem__("inventory", "contrib/roots/other.json"), "freeze field links.inventory differs"),
                ("link invariants", lambda value: value["links"].__setitem__("invariants", "contrib/roots/other.json"), "freeze field links.invariants differs"),
                ("link replay", lambda value: value["links"].__setitem__("replay", "contrib/roots/other.json"), "freeze field links.replay differs"),
                ("outcome inventory", lambda value: value["outcomes"].__setitem__("inventory", "sha256:" + "0" * 64), "freeze field outcomes.inventory differs"),
                ("outcome replay", lambda value: value["outcomes"].__setitem__("replay", "sha256:" + "0" * 64), "freeze field outcomes.replay differs"),
                ("patch", lambda value: value.__setitem__("patch_series_sha256", "sha256:" + "0" * 64), "freeze field patch_series_sha256 differs"),
                ("provenance core", lambda value: value["provenance"].__setitem__("core_commit", "sha1:" + "0" * 40), "freeze field provenance.core_commit differs"),
                ("provenance inventory", lambda value: value["provenance"].__setitem__("inventory", "sha256:" + "0" * 64), "freeze field provenance.inventory differs"),
                ("provenance replay", lambda value: value["provenance"].__setitem__("replay", "sha256:" + "0" * 64), "freeze field provenance.replay differs"),
                ("publication", lambda value: value.__setitem__("publication_authorized", True), "freeze field publication_authorized differs"),
                ("range", lambda value: value.__setitem__("range_diff_sha256", "sha256:" + "0" * 64), "freeze field range_diff_sha256 differs"),
                ("tree", lambda value: value.__setitem__("result_tree", "sha1:" + "0" * 40), "freeze field result_tree differs"),
                ("scoped", lambda value: value.__setitem__("scoped_diff_sha256", "sha256:" + "0" * 64), "freeze field scoped_diff_sha256 differs"),
                ("tests", lambda value: value["tests"].append("other"), "freeze field tests differs"),
            ]
            for name, mutate, error in cases:
                with self.subTest(name=name):
                    changed = copy.deepcopy(record)
                    mutate(changed)
                    self.assert_rejected(changed, directory, error)

    def test_missing_extra_stale_unsafe_symlink_and_overwrite_reject(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT / "contrib/roots") as temporary:
            directory = Path(temporary)
            record = self.generated_record(directory)
            cases = (
                ("missing top", lambda value: value.pop("tests"), "freeze schema keys invalid"),
                ("extra top", lambda value: value.__setitem__("extra", True), "freeze schema keys invalid"),
                ("missing nested", lambda value: value["links"].pop("inventory"), "freeze links schema invalid"),
                ("extra nested", lambda value: value["links"].__setitem__("extra", "x"), "freeze links schema invalid"),
                ("unsafe link", lambda value: value["links"].__setitem__("inventory", "../escape"), "freeze link inventory unsafe"),
                ("stale", lambda value: value["outcomes"].__setitem__("inventory", "sha256:" + "0" * 64), "freeze field outcomes.inventory differs"),
            )
            for name, mutate, error in cases:
                with self.subTest(name=name):
                    changed = copy.deepcopy(record)
                    mutate(changed)
                    self.assert_rejected(changed, directory, error)
            existing = directory / "existing.json"
            existing.write_text("present", encoding="utf-8")
            overwrite = self.run_tool("build", existing)
            self.assertNotEqual(overwrite.returncode, 0)
            self.assertIn("refusing overwrite", overwrite.stderr)
            symlink = directory / "output-link.json"
            symlink.symlink_to(existing)
            linked_output = self.run_tool("build", symlink)
            self.assertNotEqual(linked_output.returncode, 0)
            self.assertIn("freeze output symlink rejected", linked_output.stderr)

    def test_component_symlink_and_escape_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT / "contrib/roots") as temporary:
            directory = Path(temporary)
            root_link = directory / "root-link"
            root_link.symlink_to(ROOT, target_is_directory=True)
            output = root_link / "contrib/roots/new.json"
            self.assertNotEqual(self.run_tool("build", output, root=root_link, repository=root_link).returncode, 0)
            escaped = Path(tempfile.gettempdir()) / "roots-freeze-escaped.json"
            self.assertNotEqual(self.run_tool("build", escaped).returncode, 0)

    def test_expected_material_symlink_is_rejected_in_fresh_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.fresh_fixture(Path(temporary))
            material_directory = fixture / "contrib/roots"
            expected_input = material_directory / "post-candidate-inventory-29.4.json"
            expected_input.unlink()
            expected_input.symlink_to(material_directory / "post-candidate-replay-29.4.json")
            result = self.run_tool("build", material_directory / "frozen.json", root=fixture, repository=fixture)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("freeze inventory input symlink rejected", result.stderr)

    def test_anchor_creates_exact_private_ref_in_fresh_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.fresh_fixture(Path(temporary))
            record = fixture / "contrib/roots/frozen-production-29.4.json"
            result = self.run_tool("anchor", record, root=fixture, repository=fixture)
            self.assertEqual(result.returncode, 0, result.stderr)
            final_commit = json.loads(record.read_text(encoding="utf-8"))["final_commit"][5:]
            anchored = subprocess.check_output(["git", "-C", str(fixture), "rev-parse", "refs/roots/29.4/frozen-production-g2^{commit}"], text=True).strip()
            tree = subprocess.check_output(["git", "-C", str(fixture), "rev-parse", "refs/roots/29.4/frozen-production-g2^{tree}"], text=True).strip()
            self.assertEqual(anchored, final_commit)
            self.assertEqual(tree, str(json.loads(record.read_text(encoding="utf-8"))["result_tree"])[5:])

    def test_anchor_rejects_invalid_or_existing_ref_without_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.fresh_fixture(Path(temporary))
            record = fixture / "contrib/roots/frozen-production-29.4.json"
            invalid = self.run_tool("anchor", record, root=fixture, repository=fixture, reference="refs/heads/not-owned")
            self.assertNotEqual(invalid.returncode, 0)
            self.assertIn("freeze anchor ref is not an owned private ref", invalid.stderr)
            self.assertEqual(self.run_tool("anchor", record, root=fixture, repository=fixture).returncode, 0)
            existing = self.run_tool("anchor", record, root=fixture, repository=fixture)
            self.assertNotEqual(existing.returncode, 0)
            self.assertIn("freeze anchor ref already exists or cannot be created", existing.stderr)

    def test_concurrent_anchor_allows_exactly_one_creator(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            fixture = self.fresh_fixture(Path(temporary))
            record = fixture / "contrib/roots/frozen-production-29.4.json"
            command = [sys.executable, str(TOOL), "anchor", "--repository", str(fixture), "--root", str(fixture), "--output", str(record)]
            first = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            second = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            _, first_error = first.communicate()
            _, second_error = second.communicate()
            self.assertEqual(sorted((first.returncode, second.returncode)), [0, 1])
            failure = first_error if first.returncode else second_error
            self.assertIn("freeze anchor ref already exists or cannot be created", failure)

    def test_hostile_git_configuration_does_not_change_record_bytes(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT / "contrib/roots") as temporary:
            directory = Path(temporary)
            baseline = directory / "baseline.json"
            hostile = directory / "hostile.json"
            self.assertEqual(self.run_tool("build", baseline).returncode, 0)
            environment = dict(os.environ)
            environment.update({
                "GIT_CONFIG_COUNT": "7",
                "GIT_CONFIG_KEY_0": "diff.external",
                "GIT_CONFIG_VALUE_0": "false",
                "GIT_CONFIG_KEY_1": "format.subjectPrefix",
                "GIT_CONFIG_VALUE_1": "HOSTILE",
                "GIT_CONFIG_KEY_2": "core.pager",
                "GIT_CONFIG_VALUE_2": "false",
                "GIT_CONFIG_KEY_3": "core.abbrev",
                "GIT_CONFIG_VALUE_3": "7",
                "GIT_CONFIG_KEY_4": "diff.algorithm",
                "GIT_CONFIG_VALUE_4": "histogram",
                "GIT_CONFIG_KEY_5": "diff.renames",
                "GIT_CONFIG_VALUE_5": "true",
                "GIT_CONFIG_KEY_6": "diff.indentHeuristic",
                "GIT_CONFIG_VALUE_6": "true",
            })
            result = subprocess.run(
                [sys.executable, str(TOOL), "build", "--repository", str(ROOT), "--root", str(ROOT), "--output", str(hostile)],
                capture_output=True,
                check=False,
                env=environment,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(baseline.read_bytes(), hostile.read_bytes())

    def test_final_commit_object_parent_and_tree_are_locked(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT / "contrib/roots") as temporary:
            record = self.generated_record(Path(temporary))
            final = str(record["final_commit"])[5:]
            object_type = subprocess.check_output(["git", "cat-file", "-t", final], text=True).strip()
            parent = subprocess.check_output(["git", "show", "-s", "--format=%P", final], text=True).strip()
            tree = subprocess.check_output(["git", "rev-parse", final + "^{tree}"], text=True).strip()
            self.assertEqual(object_type, "commit")
            self.assertEqual(parent, str(record["candidate_commit"])[5:])
            self.assertEqual(tree, str(record["result_tree"])[5:])


if __name__ == "__main__":
    unittest.main()
