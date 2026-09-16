#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Execute the fail-closed Roots maintainer procedure in disposable clones."""

from __future__ import annotations

import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
RUNBOOK = ROOT / "contrib/roots/maintainer-runbook.md"
REVIEW = ROOT / "contrib/roots/maintainer-runbook-review.json"
REPLAY = ROOT / "contrib/devtools/roots-replay.py"
METHODOLOGY = ROOT / "contrib/roots/methodology-v1.json"
TRUSTED_GATE = ROOT / "ci/roots-trusted-replay-gate.py"
ACCOUNTING_TOOL = ROOT / "contrib/devtools/roots-continuous-accounting.py"
RELEASE_EVIDENCE = ROOT / "ci/release/roots-release-evidence.py"
PREPARE_RELEASE = ROOT / "ci/release/prepare-release.sh"
PUBLIC_KEY = ROOT / "contrib/release/bitcoin-roots-release-key.asc"

CORE_29_3_COMMIT = "99003bed87333f1be51bf3070235591b3a72f007"
CORE_29_3_TREE = "d7910bd5e9335128932f1f848a767d773895c4a4"
KNOTS_29_3_COMMIT = "99ee26e9df0e63a5d1e0ab6bd46b1862ce67648b"
KNOTS_STAGE_TREE = "56f97d3a9199c1fb191e1b8a21f2caae3901b7f6"
ROOTS_29_3_TREE = "a5708dcbf1d2611360fab68fc6a8e504db1ba95d"
CORE_29_4_COMMIT = "3fc0865963a38b871e9f7d94e6151c4953563516"
CORE_29_4_TREE = "38ad59b187f59647eb90ad1347bc481485ef4d01"
CANONICAL_29_4_COMMIT = "cbc88cff9b35b95a549c0313e424e13093fcd6a1"
CANONICAL_29_4_TREE = "39a5e30207a09962e78ae81c24cc65b1e478ef90"
INCREMENTAL_29_4_COMMIT = "3e29908f7a0131a71309e80a78fe865ec8a50f76"
INCREMENTAL_29_4_TREE = "c676e8944470cc74fcc213e7368aed359ad8ae55"
VALIDATION_REF = "refs/remotes/origin/ci/l7-validation/39a5e302"


