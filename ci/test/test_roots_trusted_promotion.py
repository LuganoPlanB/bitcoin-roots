#!/usr/bin/env python3
"""Adversarial contracts for the isolated, read-only promotion control plane."""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "ci/roots-trusted-promotion.py"
WORKFLOW = ROOT / ".github/workflows/roots-trusted-replay.yml"
spec = importlib.util.spec_from_file_location("trusted_promotion", SCRIPT)
PROMOTION = importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(PROMOTION)


class TrustedPromotionTest(unittest.TestCase):
    @staticmethod
    def job(workflow: str, name: str) -> str:
        match = re.search(rf"(?ms)^  {re.escape(name)}:\n(.*?)(?=^  [a-z0-9_-]+:\n|\Z)", workflow)
        if match is None:
            raise AssertionError(f"workflow job {name!r} is missing")
        return match.group(1)

    @staticmethod
    def named_step_run(job: str, name: str) -> str:
        match = re.search(rf"(?ms)^      - name: {re.escape(name)}\n(.*?)(?=^      - |\Z)", job)
        if match is None:
            raise AssertionError(f"workflow step {name!r} is missing")
        run = re.search(r"(?ms)^        run: (.*?)(?=^        [a-zA-Z0-9_-]+: |\Z)", match.group(1))
        if run is None:
            raise AssertionError(f"workflow step {name!r} has no run command")
        return run.group(1).strip()

    def invoke(self, candidate: Path, output: Path, **changes: str) -> subprocess.CompletedProcess[str]:
        control_sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        values = {"candidate_commit": PROMOTION.CANDIDATE_COMMIT, "candidate_tree": PROMOTION.CANDIDATE_TREE, "control_sha": control_sha}
        values.update(changes)
        return subprocess.run(["python3", SCRIPT, "--candidate-commit", values["candidate_commit"], "--candidate-tree", values["candidate_tree"], "--control-sha", values["control_sha"], "--candidate-repository", candidate, "--trusted-root", ROOT, "--output", output], text=True, capture_output=True, check=False)

    def test_workflow_isolated_from_candidate_workflows_tokens_and_cache(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        for required in ("promotion-inputs:", "operation:", "options: [replay, promote]", "control_sha:", "ref: '${{ github.sha }}'", "persist-credentials: false", "credential.helper=", "GIT_TERMINAL_PROMPT=0", "refs/heads/integration/roots-29.4", "ls-remote --refs origin", "test \"$after\" = \"$before\"", "roots-trusted-candidate-build.py", "ccache -s"):
            self.assertIn(required, workflow)
        for forbidden in ("pull_request", "pull_request_target", "contents: write", "id-token: write", "secrets.", "actions/cache", "git push", "git tag", "create-release", "publish-release", "promotion_digest", "roots-trusted-promotion.yml"):
            self.assertNotIn(forbidden, workflow)
        for script in re.findall(r"run: \|\n(.*?)(?=      - |\Z)", workflow, re.DOTALL):
            self.assertNotIn("${{ inputs.", script)
        promotion = workflow.split("\n  promotion:\n", 1)[1]
        self.assertEqual(promotion.count("python3 ci/roots-trusted-promotion.py"), 1)
        self.assertEqual(promotion.count("python3 ci/roots-trusted-candidate-build.py"), 1)
        prerequisite_step = "Install supported headless build prerequisites"
        build_install = self.named_step_run(self.job(workflow, "build-test"), prerequisite_step)
        promotion_install = self.named_step_run(self.job(workflow, "promotion"), prerequisite_step)
        self.assertEqual(build_install, promotion_install)
        self.assertIn("HEADLESS_BUILD_PACKAGES", build_install)

    def test_exact_current_frozen_candidate_is_accepted_twice_without_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / "a", root / "b"
            candidate = root / "candidate"
            subprocess.run(["git", "clone", "--no-local", "--no-checkout", ROOT, candidate], check=True, capture_output=True)
            subprocess.run(["git", "-C", candidate, "fetch", "--no-tags", ROOT, PROMOTION.CANDIDATE_COMMIT], check=True, capture_output=True)
            subprocess.run(["git", "-C", candidate, "checkout", "--detach", PROMOTION.CANDIDATE_COMMIT], check=True, capture_output=True)
            subprocess.run(["git", "-C", candidate, "update-ref", "refs/remotes/origin/integration/roots-29.4", PROMOTION.CANDIDATE_COMMIT], check=True, capture_output=True)
            before = subprocess.check_output(["git", "status", "--porcelain=v1"], cwd=ROOT, text=True)
            for output in (first, second):
                output.mkdir()
                result = self.invoke(candidate, output / "trusted-promotion-report.json")
                self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((first / "trusted-promotion-report.json").read_bytes(), (second / "trusted-promotion-report.json").read_bytes())
            report = json.loads((first / "trusted-promotion-report.json").read_text(encoding="utf-8"))
            self.assertEqual(report["candidate_commit"], PROMOTION.CANDIDATE_COMMIT)
            self.assertEqual(report["candidate_tree"], PROMOTION.CANDIDATE_TREE)
            self.assertEqual(report["control_commit"], subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip())
            self.assertEqual(report["control_tree"], subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"], cwd=ROOT, text=True).strip())
            self.assertEqual(before, subprocess.check_output(["git", "status", "--porcelain=v1"], cwd=ROOT, text=True))

    def test_untrusted_inputs_and_unsafe_paths_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "trusted-promotion-report.json"
            cases = (
                {"candidate_commit": "0" * 40},
                {"candidate_tree": "0" * 40},
                {"candidate_commit": "not-an-oid"},
                {"candidate_commit": PROMOTION.G2_COMMIT},
                {"candidate_tree": PROMOTION.G2_TREE},
            )
            for changed in cases:
                with self.subTest(changed=changed):
                    values = {"candidate_commit": PROMOTION.CANDIDATE_COMMIT, "candidate_tree": PROMOTION.CANDIDATE_TREE, "control_sha": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()}
                    values.update(changed)
                    with self.assertRaises(PROMOTION.PromotionError):
                        PROMOTION.validate_inputs(**values)
            output.write_text("occupied", encoding="utf-8")
            self.assertNotEqual(self.invoke(ROOT, output).returncode, 0)
            output.unlink()
            output.symlink_to(ROOT / "README.md")
            self.assertNotEqual(self.invoke(ROOT, output).returncode, 0)
            output.unlink()
            link = root / "link"
            link.symlink_to(ROOT, target_is_directory=True)
            self.assertNotEqual(self.invoke(link, output).returncode, 0)

    def test_exact_g3_evidence_and_rejected_g2_history_are_immutable(self):
        with tempfile.TemporaryDirectory() as directory:
            trusted = Path(directory) / "trusted"
            for relative in (
                PROMOTION.PORTABILITY_WORKFLOW,
                PROMOTION.G2_FREEZE,
                *PROMOTION.G3_EVIDENCE_DIGESTS,
            ):
                source, target = ROOT / relative, trusted / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
            PROMOTION.verify_g3_evidence(trusted)

            mutations = (
                (PROMOTION.PORTABILITY_WORKFLOW, lambda value: value.replace(PROMOTION.ACCEPTED_BOOTSTRAP, PROMOTION.REJECTED_BOOTSTRAP)),
                ("contrib/roots/frozen-production-29.4-g3.json", lambda value: value.replace(PROMOTION.CANDIDATE_TREE, PROMOTION.G2_TREE)),
                ("contrib/roots/post-candidate-replay-29.4-g3.json", lambda value: value.replace(PROMOTION.G2_COMMIT, "0" * 40, 1)),
                ("contrib/roots/production-accounting-29.4-g3.json", lambda value: value.replace("published-rejected-portability", "accepted", 1)),
                ("contrib/roots/acceptance-evidence-29.4-g3.json", lambda value: value.replace("35340807632", "35340807633", 1)),
            )
            for relative, mutate in mutations:
                with self.subTest(relative=relative):
                    path = trusted / relative
                    saved = path.read_text(encoding="utf-8")
                    path.write_text(mutate(saved), encoding="utf-8")
                    with self.assertRaises(PROMOTION.PromotionError):
                        PROMOTION.verify_g3_evidence(trusted)
                    path.write_text(saved, encoding="utf-8")

            evidence = trusted / "contrib/roots/acceptance-evidence-29.4-g3.json"
            saved = evidence.read_bytes()
            evidence.unlink()
            evidence.symlink_to(ROOT / "contrib/roots/acceptance-evidence-29.4-g3.json")
            with self.assertRaises(PROMOTION.PromotionError):
                PROMOTION.verify_g3_evidence(trusted)
            evidence.unlink()
            evidence.write_bytes(saved)

    def test_malformed_candidate_syntax_is_rejected_before_git(self):
        with mock.patch.object(PROMOTION, "git") as mocked_git:
            with self.assertRaises(PROMOTION.PromotionError):
                PROMOTION.report("not-an-oid", PROMOTION.CANDIDATE_TREE, "0" * 40, ROOT, ROOT)
        mocked_git.assert_not_called()

    def test_race_validator_change_and_secret_environment_are_rejected(self):
        with mock.patch.object(PROMOTION, "git", side_effect=["false", ".git", PROMOTION.CANDIDATE_COMMIT, PROMOTION.CANDIDATE_TREE]):
            with self.assertRaises(PROMOTION.PromotionError):
                PROMOTION.validate_candidate_repository(ROOT, ROOT.parent, PROMOTION.CANDIDATE_COMMIT, PROMOTION.CANDIDATE_TREE)
        with mock.patch.object(PROMOTION, "git", return_value="0" * 40):
            with self.assertRaises(PROMOTION.PromotionError):
                PROMOTION.verify_trusted_controls(ROOT, subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip())
        with mock.patch.dict(os.environ, {"GITHUB_TOKEN": "token", "TOP_SECRET": "secret", "AWS_ACCESS_KEY_ID": "key"}, clear=False):
            environment = PROMOTION.candidate_environment()
        self.assertNotIn("GITHUB_TOKEN", environment)
        self.assertNotIn("TOP_SECRET", environment)
        self.assertNotIn("AWS_ACCESS_KEY_ID", environment)

    def test_integration_ref_race_and_hostile_candidate_tree_are_rejected(self):
        with mock.patch.object(PROMOTION, "git", side_effect=["0" * 40, PROMOTION.CANDIDATE_TREE]):
            with self.assertRaises(PROMOTION.PromotionError):
                PROMOTION.validate_integration_ref(ROOT, PROMOTION.CANDIDATE_COMMIT, PROMOTION.CANDIDATE_TREE)
        malformed = mock.Mock(returncode=0, stdout=b"100644 blob " + b"a" * 40 + b" 1\t../escape\0")
        with mock.patch.object(PROMOTION.subprocess, "run", return_value=malformed):
            with self.assertRaises(PROMOTION.PromotionError):
                PROMOTION.validate_candidate_tree(ROOT, PROMOTION.CANDIDATE_COMMIT)
        gitlink = mock.Mock(returncode=0, stdout=b"160000 commit " + b"a" * 40 + b" -\tgitlink\0")
        with mock.patch.object(PROMOTION.subprocess, "run", return_value=gitlink):
            with self.assertRaises(PROMOTION.PromotionError):
                PROMOTION.validate_candidate_tree(ROOT, PROMOTION.CANDIDATE_COMMIT)
        unsafe_link = mock.Mock(returncode=0, stdout=b"120000 blob " + b"a" * 40 + b" 3\tlink\0")
        with mock.patch.object(PROMOTION.subprocess, "run", return_value=unsafe_link), mock.patch.object(PROMOTION, "git", return_value="../escape"):
            with self.assertRaises(PROMOTION.PromotionError):
                PROMOTION.validate_candidate_tree(ROOT, PROMOTION.CANDIDATE_COMMIT)


if __name__ == "__main__":
    unittest.main()
