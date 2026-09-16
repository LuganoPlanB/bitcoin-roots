#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Focused release-evidence binding and fail-closed output tests."""

import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "ci/release/roots-release-evidence.py"
INPUTS = (
    ROOT / "contrib/roots/lineage-ledger.json",
    ROOT / "contrib/roots/adaptation-manifest-29.3.json",
    ROOT / "contrib/roots/replay-29.4-proposal/acceptance-evidence.json",
)


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


class ReleaseEvidenceTest(unittest.TestCase):
    def test_replay_acceptance_schema_rejects_mutations(self):
        cases = (
            ("extra_top", lambda value: value.__setitem__("extra", True)),
            ("unapproved", lambda value: value["approval"].__setitem__("status", "unapproved")),
            ("wrong_approval_tree", lambda value: value["approval"].__setitem__("candidate_tree", "sha1:" + "0" * 40)),
            ("waived_gate", lambda value: value["gates"][0].__setitem__("waiver", True)),
            ("bad_platform", lambda value: value["platform_and_behavior"].__setitem__("status", "fail")),
        )
        for name, mutate in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                source_root = self.source_root(directory)
                replay = source_root / "contrib/roots/replay-29.4-proposal/acceptance-evidence.json"
                value = json.loads(replay.read_text(encoding="utf-8"))
                mutate(value)
                value.pop("digest")
                value["digest"] = "sha256:" + hashlib.sha256(canonical_json(value)).hexdigest()
                replay.write_text(json.dumps(value), encoding="utf-8")
                self.commit_source(source_root)
                result = self.generate(directory, *INPUTS, source_root=source_root)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("roots-release-evidence:", result.stderr)
                self.assertNotIn("Traceback", result.stderr)

    def test_canonical_source_symlinks_are_rejected(self):
        for parent in (False, True):
            with self.subTest(parent=parent), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                source_root = self.source_root(directory)
                target = source_root / "contrib/roots"
                if parent:
                    external = directory / "external"
                    target.rename(external)
                    target.symlink_to(external, target_is_directory=True)
                else:
                    target = target / "lineage-ledger.json"
                    data = target.read_bytes()
                    target.unlink()
                    external = directory / "external.json"
                    external.write_bytes(data)
                    target.symlink_to(external)
                self.assertNotEqual(self.generate(directory, *INPUTS, source_root=source_root).returncode, 0)
    def test_failures_do_not_change_tags(self):
        before = subprocess.run(["git", "-C", ROOT, "for-each-ref", "--format=%(refname)%00%(objectname)", "refs/tags"], check=True, capture_output=True).stdout
        with tempfile.TemporaryDirectory() as temporary:
            result = self.generate(temporary, *INPUTS, candidate_tree="invalid")
            self.assertNotEqual(result.returncode, 0)
        after = subprocess.run(["git", "-C", ROOT, "for-each-ref", "--format=%(refname)%00%(objectname)", "refs/tags"], check=True, capture_output=True).stdout
        self.assertEqual(before, after)
    def test_authoritative_sources_reject_semantic_substitution(self):
        cases = (
            ("ledger", "contrib/roots/lineage-ledger.json", lambda value: value.__setitem__("exceptions", [{"id": "semantic"}])),
            ("manifest", "contrib/roots/adaptation-manifest-29.3.json", lambda value: value.__setitem__("units", list(reversed(value["units"])))),
            ("replay_base", "contrib/roots/replay-29.4-proposal/acceptance-evidence.json", lambda value: value["candidate"].__setitem__("base_tree", "sha1:" + "0" * 40)),
            ("replay_tree", "contrib/roots/replay-29.4-proposal/acceptance-evidence.json", lambda value: value["candidate"].__setitem__("tree", "sha1:" + "0" * 40)),
            ("replay_limitations", "contrib/roots/replay-29.4-proposal/acceptance-evidence.json", lambda value: value.__setitem__("limitations", [])),
        )
        for name, relative, mutate in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                source_root = self.source_root(directory)
                revision = self.revision(source_root)
                result = self.generate(directory, *INPUTS, source_root=source_root)
                self.assertEqual(result.returncode, 0, result.stderr)
                evidence = directory / "roots-release-evidence.json"
                path = source_root / relative
                value = json.loads(path.read_text())
                mutate(value)
                if relative.endswith("acceptance-evidence.json"):
                    value.pop("digest")
                    value["digest"] = "sha256:" + hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
                path.write_text(json.dumps(value))
                verify = subprocess.run(
                    [
                        sys.executable, SCRIPT, "--source-repository", source_root,
                        "--source-revision", revision, "--ledger", INPUTS[0],
                        "--manifest", INPUTS[1], "--replay-result", INPUTS[2],
                        "--candidate-tree",
                        json.loads(evidence.read_text())["release_source_tree"],
                        "--output", evidence, "--verify",
                    ],
                    text=True,
                    capture_output=True,
                )
                self.assertNotEqual(verify.returncode, 0)
                self.assertIn("canonical input differs from source revision", verify.stderr)
    def test_verify_rejects_self_digest_correct_nested_mutations(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            result = self.generate(directory, *INPUTS)
            self.assertEqual(result.returncode, 0, result.stderr)
            baseline = json.loads((directory / "roots-release-evidence.json").read_text())
            revision = self.revision(ROOT)
            for field, replacement in (("build_matrix", "arbitrary"), ("manual_approvals", [123]), ("exceptions", {"fabricated": True}), ("outcome_counts", {"pass": -1}), ("reproducibility", "claimed"), ("invariants", [None])):
                value = dict(baseline)
                value[field] = replacement
                value.pop("digest")
                value["digest"] = "sha256:" + hashlib.sha256(canonical_json(value)).hexdigest()
                path = directory / "roots-release-evidence.json"
                path.write_text(json.dumps(value))
                verify = subprocess.run(
                    [
                        sys.executable, SCRIPT, "--source-repository", ROOT,
                        "--source-revision", revision, "--ledger", INPUTS[0],
                        "--manifest", INPUTS[1], "--replay-result", INPUTS[2],
                        "--candidate-tree", baseline["release_source_tree"],
                        "--output", path, "--verify",
                    ],
                    text=True,
                    capture_output=True,
                )
                self.assertNotEqual(verify.returncode, 0)
                self.assertIn("release evidence", verify.stderr)
    def source_root(self, directory):
        root = Path(directory) / "source"
        for source, relative in zip(INPUTS, ("contrib/roots/lineage-ledger.json", "contrib/roots/adaptation-manifest-29.3.json", "contrib/roots/replay-29.4-proposal/acceptance-evidence.json")):
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        subprocess.run(["git", "init", "--quiet", root], check=True)
        subprocess.run(["git", "-C", root, "config", "user.email", "test@example.invalid"], check=True)
        subprocess.run(["git", "-C", root, "config", "user.name", "Test"], check=True)
        subprocess.run(["git", "-C", root, "add", "contrib"], check=True)
        subprocess.run(["git", "-C", root, "commit", "--quiet", "-m", "canonical inputs"], check=True)
        return root

    def commit_source(self, root, message="mutate canonical input"):
        subprocess.run(["git", "-C", root, "add", "contrib"], check=True)
        subprocess.run(["git", "-C", root, "commit", "--quiet", "-m", message], check=True)

    def revision(self, repository):
        return subprocess.run(
            ["git", "-C", repository, "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()

    def generate(self, directory, *inputs, candidate_tree=None, output="roots-release-evidence.json", source_root=ROOT):
        revision = self.revision(source_root)
        command = [
            sys.executable, SCRIPT, "--ledger", inputs[0], "--manifest", inputs[1],
            "--replay-result", inputs[2], "--source-repository", source_root,
            "--source-revision", revision, "--output", output,
        ]
        if candidate_tree is not None:
            command.extend(("--candidate-tree", candidate_tree))
        return subprocess.run(command, cwd=directory, text=True, capture_output=True)

    def test_malformed_replay_shapes_fail_without_traceback(self):
        cases = (
            ("gates_string", lambda value: value.__setitem__("gates", "invalid")),
            ("fresh_trees_integer", lambda value: value["reproducibility"].__setitem__("fresh_replay_trees", 2)),
            ("platform_mapping", lambda value: value["platform_and_behavior"].__setitem__("supported_platforms", [{}])),
            ("candidate_base_commit", lambda value: value["candidate"].__setitem__("base_commit", "invalid")),
            ("candidate_tree_digest", lambda value: value["candidate"].__setitem__("tree_digest", "invalid")),
            ("duplicate_gate", lambda value: value["gates"].append(dict(value["gates"][0]))),
            ("approval_reviewer", lambda value: value["approval"].__setitem__("reviewer", [])),
            ("platform_count", lambda value: value["platform_and_behavior"].__setitem__("generated_outputs", "one")),
        )
        for name, mutate in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                source_root = self.source_root(directory)
                replay_path = source_root / "contrib/roots/replay-29.4-proposal/acceptance-evidence.json"
                replay = json.loads(replay_path.read_text(encoding="utf-8"))
                mutate(replay)
                replay.pop("digest")
                replay["digest"] = "sha256:" + hashlib.sha256(canonical_json(replay)).hexdigest()
                replay_path.write_text(json.dumps(replay), encoding="utf-8")
                self.commit_source(source_root)
                result = self.generate(directory, *INPUTS, source_root=source_root)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("roots-release-evidence:", result.stderr)
                self.assertNotIn("Traceback", result.stderr)

    def test_trusted_provenance_validators_reject_nested_mutations(self):
        cases = (
            (
                "ledger_claims",
                "contrib/roots/lineage-ledger.json",
                lambda value: value.__setitem__("claims", [{"arbitrary": "claim"}]),
            ),
            (
                "manifest_units",
                "contrib/roots/adaptation-manifest-29.3.json",
                lambda value: value.__setitem__("units", [{"arbitrary": "unit"}]),
            ),
        )
        for name, relative, mutate in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                source_root = self.source_root(directory)
                source_path = source_root / relative
                value = json.loads(source_path.read_text(encoding="utf-8"))
                mutate(value)
                source_path.write_text(json.dumps(value), encoding="utf-8")
                self.commit_source(source_root)
                result = self.generate(directory, *INPUTS, source_root=source_root)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("trusted provenance validation failed", result.stderr)

    def test_cli_is_deterministic_and_bound_to_inputs(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            first_result = self.generate(first, *INPUTS)
            second_result = self.generate(second, *INPUTS)
            self.assertEqual(first_result.returncode, 0, first_result.stderr)
            self.assertEqual(second_result.returncode, 0, second_result.stderr)
            self.assertEqual((Path(first) / "roots-release-evidence.json").read_bytes(), (Path(second) / "roots-release-evidence.json").read_bytes())
            self.assertNotEqual(self.generate(first, *INPUTS).returncode, 0)

    def test_authoritative_verify_accepts_matching_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            result = self.generate(directory, *INPUTS)
            self.assertEqual(result.returncode, 0, result.stderr)
            evidence = directory / "roots-release-evidence.json"
            baseline = json.loads(evidence.read_text(encoding="utf-8"))
            verify = subprocess.run(
                [
                    sys.executable, SCRIPT, "--source-repository", ROOT,
                    "--source-revision", self.revision(ROOT), "--ledger", INPUTS[0],
                    "--manifest", INPUTS[1], "--replay-result", INPUTS[2],
                    "--candidate-tree", baseline["release_source_tree"],
                    "--output", evidence, "--verify",
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(verify.returncode, 0, verify.stderr)

    def test_verify_rejects_malformed_json_without_traceback(self):
        cases = (
            ("top_level_list", b"[{}]"),
            ("nested_list", b'{"artifact_links": []}'),
            ("invalid_utf8", b"\xff"),
        )
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            generated = self.generate(directory, *INPUTS)
            self.assertEqual(generated.returncode, 0, generated.stderr)
            evidence = directory / "roots-release-evidence.json"
            baseline = json.loads(evidence.read_text(encoding="utf-8"))
            for name, content in cases:
                with self.subTest(name=name):
                    evidence.write_bytes(content)
                    verify = subprocess.run(
                        [
                            sys.executable, SCRIPT, "--source-repository", ROOT,
                            "--source-revision", self.revision(ROOT), "--ledger",
                            INPUTS[0], "--manifest", INPUTS[1], "--replay-result",
                            INPUTS[2], "--candidate-tree",
                            baseline["release_source_tree"], "--output", evidence,
                            "--verify",
                        ],
                        text=True,
                        capture_output=True,
                    )
                    self.assertNotEqual(verify.returncode, 0)
                    self.assertIn("roots-release-evidence:", verify.stderr)
                    self.assertNotIn("Traceback", verify.stderr)

    def test_rejects_substituted_or_incomplete_inputs_and_unsafe_output(self):
        paths = (
            "contrib/roots/lineage-ledger.json",
            "contrib/roots/adaptation-manifest-29.3.json",
            "contrib/roots/replay-29.4-proposal/acceptance-evidence.json",
        )
        for position, source in enumerate(INPUTS):
            with self.subTest(source=source.name), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                source_root = self.source_root(directory)
                replacement = source_root / paths[position]
                value = json.loads(source.read_text(encoding="utf-8"))
                if position == 0:
                    value["schema_version"] = 2
                elif position == 1:
                    value["unexpected"] = True
                else:
                    value["candidate"]["tree"] = "sha1:" + "0" * 40
                    value.pop("digest")
                    value["digest"] = "sha256:" + hashlib.sha256(canonical_json(value)).hexdigest()
                replacement.write_text(json.dumps(value), encoding="utf-8")
                self.commit_source(source_root)
                result = self.generate(directory, *INPUTS, source_root=source_root)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("roots-release-evidence:", result.stderr)

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for output in ("../roots-release-evidence.json", "unexpected.json"):
                result = self.generate(directory, *INPUTS, output=output)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("unsafe evidence output path", result.stderr)
            (directory / "roots-release-evidence.json").symlink_to("target")
            result = self.generate(directory, *INPUTS)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("refusing to replace existing evidence output", result.stderr)

    def test_current_release_tree_is_distinct_from_replay_candidate(self):
        with tempfile.TemporaryDirectory() as temporary:
            tree = subprocess.run(
                ["git", "-C", ROOT, "rev-parse", "HEAD^{tree}"],
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            result = self.generate(temporary, *INPUTS, candidate_tree="sha1:" + tree)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