def run(command, *, cwd=None, check=True, env=None):
    result = subprocess.run(
        [str(item) for item in command],
        cwd=cwd,
        check=False,
        text=True,
        capture_output=True,
        env=env,
    )
    if check and result.returncode:
        raise AssertionError(
            f"command failed ({result.returncode}): {' '.join(str(item) for item in command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def git(repository, *arguments):
    return run(["git", "-C", repository, *arguments]).stdout.strip()


def sha256(path):
    return "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def load_accounting():
    spec = importlib.util.spec_from_file_location("runbook_accounting", ACCOUNTING_TOOL)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ACCOUNTING = load_accounting()


class MaintainerRunbookTest(unittest.TestCase):
    maxDiff = None

    @unittest.skipUnless(shutil.which("gpg"), "gpg is unavailable")
    def test_disposable_signed_core_tag_verifies(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            home = root / "gpg"
            repository = root / "core"
            home.mkdir(mode=0o700)
            environment = {**os.environ, "GNUPGHOME": str(home)}
            run(
                ["gpg", "--batch", "--passphrase", "", "--quick-generate-key", "rehearsal@example.invalid", "default", "default", "never"],
                env=environment,
            )
            key = run(
                ["gpg", "--batch", "--with-colons", "--list-secret-keys", "rehearsal@example.invalid"],
                env=environment,
            ).stdout.splitlines()[0].split(":")[4]
            run(["git", "init", "--quiet", repository])
            for name, value in (("user.email", "rehearsal@example.invalid"), ("user.name", "Rehearsal"), ("user.signingkey", key)):
                git(repository, "config", name, value)
            (repository / "README").write_text("Core rehearsal\n", encoding="utf-8")
            git(repository, "add", "README")
            git(repository, "commit", "--quiet", "-m", "Core base")
            run(["git", "-C", repository, "tag", "-s", "-m", "v29.4", "v29.4"], env=environment)
            verified = run(["git", "-C", repository, "verify-tag", "v29.4"], check=False, env=environment)
            self.assertEqual(verified.returncode, 0, verified.stderr)

    def test_runbook_has_copyable_real_commands_and_complete_review(self):
        text = RUNBOOK.read_text(encoding="utf-8")
        for required in (
            "roots-methodology.py",
            "\"$REPLAY\" replay",
            "canonical-lineage.bash",
            "incremental-oracle.bash",
            "roots-trusted-replay-gate.py",
            "roots-continuous-accounting.py",
            "roots-release-evidence.py",
            "prepare-release.sh",
            "\"$REPLAY\" resume",
            "\"$REPLAY\" abandon",
            "later base is not allowlisted",
            "never a Bitcoin Knots branch",
            "never a previous Roots trunk",
        ):
            self.assertIn(required, text)
        self.assertNotIn(" --help", text)

        review = json.loads(REVIEW.read_text(encoding="utf-8"))
        expected_domains = {
            "exhaustive-adaptation-inventory",
            "modularization-and-upstreaming-roadmap",
            "deterministic-replay-products",
            "release-and-continuous-accounting-integration",
            "maintainer-procedure",
        }
        self.assertEqual({domain["id"] for domain in review["review_domains"]}, expected_domains)
        for domain in review["review_domains"]:
            self.assertTrue(domain["inputs"])
            self.assertTrue(all((ROOT / path).exists() for path in domain["inputs"]))
            self.assertIn(domain["finding_result"], {"no-unresolved-p0-p1", "resolved"})
            self.assertTrue(all(finding["disposition"] == "resolved" for finding in domain["findings"]))
        self.assertEqual(review["severity_review"]["p0"], [])
        self.assertEqual(review["severity_review"]["p1"], [])

    def replay_layer(self, source, state, review, materials_root, revision, tree, final_state):
        state.mkdir()
        review.mkdir()
        run([
            sys.executable, REPLAY, "replay", "--repository", source,
            "--revision", revision, "--expected-tree", tree,
            "--manifest", materials_root / "adaptation-manifest-29.3.json",
            "--materials", materials_root / "replay-materials.json",
            "--materials-root", materials_root, "--state-directory", state,
            "--apply",
        ])
        state_path = state / final_state
        run([sys.executable, REPLAY, "report", "--state", state_path, "--output-directory", review])
        export = review / "replay-generated-series.patch"
        run([
            sys.executable, REPLAY, "export-patches",
            "--repository", state / "owned-candidate", "--state", state_path,
            "--output", export,
        ])
        value = json.loads(state_path.read_text(encoding="utf-8"))
        return state_path, value, {
            "state": sha256(state_path),
            "report_json": sha256(review / "replay-review.json"),
            "report_text": sha256(review / "replay-review.txt"),
            "generated_export": sha256(export),
            "outcome_set": "sha256:" + hashlib.sha256(canonical(value["completed_units"]).encode()).hexdigest(),
        }

    def release_source(self, work):
        repository = work / "release-source"
        for relative in (
            "contrib/roots/lineage-ledger.json",
            "contrib/roots/adaptation-manifest-29.3.json",
            "contrib/roots/replay-29.4-proposal/acceptance-evidence.json",
        ):
            destination = repository / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, destination)
        run(["git", "init", "--quiet", repository])
        git(repository, "config", "user.email", "rehearsal@example.invalid")
        git(repository, "config", "user.name", "Roots rehearsal")
        git(repository, "add", "contrib")
        environment = {
            **os.environ,
            "GIT_AUTHOR_DATE": "2000-01-01T00:00:00Z",
            "GIT_COMMITTER_DATE": "2000-01-01T00:00:00Z",
        }
        run(["git", "-C", repository, "commit", "--quiet", "-m", "accepted release inputs"], env=environment)
        return repository

    def write_package(self, path):
        content = b"runbook package"
        member = "bitcoin-roots-29.4-roots.1/bin/bitcoin-qt"
        with tarfile.open(path, mode="w:gz") as package:
            info = tarfile.TarInfo(member)
            info.size = len(content)
            package.addfile(info, io.BytesIO(content))

    def rehearse_release(self, work):
        source = self.release_source(work)
        revision = git(source, "rev-parse", "HEAD")
        tree = git(source, "rev-parse", "HEAD^{tree}")
        tags_before = git(source, "for-each-ref", "--format=%(refname) %(objectname)", "refs/tags")
        release = work / "release"
        downloads = work / "downloads"
        prepared = work / "prepared"
        release.mkdir()
        downloads.mkdir()
        evidence = release / "roots-release-evidence.json"
        inputs = [
            "--ledger", source / "contrib/roots/lineage-ledger.json",
            "--manifest", source / "contrib/roots/adaptation-manifest-29.3.json",
            "--replay-result", source / "contrib/roots/replay-29.4-proposal/acceptance-evidence.json",
            "--source-repository", source, "--source-revision", revision,
            "--candidate-tree", "sha1:" + tree, "--output", evidence.name,
        ]
        run([sys.executable, RELEASE_EVIDENCE, *inputs], cwd=release)
        run([sys.executable, RELEASE_EVIDENCE, *inputs, "--verify"], cwd=release)
        self.write_package(downloads / "bitcoin-roots-linux-x86_64.tar.gz")
        run([
            PREPARE_RELEASE, downloads, prepared, PUBLIC_KEY, "v29.4-roots.1", "1",
            evidence, source, revision,
        ], cwd=ROOT)
        run(["sha512sum", "--check", "SHA512SUMS"], cwd=prepared)
        tags_after = git(source, "for-each-ref", "--format=%(refname) %(objectname)", "refs/tags")
        self.assertEqual(tags_before, tags_after)
        checksum_lines = [line for line in (prepared / "SHA512SUMS").read_text().splitlines() if not line.startswith("#")]
        self.assertEqual([path.name for path in prepared.iterdir() if path.name == "SHA512SUMS"], ["SHA512SUMS"])
        return {
            "evidence": "verified", "evidence_digest": sha256(evidence),
            "sha512": "verified", "sha512_entries": len(checksum_lines),
            "tags_changed": tags_before != tags_after,
        }

    def account_decisions(self, work, repository, base):
        decisions = ["accept", "rewrite", "reject", "defer", "upstream"]
        decision_path = repository / "contrib/roots/rehearsal-decisions.json"
        decision_path.parent.mkdir(parents=True, exist_ok=True)
        decision_path.write_text(canonical({"schema_version": 1, "decisions": decisions}) + "\n", encoding="utf-8")
        git(repository, "config", "user.email", "rehearsal@example.invalid")
        git(repository, "config", "user.name", "Roots rehearsal")
        git(repository, "add", "contrib/roots/rehearsal-decisions.json")
        environment = {
            **os.environ,
            "GIT_AUTHOR_DATE": "2000-01-01T00:00:00Z",
            "GIT_COMMITTER_DATE": "2000-01-01T00:00:00Z",
        }
        run(["git", "-C", repository, "commit", "--quiet", "-m", "rehearse decision accounting"], env=environment)
        head = git(repository, "rev-parse", "HEAD")
        atoms = ACCOUNTING.atoms(repository, base, head)
        self.assertEqual(len(atoms), 1)
        path, kind, digest = next(iter(atoms))
        record = work / "accounting.json"
        record.write_text(json.dumps({
            "schema_version": 1,
            "changes": [{
                "path": path, "kind": kind, "digest": digest,
                "disposition": "add", "adaptation": "roots-post-methodology-maintainer-runbook-v1",
                "risk": "low", "tests": "ci/test/test_roots_maintainer_runbook.py",
                "dependencies": "accepted 29.4 lineage", "provenance": "runbook rehearsal",
                "replay_impact": "decision-accounting", "scope": path,
                "rationale": "exercise every documented maintainer decision",
            }],
        }, sort_keys=True), encoding="utf-8")
        run([
            sys.executable, ACCOUNTING_TOOL, "--repository", repository,
            "--base", base, "--head", head, "--record", record, "--public-release",
        ])
        return {"status": "passed", "atom_count": 1, "decisions": decisions}

    def complete_rehearsal(self, work):
        source = work / "source"
        run(["git", "clone", "--quiet", "--no-local", ROOT, source])
        source_head = git(source, "rev-parse", "HEAD")
        git(source, "fetch", "--quiet", "--no-tags", ROOT, f"{VALIDATION_REF}:{VALIDATION_REF}")
        tags_before = git(source, "for-each-ref", "--format=%(refname) %(objectname)", "refs/tags")

        run([sys.executable, ROOT / "contrib/devtools/roots-methodology.py", METHODOLOGY])
        for revision, tree in ((CORE_29_3_COMMIT, CORE_29_3_TREE), (CORE_29_4_COMMIT, CORE_29_4_TREE)):
            run([sys.executable, REPLAY, "verify", "--repository", source, "--revision", revision, "--expected-tree", tree])

        method = json.loads(METHODOLOGY.read_text(encoding="utf-8"))["reconstructions"]
        git(source, "checkout", "--quiet", "--detach", CORE_29_3_COMMIT)
        stage_a_state, stage_a, stage_a_digests = self.replay_layer(
            source, work / "core-knots-state", work / "core-knots-review",
            ROOT / "contrib/roots/replay-core-to-knots-29.3",
            CORE_29_3_COMMIT, CORE_29_3_TREE, "replay-state-0010.json",
        )
        self.assertEqual(stage_a["candidate_tree"], KNOTS_STAGE_TREE)
        self.assertEqual(stage_a_digests, method["core-to-knots-29.3"]["artifact_digests"])

        git(source, "checkout", "--quiet", "--detach", KNOTS_29_3_COMMIT)
        roots_state, roots_29_3, roots_digests = self.replay_layer(
            source, work / "roots-29.3-state", work / "roots-29.3-review",
            ROOT / "contrib/roots/replay-29.3-release",
            KNOTS_29_3_COMMIT, KNOTS_STAGE_TREE, "replay-state-0016.json",
        )
        self.assertEqual(roots_29_3["candidate_tree"], ROOTS_29_3_TREE)
        self.assertEqual(roots_digests, method["roots-29.3"]["artifact_digests"])
        applied = sum(item["outcome"] == "applied" for item in roots_29_3["completed_units"])
        manual = sum(item["outcome"] == "manual" for item in roots_29_3["completed_units"])
        git(source, "checkout", "--quiet", "--detach", source_head)

        canonical_destination = work / "canonical-29.4"
        canonical_result = run([
            ROOT / "contrib/roots/replay-29.4-proposal/canonical-lineage.bash",
            source, canonical_destination,
        ])
        incremental_result = run([
            ROOT / "contrib/roots/replay-29.4-proposal/incremental-oracle.bash",
            source, work / "incremental-29.4",
        ])
        self.assertIn(f"target_commit={CANONICAL_29_4_COMMIT}", canonical_result.stdout)
        self.assertIn(f"target_tree={CANONICAL_29_4_TREE}", canonical_result.stdout)
        self.assertIn(f"oracle_commit={INCREMENTAL_29_4_COMMIT}", incremental_result.stdout)
        self.assertIn(f"oracle_tree={INCREMENTAL_29_4_TREE}", incremental_result.stdout)
        self.assertIn("conflict_count=12", incremental_result.stdout)

        trusted = work / "trusted"
        trusted.mkdir()
        trusted_report = trusted / "trusted-replay-report.json"
        run([
            sys.executable, TRUSTED_GATE, "--mode", "manual", "--changed", "true",
            "--manual-run", "force", "--later-base", "none",
            "--manifest", ROOT / "contrib/roots/adaptation-manifest-29.3.json",
            "--methodology", METHODOLOGY,
            "--fixture", ROOT / "contrib/roots/core-29.4-migration-fixture.json",
            "--output", trusted_report,
        ])
        self.assertEqual(json.loads(trusted_report.read_text())["decision"], "replay")

        plan_directory = work / "later-plan"
        plan_directory.mkdir()
        plan = plan_directory / "replay-plan.json"
        run([
            sys.executable, REPLAY, "plan", "--repository", source,
            "--revision", CORE_29_4_COMMIT, "--expected-tree", CORE_29_4_TREE,
            "--output", plan,
        ])

        staging = work / "staging-29.4"
        run(["git", "clone", "--quiet", "--no-checkout", source, staging])
        git(staging, "switch", "--quiet", "--create", "roots/integration-v29.4", CORE_29_4_COMMIT)
        git(staging, "fetch", "--quiet", "--no-tags", canonical_destination, f"refs/heads/roots-29.4-canonical:refs/roots/candidate-29.4")
        staged = json.loads(run([
            sys.executable, REPLAY, "stage-candidate", "--repository", staging,
            "--integration-branch", "roots/integration-v29.4",
            "--expected-head", CORE_29_4_COMMIT,
            "--candidate-revision", CANONICAL_29_4_COMMIT,
            "--expected-candidate-tree", CANONICAL_29_4_TREE,
            "--confirm", "I_STAGE_THE_EXACT_CANDIDATE",
        ]).stdout)
        accounting = self.account_decisions(work, staging, CANONICAL_29_4_COMMIT)

        resume_state = work / "roots-29.3-state/replay-state-0008.json"
        resume_value = json.loads(resume_state.read_text(encoding="utf-8"))
        for state_path in (work / "roots-29.3-state").glob("replay-state-*.json"):
            if int(state_path.stem.rsplit("-", 1)[1]) > 8:
                state_path.unlink()
        owned_candidate = work / "roots-29.3-state/owned-candidate"
        git(owned_candidate, "reset", "--hard", KNOTS_29_3_COMMIT)
        git(owned_candidate, "read-tree", resume_value["candidate_tree"])
        git(owned_candidate, "checkout-index", "--all", "--force")
        self.assertEqual(git(owned_candidate, "write-tree"), resume_value["candidate_tree"])
        inspected = json.loads(run([sys.executable, REPLAY, "inspect", "--state", resume_state]).stdout)
        git(source, "checkout", "--quiet", "--detach", KNOTS_29_3_COMMIT)
        resumed = json.loads(run([
            sys.executable, REPLAY, "resume", "--repository", source,
            "--revision", KNOTS_29_3_COMMIT, "--expected-tree", KNOTS_STAGE_TREE,
            "--manifest", ROOT / "contrib/roots/replay-29.3-release/adaptation-manifest-29.3.json",
            "--materials", ROOT / "contrib/roots/replay-29.3-release/replay-materials.json",
            "--materials-root", ROOT / "contrib/roots/replay-29.3-release",
            "--state", resume_state, "--state-directory", work / "roots-29.3-state",
        ]).stdout)
        self.assertTrue(roots_state.is_file())
        abandon = json.loads(run([
            sys.executable, REPLAY, "abandon", "--state", roots_state,
            "--output", work / "roots-29.3-state/replay-abandoned.json",
        ]).stdout)
        git(source, "checkout", "--quiet", "--detach", source_head)

        release = self.rehearse_release(work)
        tags_after = git(source, "for-each-ref", "--format=%(refname) %(objectname)", "refs/tags")
        self.assertEqual(tags_before, tags_after)
        self.assertEqual(git(source, "rev-parse", "HEAD"), source_head)

        summary = {
            "schema_version": 1,
            "roots_29_3": {
                "core_to_knots_tree": "sha1:" + stage_a["candidate_tree"],
                "roots_tree": "sha1:" + roots_29_3["candidate_tree"],
                "units": len(roots_29_3["completed_units"]),
                "applied": applied,
                "manual": manual,
            },
            "canonical_29_4": {
                "decision": "accepted",
                "target_commit": "sha1:" + CANONICAL_29_4_COMMIT,
                "target_tree": "sha1:" + CANONICAL_29_4_TREE,
                "incremental_accepted": False,
                "incremental_tree": "sha1:" + INCREMENTAL_29_4_TREE,
            },
            "trusted_replay": {"decision": "replay", "report_digest": sha256(trusted_report)},
            "later_core_candidate": {
                "status": "prepared-not-published",
                "integration_branch": staged["branch"],
                "tree": "sha1:" + staged["tree"],
                "plan_digest": json.loads(plan.read_text())["digest"],
                "accounting": accounting["status"],
                "decisions": accounting["decisions"],
            },
            "release_preparation": release,
            "recovery": {
                "resume": "passed" if resumed["candidate"] == "owned-candidate" else "failed",
                "safe_stop": abandon["action"],
                "candidate_tree": "sha1:" + json.loads(roots_state.read_text())["candidate_tree"],
                "resumed_from_units": len(inspected["completed_units"]),
            },
        }
        summary_path = work / "rehearsal-summary.json"
        summary_path.write_text(canonical(summary) + "\n", encoding="utf-8")
        return summary_path

    def test_two_fresh_clones_execute_complete_deterministic_rehearsal(self):
        review_contract = json.loads(REVIEW.read_text(encoding="utf-8"))["rehearsal_contract"]
        summaries = []
        for _ in range(2):
            with tempfile.TemporaryDirectory() as temporary:
                summary_path = self.complete_rehearsal(Path(temporary))
                summary = json.loads(summary_path.read_text(encoding="utf-8"))
                for key in ("roots_29_3", "canonical_29_4", "later_core_candidate", "release_preparation", "recovery"):
                    for expected_key, expected_value in review_contract[key].items():
                        self.assertEqual(summary[key][expected_key], expected_value)
                summaries.append(summary_path.read_bytes())
        self.assertEqual(*summaries)
        self.assertEqual(review_contract["successful_fresh_clones"], 2)

    def test_real_tools_drive_representative_safe_stops(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            source = work / "source"
            run(["git", "clone", "--quiet", "--no-local", ROOT, source])
            git(source, "fetch", "--quiet", "--no-tags", ROOT, f"{VALIDATION_REF}:{VALIDATION_REF}")

            readme = source / "README.md"
            readme.write_text(readme.read_text(encoding="utf-8") + "dirty\n", encoding="utf-8")
            dirty = run([
                sys.executable, REPLAY, "verify", "--repository", source,
                "--revision", CORE_29_4_COMMIT, "--expected-tree", CORE_29_4_TREE,
            ], check=False)
            self.assertNotEqual(dirty.returncode, 0)
            self.assertIn("tracked worktree and index must be clean", dirty.stderr)
            git(source, "reset", "--hard", "HEAD")

            corrupt = run([
                sys.executable, REPLAY, "verify", "--repository", source,
                "--revision", CORE_29_4_COMMIT, "--expected-tree", "0" * 40,
            ], check=False)
            self.assertNotEqual(corrupt.returncode, 0)
            self.assertIn("locked tree does not match revision", corrupt.stderr)

            missing = run([
                ROOT / "contrib/roots/replay-29.4-proposal/canonical-lineage.bash",
                work / "missing-upstream", work / "unused-canonical",
            ], check=False)
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn("source is not a Git worktree", missing.stderr)

            invalid_materials = work / "replay-materials.json"
            materials = json.loads((ROOT / "contrib/roots/replay-29.3-release/replay-materials.json").read_text())
            materials["schema_version"] = 0
            invalid_materials.write_text(json.dumps(materials), encoding="utf-8")
            schema = run([
                sys.executable, REPLAY, "replay", "--repository", source,
                "--revision", KNOTS_29_3_COMMIT, "--expected-tree", KNOTS_STAGE_TREE,
                "--manifest", ROOT / "contrib/roots/replay-29.3-release/adaptation-manifest-29.3.json",
                "--materials", invalid_materials,
                "--materials-root", ROOT / "contrib/roots/replay-29.3-release",
                "--state-directory", work / "unused-state",
            ], check=False)
            self.assertNotEqual(schema.returncode, 0)
            self.assertIn("schema version is unsupported", schema.stderr)

            later = run([
                sys.executable, TRUSTED_GATE, "--mode", "manual", "--changed", "true",
                "--later-base", "v30.0", "--manifest", ROOT / "contrib/roots/adaptation-manifest-29.3.json",
                "--methodology", METHODOLOGY,
                "--fixture", ROOT / "contrib/roots/core-29.4-migration-fixture.json",
                "--output", work / "trusted-replay-report.json",
            ], check=False)
            self.assertNotEqual(later.returncode, 0)
            self.assertIn("later base is not allowlisted", later.stderr)

            fixture = work / "fixture.json"
            fixture_value = json.loads((ROOT / "contrib/roots/core-29.4-migration-fixture.json").read_text())
            fixture_value["core_inputs"]["v29.4"]["tree"] = "0" * 40
            fixture.write_text(json.dumps(fixture_value), encoding="utf-8")
            invariant = run([
                sys.executable, TRUSTED_GATE, "--mode", "manual", "--changed", "true",
                "--later-base", "none", "--manifest", ROOT / "contrib/roots/adaptation-manifest-29.3.json",
                "--methodology", METHODOLOGY, "--fixture", fixture,
                "--output", work / "trusted-replay-report.json",
            ], check=False)
            self.assertNotEqual(invariant.returncode, 0)
            self.assertIn("migration fixture differs from accepted immutable locks", invariant.stderr)


if __name__ == "__main__":
    unittest.main()
