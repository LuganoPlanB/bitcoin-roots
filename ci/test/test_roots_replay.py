#!/usr/bin/env python3
"""Focused safety tests for the offline Roots replay CLI."""

from __future__ import annotations

import copy
import importlib.util
import json
from concurrent.futures import ThreadPoolExecutor
import hashlib
import os
from pathlib import Path
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "contrib/devtools/roots-replay.py"
MANIFEST_SCRIPT = ROOT / "contrib/devtools/roots-adaptation-manifest.py"


def load_module():
    spec = importlib.util.spec_from_file_location("roots_replay", SCRIPT)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


REPLAY = load_module()


def load_manifest_module():
    spec = importlib.util.spec_from_file_location("roots_adaptation_manifest_l7", MANIFEST_SCRIPT)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


MANIFEST = load_manifest_module()


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
    L7_CORE_29_4_COMMIT = "3fc0865963a38b871e9f7d94e6151c4953563516"
    L7_CORE_29_4_TREE = "38ad59b187f59647eb90ad1347bc481485ef4d01"
    L7_CANDIDATE_COMMIT = "f771e13259f23f02efc215327f2806d421cc7339"
    L7_CANDIDATE_TREE = "39a5e30207a09962e78ae81c24cc65b1e478ef90"
    L7_INCREMENTAL_COMMIT = "3e29908f7a0131a71309e80a78fe865ec8a50f76"
    L7_INCREMENTAL_TREE = "c676e8944470cc74fcc213e7368aed359ad8ae55"
    L7_CANONICAL_COMMIT = "cbc88cff9b35b95a549c0313e424e13093fcd6a1"

    def test_l7_29_4_proposal_accounts_for_git_and_snapshot_outcomes(self):
        """The fresh-port proposal cannot omit a Git path or archive-only identity."""
        proposal = json.loads(
            (ROOT / "contrib/roots/replay-29.4-proposal/manual-resolution-proposal.json").read_text()
        )
        outcomes = proposal["outcomes"]
        self.assertEqual(len(outcomes), 40)
        self.assertEqual(sum(item["git_tree_delta"] for item in outcomes), 39)
        snapshot = [item for item in outcomes if not item["git_tree_delta"]]
        self.assertEqual([item["path"] for item in snapshot], ["src/clientversion.cpp"])
        self.assertEqual(
            {item["proposed_status"] for item in outcomes},
            {"automatic", "absorbed", "manual", "rewritten"},
        )

        fixture = json.loads(
            (ROOT / "contrib/roots/core-29.4-migration-fixture.json").read_text()
        )
        expected_paths = {
            path
            for paths in fixture["expected_outcomes"].values()
            for path in paths
        }
        expected_paths.add(fixture["snapshot_only"]["path"])
        self.assertEqual({item["path"] for item in outcomes}, expected_paths)
        self.assertEqual(
            sum(item["proposed_status"] == "rewritten" for item in outcomes),
            len(fixture["expected_outcomes"]["generated"]),
        )

        l4_mapping = {
            path: classification
            for classification, paths in fixture["expected_outcomes"].items()
            for path in paths
        }
        l4_mapping[fixture["snapshot_only"]["path"]] = "snapshot-only"
        self.assertEqual(
            {item["path"]: item["l4_expected_outcome"] for item in outcomes},
            l4_mapping,
        )
        proposal_digest = proposal.pop("digest")
        self.assertEqual(
            proposal_digest,
            "sha256:" + hashlib.sha256(
                json.dumps(proposal, sort_keys=True, separators=(",", ":")).encode()
            ).hexdigest(),
        )

        ordered = json.loads(
            (ROOT / "contrib/roots/replay-29.4-proposal/ordered-outcome-map.json").read_text()
        )
        self.assertEqual(ordered["proposal_digest"], proposal_digest)
        self.assertEqual(ordered["candidate_tree"], "sha1:" + self.L7_CANDIDATE_TREE)
        self.assertEqual(
            {
                status: sum(item["proposed_status"] == status for item in outcomes)
                for status in ("automatic", "absorbed", "manual", "rewritten")
            },
            {key: ordered["expected"][key] for key in ("automatic", "absorbed", "manual", "rewritten")},
        )

    def test_l7_29_4_downstream_units_and_provenance_are_complete(self):
        """All sixteen L3 units and exact Core ancestry remain independently visible."""
        root = ROOT / "contrib/roots/replay-29.4-proposal"
        manifest = json.loads((ROOT / "contrib/roots/adaptation-manifest-29.3.json").read_text())
        evidence = json.loads((root / "downstream-unit-outcomes.json").read_text())
        self.assertEqual(
            [item["id"] for item in evidence["outcomes"]],
            [item["id"] for item in manifest["units"]],
        )
        self.assertEqual(len(evidence["outcomes"]), 16)
        self.assertEqual(
            sum(item["declared_paths"] for item in evidence["outcomes"]), 839
        )
        self.assertEqual(
            sum(item["changed_paths"] for item in evidence["outcomes"]), 837
        )
        absorbed = sorted(
            path for item in evidence["outcomes"] for path in item["absorbed_paths"]
        )
        self.assertEqual(absorbed, ["src/common/netif.cpp", "src/txrequest.cpp"])
        self.assertEqual(evidence["accounting"]["absorbed_paths"], absorbed)
        self.assertEqual(evidence["accounting"]["unaccounted_paths"], [])
        self.assertEqual({item["status"] for item in evidence["outcomes"]}, {"rewritten"})

        provenance = json.loads((root / "provenance.json").read_text())
        fixture = json.loads(
            (ROOT / "contrib/roots/core-29.4-migration-fixture.json").read_text()
        )
        for label in ("v29.3", "v29.4"):
            self.assertEqual(
                provenance["core_tags"][label]["tag_object"],
                "sha1:" + fixture["core_inputs"][label]["tag"],
            )
            self.assertEqual(
                provenance["core_tags"][label]["peeled_commit"],
                "sha1:" + fixture["core_inputs"][label]["commit"],
            )
            self.assertEqual(
                provenance["core_tags"][label]["tree"],
                "sha1:" + fixture["core_inputs"][label]["tree"],
            )
        self.assertFalse(provenance["candidate"]["validation_commit_is_lineage"])
        self.assertEqual(provenance["candidate"]["base"], "sha1:" + self.L7_CORE_29_4_COMMIT)
        self.assertEqual(provenance["candidate"]["tree"], "sha1:" + self.L7_CANDIDATE_TREE)
        self.assertEqual(
            {item["pull_request"] for item in provenance["rejected_validation_candidates"]},
            {11, 12, 13, 14},
        )

        available = subprocess.run(
            ["git", "-C", str(ROOT), "cat-file", "-e", self.L7_CANDIDATE_COMMIT + "^{commit}"],
            capture_output=True,
        )
        if available.returncode:
            self.skipTest("the disposable candidate object is unavailable")
        self.assertEqual(
            git(ROOT, "rev-parse", self.L7_CANDIDATE_COMMIT + "^"),
            self.L7_CORE_29_4_COMMIT,
        )
        self.assertEqual(
            git(ROOT, "rev-parse", self.L7_CANDIDATE_COMMIT + "^{tree}"),
            self.L7_CANDIDATE_TREE,
        )
        candidate_listing = subprocess.run(
            ["git", "-C", str(ROOT), "ls-tree", "-r", "-z", self.L7_CANDIDATE_TREE],
            check=True,
            capture_output=True,
        ).stdout
        self.assertEqual(
            provenance["candidate"]["tree_digest"],
            "sha256:" + hashlib.sha256(candidate_listing).hexdigest(),
        )
        scoped_paths = sorted({
            path
            for unit in manifest["units"]
            for path in unit["touched"]["paths"]
        })
        scoped_listing = b"".join(
            subprocess.run(
                ["git", "-C", str(ROOT), "ls-tree", "-z", self.L7_CANDIDATE_TREE, "--", path],
                check=True,
                capture_output=True,
            ).stdout
            for path in scoped_paths
        )
        self.assertEqual(
            provenance["candidate"]["scoped_tree_digest"],
            "sha256:" + hashlib.sha256(scoped_listing).hexdigest(),
        )

        proposal = json.loads((root / "manual-resolution-proposal.json").read_text())
        reviewed = [item for item in proposal["outcomes"] if "result_blob" in item]
        self.assertEqual(len(reviewed), 13)
        for item in reviewed:
            listing = git(ROOT, "ls-tree", self.L7_CANDIDATE_TREE, "--", item["path"])
            self.assertEqual("sha1:" + listing.split()[2], item["result_blob"])

    def test_l7_29_4_platform_evidence_never_calls_the_failed_run_green(self):
        evidence = json.loads(
            (ROOT / "contrib/roots/replay-29.4-proposal/validation-evidence.json").read_text()
        )
        actions = evidence["github_actions"]
        self.assertEqual(actions["run_conclusion"], "failure")
        self.assertEqual(actions["jobs"], {"success": 17, "failure": 1, "skipped": 1})
        self.assertEqual(
            {value["status"] for value in actions["platforms"].values()}, {"pass"}
        )
        self.assertEqual(actions["lint"]["status"], "known-inherited-failure")
        self.assertFalse(actions["lint"]["candidate_correction_authorized"])

    def test_l7_incremental_comparison_accounts_for_every_difference(self):
        """The incremental oracle is rejected and every observed delta is owned."""
        root = ROOT / "contrib/roots/replay-29.4-proposal"
        evidence = json.loads((root / "incremental-comparison.json").read_text())
        evidence_digest = evidence.pop("digest")
        self.assertEqual(
            evidence_digest,
            "sha256:" + hashlib.sha256(
                json.dumps(evidence, sort_keys=True, separators=(",", ":")).encode()
            ).hexdigest(),
        )
        self.assertFalse(evidence["accepted_lineage"])
        fixture = json.loads(
            (ROOT / "contrib/roots/core-29.4-migration-fixture.json").read_text()
        )
        l4_paths = {
            path for unit in fixture["units"] for path in unit["paths"]
        }
        self.assertEqual(len(l4_paths), evidence["incremental_oracle"]["core_delta_git_paths"])
        self.assertEqual(
            evidence["incremental_oracle"]["locked_l4_units"],
            [unit["id"] for unit in fixture["units"]],
        )
        self.assertEqual(
            evidence["incremental_oracle"]["locked_l4_snapshot_paths"],
            [fixture["snapshot_only"]["path"]],
        )
        self.assertEqual(
            evidence["incremental_oracle"]["script_digest"],
            "sha256:" + hashlib.sha256((root / "incremental-oracle.bash").read_bytes()).hexdigest(),
        )
        build = evidence["comparisons"]["build_artifacts"]
        transcript = root / build["transcript"]
        self.assertEqual(
            build["transcript_digest"],
            "sha256:" + hashlib.sha256(transcript.read_bytes()).hexdigest(),
        )
        self.assertEqual(build["configure_status"], 0)
        self.assertEqual(build["build_status"], 2)
        self.assertEqual(build["produced_targets"], [])
        self.assertIn("DEFAULT_PRINT_MODIFIED_FEE", transcript.read_text())
        self.assertIn("UpdateDependentPriorities", transcript.read_text())

        raw = evidence["comparisons"]["raw_tree"]
        self.assertEqual(raw["difference_count"], len(raw["differences"]))
        self.assertEqual(len({item["path"] for item in raw["differences"]}), 11)
        self.assertTrue(all(item["owner"] == "L7.4" for item in raw["differences"]))
        self.assertEqual(
            raw["class_counts"],
            {
                "implementation-history-noise": 0,
                "tool-defect": 0,
                "missing-adaptation": 6,
                "unsafe-manual-divergence": 5,
                "reviewed-equivalent-implementation": 0,
            },
        )
        self.assertEqual(evidence["accounting"]["unowned_differences"], [])
        self.assertEqual(evidence["accounting"]["accepted_history"], "fresh-only")
        self.assertEqual(
            evidence["comparisons"]["effort_and_performance"]["result"],
            "not-comparable",
        )
        self.assertEqual(
            evidence["fresh_candidate"]["candidate_evidence_digest"],
            "sha256:" + hashlib.sha256((root / "candidate-evidence-draft.json").read_bytes()).hexdigest(),
        )

    def test_l7_incremental_oracle_reconstructs_and_exposes_hotfix(self):
        """Two incremental constructions agree, while an undeclared hotfix is rejected."""
        proposal = ROOT / "contrib/roots/replay-29.4-proposal"
        evidence = json.loads((proposal / "incremental-comparison.json").read_text())
        script = proposal / "incremental-oracle.bash"
        repositories = []
        with tempfile.TemporaryDirectory() as directory:
            for index in range(2):
                repository = Path(directory) / f"oracle-{index}"
                result = subprocess.run(
                    [str(script), str(ROOT), str(repository)],
                    text=True,
                    capture_output=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("conflict_count=12", result.stdout)
                self.assertEqual(git(repository, "write-tree"), self.L7_INCREMENTAL_TREE)
                self.assertEqual(
                    git(repository, "rev-parse", self.L7_INCREMENTAL_COMMIT + "^{tree}"),
                    self.L7_INCREMENTAL_TREE,
                )
                repositories.append(repository)

            repository = repositories[0]
            raw = evidence["comparisons"]["raw_tree"]
            fixture = json.loads(
                (ROOT / "contrib/roots/core-29.4-migration-fixture.json").read_text()
            )
            locked_paths = sorted(
                path for unit in fixture["units"] for path in unit["paths"]
            )
            self.assertEqual(
                git(repository, "diff", "--name-only",
                    "99003bed87333f1be51bf3070235591b3a72f007",
                    self.L7_CORE_29_4_COMMIT).splitlines(),
                locked_paths,
            )
            actual_paths = git(
                repository, "diff", "--name-only", self.L7_INCREMENTAL_TREE,
                self.L7_CANDIDATE_TREE,
            ).splitlines()
            self.assertEqual(actual_paths, [item["path"] for item in raw["differences"]])
            raw_diff = subprocess.run(
                ["git", "-C", str(repository), "diff", "--binary",
                 self.L7_INCREMENTAL_TREE, self.L7_CANDIDATE_TREE],
                check=True,
                capture_output=True,
            ).stdout
            self.assertEqual(
                raw["binary_diff_digest"],
                "sha256:" + hashlib.sha256(raw_diff).hexdigest(),
            )

            patch_ranges = (
                ("incremental", "42098b53c57fb6818736c7b6732fa84ffe6ad391", self.L7_INCREMENTAL_TREE),
                ("fresh", self.L7_CORE_29_4_COMMIT, self.L7_CANDIDATE_TREE),
            )
            for label, base, tree in patch_ranges:
                patch = subprocess.run(
                    ["git", "-C", str(repository), "diff", "--binary", base, tree],
                    check=True,
                    capture_output=True,
                ).stdout
                patch_id = subprocess.run(
                    ["git", "patch-id", "--stable"],
                    input=patch,
                    check=True,
                    capture_output=True,
                ).stdout.decode().split()[0]
                self.assertEqual(
                    evidence["comparisons"]["patch_id"][label], "sha1:" + patch_id
                )

            range_diff = subprocess.run(
                ["git", "-C", str(repository), "range-diff",
                 "42098b53c57fb6818736c7b6732fa84ffe6ad391.." + self.L7_INCREMENTAL_COMMIT,
                 self.L7_CORE_29_4_COMMIT + ".." + self.L7_CANDIDATE_COMMIT],
                check=True,
                capture_output=True,
            ).stdout
            self.assertEqual(
                evidence["comparisons"]["range_diff"]["digest"],
                "sha256:" + hashlib.sha256(range_diff).hexdigest(),
            )

            golden = evidence["comparisons"]["generated_and_api_config_golden"]
            for label, tree in (
                ("incremental", self.L7_INCREMENTAL_TREE),
                ("fresh", self.L7_CANDIDATE_TREE),
            ):
                manpages = b"".join(
                    subprocess.run(
                        ["git", "-C", str(repository), "show", f"{tree}:{path}"],
                        check=True,
                        capture_output=True,
                    ).stdout
                    for path in golden["manpage_paths"]
                )
                self.assertEqual(
                    golden[f"{label}_manpages_digest"],
                    "sha256:" + hashlib.sha256(manpages).hexdigest(),
                )
                config = subprocess.run(
                    ["git", "-C", str(repository), "show", f"{tree}:{golden['config_path']}"],
                    check=True,
                    capture_output=True,
                ).stdout
                self.assertEqual(
                    golden[f"{label}_config_digest"],
                    "sha256:" + hashlib.sha256(config).hexdigest(),
                )

            hotfix = evidence["hotfix_injection"]
            original = subprocess.run(
                ["git", "-C", str(repository), "show",
                 f"{self.L7_INCREMENTAL_TREE}:{hotfix['path']}"],
                check=True,
                capture_output=True,
            ).stdout
            payload = original + b"\n// Undocumented incremental-only hotfix sentinel.\n"
            blob = subprocess.run(
                ["git", "-C", str(repository), "hash-object", "-w", "--stdin"],
                input=payload,
                check=True,
                capture_output=True,
            ).stdout.decode().strip()
            self.assertEqual(hotfix["blob"], "sha1:" + blob)
            git(repository, "read-tree", self.L7_INCREMENTAL_TREE)
            git(repository, "update-index", "--cacheinfo", f"100644,{blob},{hotfix['path']}")
            hotfix_tree = git(repository, "write-tree")
            self.assertEqual(hotfix["tree"], "sha1:" + hotfix_tree)
            unexpected = sorted(
                set(git(repository, "diff", "--name-only", hotfix_tree,
                        self.L7_CANDIDATE_TREE).splitlines()) - set(actual_paths)
            )
            self.assertEqual(unexpected, hotfix["detected_unexpected_paths"])
            self.assertEqual(hotfix["result"], "rejected")

    def l7_canonical_lineage(self, root: Path) -> tuple[Path, subprocess.CompletedProcess[str]]:
        repository = root / "canonical"
        script = ROOT / "contrib/roots/replay-29.4-proposal/canonical-lineage.bash"
        result = subprocess.run(
            [str(script), str(ROOT), str(repository)],
            text=True,
            capture_output=True,
        )
        return repository, result

    @staticmethod
    def l7_refresh_acceptance_digest(evidence: dict[str, object]) -> None:
        payload = copy.deepcopy(evidence)
        payload.pop("digest", None)
        evidence["digest"] = "sha256:" + hashlib.sha256(
            json.dumps(payload, sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest()

    def assert_l7_acceptance(
        self,
        evidence: dict[str, object],
        repository: Path,
        topology_override: dict[str, object] | None = None,
    ) -> None:
        proposal_root = ROOT / "contrib/roots/replay-29.4-proposal"
        digest = evidence["digest"]
        payload = copy.deepcopy(evidence)
        payload.pop("digest")
        self.assertEqual(
            digest,
            "sha256:" + hashlib.sha256(
                json.dumps(payload, sort_keys=True, separators=(",", ":")).encode()
            ).hexdigest(),
        )
        self.assertEqual(evidence["status"], "accepted")
        self.assertEqual(evidence["scope"], "technical alignment acceptance only")
        self.assertTrue(all(
            gate["status"] == "pass" and not gate["waiver"]
            for gate in evidence["gates"] if gate["critical"]
        ))

        proposal = json.loads((proposal_root / "manual-resolution-proposal.json").read_text())
        l4 = evidence["l4_accounting"]
        self.assertEqual(l4["outcome_count"], len(proposal["outcomes"]))
        self.assertEqual(l4["git_tree_outcomes"], sum(
            item["git_tree_delta"] for item in proposal["outcomes"]
        ))
        self.assertEqual(l4["snapshot_outcomes"], sum(
            not item["git_tree_delta"] for item in proposal["outcomes"]
        ))
        self.assertEqual(
            l4["status_counts"],
            {
                status: sum(item["proposed_status"] == status for item in proposal["outcomes"])
                for status in ("automatic", "absorbed", "manual", "rewritten")
            },
        )
        self.assertEqual(l4["unaccounted"], [])

        def file_digest(path: Path) -> str:
            return "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()

        l6 = evidence["l6_evidence"]
        l6_path = proposal_root / l6["source"]
        self.assertEqual(l6["source_sha256"], file_digest(l6_path))
        l6_value = json.loads(l6_path.read_text())
        self.assertEqual(l6["canonical_digest"], l6_value["digest"])
        l6_result = subprocess.run(
            [sys.executable, str(ROOT / "contrib/devtools/roots-semantic-safety.py"),
             "validate-evidence", str(l6_path)],
            text=True,
            capture_output=True,
        )
        self.assertEqual(l6_result.returncode, 0, l6_result.stderr)

        approval = evidence["approval"]
        approval_path = proposal_root / approval["source"]
        approval_value = json.loads(approval_path.read_text())
        self.assertEqual(approval["sha256"], file_digest(approval_path))
        self.assertEqual(approval["status"], "approved")
        self.assertEqual(approval_value["decision"], "accepted")
        self.assertEqual(approval["reviewer"], approval_value["reviewer_identity"])
        self.assertEqual(approval["proposal_digest"], approval_value["proposal_digest"])
        self.assertEqual(approval["candidate_tree"], approval_value["candidate_tree"])
        self.assertEqual(
            approval["approved_boundaries"],
            approval_value["scope"]["manual_rewritten_snapshot_boundaries"],
        )

        candidate_tree = evidence["candidate"]["tree"].removeprefix("sha1:")
        self.assertEqual(candidate_tree, self.L7_CANDIDATE_TREE)
        self.assertEqual(
            git(repository, "rev-parse", candidate_tree + "^{tree}"),
            self.L7_CANDIDATE_TREE,
        )
        listing = subprocess.run(
            ["git", "-C", str(repository), "ls-tree", "-r", "-z", candidate_tree],
            check=True,
            capture_output=True,
        ).stdout
        self.assertEqual(
            evidence["candidate"]["tree_digest"],
            "sha256:" + hashlib.sha256(listing).hexdigest(),
        )
        reproduction = evidence["reproducibility"]
        self.assertEqual(reproduction["status"], "pass")
        self.assertEqual(reproduction["fresh_replay_runs"], 2)
        self.assertEqual(
            reproduction["fresh_replay_trees"], ["sha1:" + candidate_tree] * 2
        )
        self.assertEqual(reproduction["canonical_lineage_runs"], 2)
        self.assertEqual(
            reproduction["canonical_lineage_commits"],
            ["sha1:" + self.L7_CANONICAL_COMMIT] * 2,
        )
        self.assertEqual(
            reproduction["canonical_lineage_trees"], ["sha1:" + candidate_tree] * 2
        )

        topology_path = proposal_root / evidence["lineage"]["topology"]
        topology = topology_override or json.loads(topology_path.read_text())
        self.assertEqual(evidence["lineage"]["topology_sha256"], file_digest(topology_path))
        constructor = proposal_root / evidence["lineage"]["constructor"]
        self.assertEqual(evidence["lineage"]["constructor_sha256"], file_digest(constructor))
        manifest = json.loads((ROOT / "contrib/roots/adaptation-manifest-29.3.json").read_text())
        try:
            MANIFEST.verify_topology(repository, topology, manifest)
        except MANIFEST.ManifestError as error:
            raise AssertionError(str(error)) from error
        self.assertEqual(
            [entry["adaptation_ids"][0] for entry in topology["commit_map"]],
            [unit["id"] for unit in manifest["units"]],
        )
        self.assertEqual(evidence["lineage"]["first_parent_commit_count"], 16)
        self.assertEqual(evidence["lineage"]["merge_commit_count"], 0)
        self.assertTrue(evidence["lineage"]["first_parent_exact_core_tag"])
        self.assertEqual(
            git(repository, "rev-parse", "refs/tags/v29.4"),
            "4e70eab99b60f7718b78e2158de9fb82726f3cec",
        )

        lineage_row_path = proposal_root / evidence["lineage"]["row"]
        lineage_row = json.loads(lineage_row_path.read_text())
        self.assertEqual(evidence["lineage"]["row_sha256"], file_digest(lineage_row_path))
        self.assertEqual(lineage_row["status"], "technically-accepted-unpublished")
        self.assertFalse(lineage_row["release"]["tag_created"])
        self.assertFalse(lineage_row["release"]["publication_authorized"])
        frozen_paths = {
            "adaptation_manifest_29_3": ROOT / "contrib/roots/adaptation-manifest-29.3.json",
            "candidate_adaptation_manifest": proposal_root / "adaptation-manifest-29.3.json",
            "replay_materials": proposal_root / "replay-materials.json",
            "ordered_outcome_map": proposal_root / "ordered-outcome-map.json",
            "manual_resolution_proposal": proposal_root / "manual-resolution-proposal.json",
            "downstream_unit_outcomes": proposal_root / "downstream-unit-outcomes.json",
            "recreation_recipe": proposal_root / "recreation-recipe.json",
            "provenance": proposal_root / "provenance.json",
            "manual_review_approval": proposal_root / "review-approval.json",
            "platform_validation": proposal_root / "validation-evidence.json",
            "candidate_evidence": proposal_root / "candidate-evidence-draft.json",
            "incremental_comparison": proposal_root / "incremental-comparison.json",
        }
        self.assertEqual(
            lineage_row["frozen_inputs"],
            {name: file_digest(path) for name, path in frozen_paths.items()},
        )

        identity = evidence["identity_and_release_notes"]
        self.assertEqual(
            identity["version"],
            {"major": 29, "minor": 4, "build": 0, "is_release": True},
        )
        self.assertFalse(identity["hard_coded_roots_29_4_identity"])
        for field, path in (
            ("cmake_blob", "CMakeLists.txt"),
            ("clientversion_blob", "src/clientversion.cpp"),
            ("release_notes_blob", identity["release_notes_path"]),
        ):
            entry = git(repository, "ls-tree", candidate_tree, "--", path)
            self.assertEqual(identity[field], "sha1:" + entry.split()[2])
        cmake = git(repository, "show", f"{candidate_tree}:CMakeLists.txt")
        self.assertIn("set(CLIENT_VERSION_MAJOR 29)", cmake)
        self.assertIn("set(CLIENT_VERSION_MINOR 4)", cmake)
        self.assertIn("set(CLIENT_VERSION_BUILD 0)", cmake)
        self.assertIn('set(CLIENT_VERSION_IS_RELEASE "true")', cmake)
        clientversion = git(repository, "show", f"{candidate_tree}:src/clientversion.cpp")
        self.assertIn('ua += "Roots:" + roots_version + "/";', clientversion)
        self.assertNotIn("29.4", clientversion)
        release_notes = git(
            repository, "show", f"{candidate_tree}:{identity['release_notes_path']}"
        )
        self.assertTrue(release_notes.startswith(identity["release_notes_identity"]))
        self.assertEqual(identity["status"], "pass")

        comparison = evidence["fresh_incremental_comparison"]
        comparison_path = proposal_root / comparison["source"]
        comparison_value = json.loads(comparison_path.read_text())
        self.assertEqual(comparison["source_sha256"], file_digest(comparison_path))
        self.assertEqual(
            comparison["owned_differences"],
            comparison_value["accounting"]["owned_difference_count"],
        )
        self.assertEqual(comparison["unowned_differences"], [])
        self.assertEqual(
            comparison["unowned_differences"],
            comparison_value["accounting"]["unowned_differences"],
        )
        self.assertFalse(comparison["incremental_lineage_accepted"])

        platform = evidence["platform_and_behavior"]
        validation_path = proposal_root / platform["source"]
        validation = json.loads(validation_path.read_text())
        self.assertEqual(platform["source_sha256"], file_digest(validation_path))
        self.assertEqual(validation["github_actions"]["run_conclusion"], "failure")
        self.assertEqual(
            {value["status"] for value in validation["github_actions"]["platforms"].values()},
            {"pass"},
        )
        self.assertEqual(len(validation["invariant_tests"]), platform["invariant_count"])
        self.assertTrue(all(item["status"] == "pass" for item in validation["invariant_tests"]))
        self.assertEqual(len(validation["generated_outputs"]["paths"]), platform["generated_outputs"])
        for path in (
            "ci/test/00_setup_env_native_centos.sh",
            "ci/test/00_setup_env_mac_cross.sh",
            "ci/test/00_setup_env_win64.sh",
        ):
            setup = git(repository, "show", f"{candidate_tree}:{path}")
            self.assertRegex(setup, r'GOAL="(?:install|deploy)"')
        local = evidence["local_validation"]
        self.assertEqual(local["status"], "pass")
        self.assertEqual(local["exit_code"], 0)
        self.assertEqual(local["tests"], 62)
        self.assertRegex(local["log_sha256"], r"^sha256:[0-9a-f]{64}$")
        self.assertRegex(local["status_file_sha256"], r"^sha256:[0-9a-f]{64}$")
        self.assertFalse(Path(local["log"]).is_absolute())
        self.assertFalse(Path(local["status_file"]).is_absolute())
        self.assertEqual(evidence["limitations"][0]["status"], "known-inherited-failure")
        self.assertFalse(evidence["limitations"][0]["candidate_correction_authorized"])
        self.assertTrue(all(not value for value in evidence["publication"].values()))

    def test_l7_29_4_canonical_lineage_reconstructs_twice_and_is_accepted(self):
        proposal = ROOT / "contrib/roots/replay-29.4-proposal"
        evidence = json.loads((proposal / "acceptance-evidence.json").read_text())
        with tempfile.TemporaryDirectory() as directory:
            repositories = []
            outputs = []
            for index in range(2):
                repository, result = self.l7_canonical_lineage(
                    Path(directory) / f"run-{index}"
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                repositories.append(repository)
                outputs.append(result.stdout)
            self.assertEqual(outputs[0], outputs[1])
            self.assertEqual(
                git(repositories[0], "rev-parse", "HEAD"), self.L7_CANONICAL_COMMIT
            )
            self.assertEqual(
                git(repositories[0], "rev-parse", "HEAD^{tree}"), self.L7_CANDIDATE_TREE
            )
            self.assert_l7_acceptance(evidence, repositories[0])

    def test_l7_29_4_acceptance_rejects_all_negative_cases(self):
        proposal = ROOT / "contrib/roots/replay-29.4-proposal"
        original = json.loads((proposal / "acceptance-evidence.json").read_text())
        topology = json.loads((proposal / "canonical-topology.json").read_text())
        with tempfile.TemporaryDirectory() as directory:
            repository, result = self.l7_canonical_lineage(Path(directory))
            self.assertEqual(result.returncode, 0, result.stderr)

            mutations = []
            for name, mutate in (
                ("omitted outcome", lambda item: item["l4_accounting"].update(outcome_count=39)),
                ("stale evidence", lambda item: item["l6_evidence"].update(source_sha256="sha256:" + "0" * 64)),
                ("unapproved manual", lambda item: item["approval"].update(status="unapproved")),
                ("failed critical gate", lambda item: item["gates"][0].update(status="fail")),
                ("waived critical gate", lambda item: item["gates"][0].update(waiver=True)),
                ("nonreproducible tree", lambda item: item["reproducibility"]["fresh_replay_trees"].pop()),
                ("incorrect Roots identity", lambda item: item["identity_and_release_notes"]["version"].update(minor=5)),
                ("missing release notes", lambda item: item["identity_and_release_notes"].update(release_notes_blob="sha1:" + "0" * 40)),
                ("unexplained incremental difference", lambda item: item["fresh_incremental_comparison"]["unowned_differences"].append("src/validation.cpp")),
            ):
                value = copy.deepcopy(original)
                mutate(value)
                self.l7_refresh_acceptance_digest(value)
                mutations.append((name, value))
            for name, value in mutations:
                with self.subTest(name=name), self.assertRaises(AssertionError):
                    self.assert_l7_acceptance(value, repository)

            topology_mutations = []
            wrong_base = copy.deepcopy(topology)
            wrong_base["base_commit"] = "sha1:" + "0" * 40
            topology_mutations.append(("incorrect base", wrong_base))
            incomplete = copy.deepcopy(topology)
            incomplete["commit_map"].pop()
            topology_mutations.append(("unaccounted commit", incomplete))
            mismatched = copy.deepcopy(topology)
            mismatched["aggregate_diff_digest"] = "sha256:" + "0" * 64
            topology_mutations.append(("manifest aggregate mismatch", mismatched))
            wrong_tree = copy.deepcopy(topology)
            wrong_tree["target_tree"] = "sha1:" + self.L7_CORE_29_4_TREE
            topology_mutations.append(("wrong target tree", wrong_tree))

            merge = subprocess.run(
                ["git", "-C", str(repository), "commit-tree", self.L7_CANDIDATE_TREE,
                 "-p", self.L7_CANONICAL_COMMIT,
                 "-p", self.L7_CORE_29_4_COMMIT],
                input="replay: forbidden merge\n",
                text=True,
                check=True,
                capture_output=True,
                env={
                    **os.environ,
                    "GIT_AUTHOR_NAME": "Roots negative test",
                    "GIT_AUTHOR_EMAIL": "negative@test.invalid",
                    "GIT_AUTHOR_DATE": "@1783509000 +0000",
                    "GIT_COMMITTER_NAME": "Roots negative test",
                    "GIT_COMMITTER_EMAIL": "negative@test.invalid",
                    "GIT_COMMITTER_DATE": "@1783509000 +0000",
                },
            ).stdout.strip()
            merged = copy.deepcopy(topology)
            merged["target_commit"] = "sha1:" + merge
            merged["commit_map"].append({
                "commit": "sha1:" + merge,
                "adaptation_ids": ["roots-roots-tests-test-coverage"],
                "provenance": topology["commit_map"][-1]["provenance"],
            })
            topology_mutations.append(("merge commit", merged))

            for name, value in topology_mutations:
                with self.subTest(name=name), self.assertRaises(AssertionError):
                    self.assert_l7_acceptance(original, repository, value)

    def test_l7_29_4_candidate_evidence_is_schema_valid_and_fresh(self):
        root = ROOT / "contrib/roots/replay-29.4-proposal"
        evidence_path = root / "candidate-evidence-draft.json"
        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "contrib/devtools/roots-semantic-safety.py"),
                "validate-evidence",
                str(evidence_path),
            ],
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

        evidence = json.loads(evidence_path.read_text())
        downstream = json.loads((root / "downstream-unit-outcomes.json").read_text())
        approval = root / "review-approval.json"
        validation = root / "validation-evidence.json"
        self.assertEqual(
            evidence["expected_unit_ids"],
            [item["id"] for item in downstream["outcomes"]],
        )
        self.assertEqual(
            evidence["approvals"][0]["evidence_digest"],
            "sha256:" + hashlib.sha256(approval.read_bytes()).hexdigest(),
        )
        self.assertEqual(
            {item["artifact_digest"] for item in evidence["build_test_platform_matrix"]},
            {"sha256:" + hashlib.sha256(validation.read_bytes()).hexdigest()},
        )
        self.assertFalse(evidence["release_approval"])

    def l7_future_replay(
        self, root: Path, materials_root: Path
    ) -> subprocess.CompletedProcess[str]:
        """Run the L7.3 candidate materials through the public replay CLI."""
        source, state = root / "source", root / "state"
        subprocess.run(["git", "init", str(source)], check=True, capture_output=True)
        fetch = subprocess.run(
            [
                "git", "-C", str(source), "fetch", "--no-tags", str(ROOT),
                "refs/remotes/origin/ci/l7-validation/39a5e302",
            ],
            text=True,
            capture_output=True,
        )
        if fetch.returncode:
            self.skipTest("the disposable hash-locked L7 validation ref is unavailable")
        git(source, "checkout", "--detach", self.L7_CORE_29_4_COMMIT)
        state.mkdir()
        command = [
            sys.executable, str(SCRIPT), "replay", "--repository", str(source),
            "--revision", self.L7_CORE_29_4_COMMIT,
            "--expected-tree", self.L7_CORE_29_4_TREE,
            "--manifest", str(materials_root / "adaptation-manifest-29.3.json"),
            "--materials", str(materials_root / "replay-materials.json"),
            "--materials-root", str(materials_root),
            "--state-directory", str(state), "--apply",
        ]
        return subprocess.run(command, text=True, capture_output=True)

    def test_l7_29_4_replays_twice_to_exact_tree(self):
        """Two clean engine runs must produce identical state and candidate trees."""
        artifacts = []
        materials = ROOT / "contrib/roots/replay-29.4-proposal"
        for _ in range(2):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                result = self.l7_future_replay(root, materials)
                self.assertEqual(result.returncode, 0, result.stderr)
                candidate = root / "state" / "owned-candidate"
                self.assertEqual(git(candidate, "write-tree"), self.L7_CANDIDATE_TREE)
                final_state = root / "state" / "replay-state-0005.json"
                review = root / "review"
                review.mkdir()
                subprocess.run(
                    [sys.executable, str(SCRIPT), "report", "--state", str(final_state),
                     "--output-directory", str(review)],
                    check=True,
                    capture_output=True,
                )
                artifacts.append(tuple(path.read_bytes() for path in (
                    final_state,
                    review / "replay-review.json",
                    review / "replay-review.txt",
                )))
        self.assertEqual(*artifacts)

    def test_l7_29_4_each_material_omission_is_rejected(self):
        """Every material in the real 29.4 replay is mandatory."""
        original = ROOT / "contrib/roots/replay-29.4-proposal"
        records = json.loads((original / "replay-materials.json").read_text())["materials"]
        for omitted in records:
            with self.subTest(omitted=omitted), tempfile.TemporaryDirectory() as directory:
                root, materials = Path(directory), Path(directory) / "materials"
                shutil.copytree(original, materials)
                value = json.loads((materials / "replay-materials.json").read_text())
                del value["materials"][omitted]
                (materials / "replay-materials.json").write_text(json.dumps(value))
                result = self.l7_future_replay(root, materials)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(
                    "replay materials do not exactly cover manifest references",
                    result.stderr,
                )

    def test_l7_29_4_tampered_material_is_rejected(self):
        """Changed material fails its content-addressed replay lock."""
        original = ROOT / "contrib/roots/replay-29.4-proposal"
        with tempfile.TemporaryDirectory() as directory:
            root, materials = Path(directory), Path(directory) / "materials"
            shutil.copytree(original, materials)
            value = json.loads((materials / "replay-materials.json").read_text())
            record = value["materials"]["roots:l7-data-miniupnpc"]
            with (materials / record["source"]).open("ab") as payload:
                payload.write(b"x")
            result = self.l7_future_replay(root, materials)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("data source does not match its lock", result.stderr)

    def test_l7_29_4_digest_valid_unexpected_conflict_is_rejected(self):
        """A newly conflicting but freshly hashed patch cannot become a candidate."""
        original = ROOT / "contrib/roots/replay-29.4-proposal"
        with tempfile.TemporaryDirectory() as directory:
            root, materials = Path(directory), Path(directory) / "materials"
            shutil.copytree(original, materials)
            value = json.loads((materials / "replay-materials.json").read_text())
            record = value["materials"]["roots:l7-clean-patch"]
            patch = materials / record["patch"]
            payload = patch.read_bytes()
            original_context = (
                b"-        ## This issue tracker is only for technical issues related to Bitcoin Core.\n"
            )
            self.assertIn(original_context, payload)
            payload = payload.replace(
                original_context,
                b"-        ## This context is deliberately absent from the locked Core tree.\n",
                1,
            )
            patch.write_bytes(payload)
            record["expected_patch_sha256"] = "sha256:" + hashlib.sha256(payload).hexdigest()
            (materials / "replay-materials.json").write_text(json.dumps(value))
            result = self.l7_future_replay(root, materials)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Git verification failed", result.stderr)

    def l7_core_replay(self, root: Path, materials_root: Path) -> subprocess.CompletedProcess[str]:
        """Run the L7.2 Core-to-Knots layer in a fresh disposable clone."""
        source, state = root / "source", root / "state"
        subprocess.run(
            ["git", "clone", "--no-local", str(ROOT), str(source)],
            check=True,
            capture_output=True,
        )
        git(source, "checkout", "--detach", "99003bed87333f1be51bf3070235591b3a72f007")
        state.mkdir()
        command = [
            sys.executable, str(SCRIPT), "replay", "--repository", str(source),
            "--revision", "99003bed87333f1be51bf3070235591b3a72f007",
            "--expected-tree", "d7910bd5e9335128932f1f848a767d773895c4a4",
            "--manifest", str(materials_root / "adaptation-manifest-29.3.json"),
            "--materials", str(materials_root / "replay-materials.json"),
            "--materials-root", str(materials_root), "--state-directory", str(state),
            "--apply",
        ]
        return subprocess.run(command, text=True, capture_output=True)

    def test_l7_core_knots_tampered_patch_is_rejected(self):
        """A changed forced-binary payload fails the replay digest gate."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            materials = root / "materials"
            shutil.copytree(ROOT / "contrib/roots/replay-core-to-knots-29.3", materials)
            record = next(iter(json.loads((materials / "replay-materials.json").read_text())["materials"].values()))
            with (materials / record["patch"]).open("ab") as patch:
                patch.write(b"x")
            result = self.l7_core_replay(root, materials)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("patch digest does not match its lock", result.stderr)

    def test_l7_core_knots_each_material_omission_is_rejected(self):
        """Every one of the ten selected units is required by the engine."""
        original = ROOT / "contrib/roots/replay-core-to-knots-29.3"
        records = json.loads((original / "replay-materials.json").read_text())["materials"]
        for omitted in records:
            with self.subTest(omitted=omitted), tempfile.TemporaryDirectory() as directory:
                root, materials = Path(directory), Path(directory) / "materials"
                shutil.copytree(original, materials)
                value = json.loads((materials / "replay-materials.json").read_text())
                del value["materials"][omitted]
                (materials / "replay-materials.json").write_text(json.dumps(value))
                result = self.l7_core_replay(root, materials)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("replay materials do not exactly cover manifest references", result.stderr)

    def test_l7_core_knots_out_of_order_tree_lock_is_rejected(self):
        """A tree lock from another sequence position rejects the application."""
        root = ROOT / "contrib/roots/replay-core-to-knots-29.3"
        with tempfile.TemporaryDirectory() as directory:
            materials = Path(directory) / "materials"
            shutil.copytree(root, materials)
            value = json.loads((materials / "replay-materials.json").read_text())
            first = next(iter(value["materials"].values()))
            first["expected_before_tree"] = "0" * 40
            (materials / "replay-materials.json").write_text(json.dumps(value))
            result = self.l7_core_replay(Path(directory), materials)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("patch unit worktree does not match its locked input", result.stderr)

    def test_l7_core_knots_material_is_locked_and_complete(self):
        """Exercise exact Core handoff and two independent immutable stage-B runs."""
        root = ROOT / "contrib/roots/replay-core-to-knots-29.3"
        materials = json.loads((root / "replay-materials.json").read_text())["materials"]
        proposal = json.loads((root / "manual-review-proposal.json").read_text())
        proposal_digest = proposal.pop("digest")
        self.assertEqual(
            proposal_digest,
            "sha256:" + hashlib.sha256(
                json.dumps(proposal, sort_keys=True, separators=(",", ":")).encode()
            ).hexdigest(),
        )
        self.assertEqual(len(proposal["entries"]), len(materials))
        license_basis = (
            "MIT license; COPYING is byte-identical at Core "
            "99003bed87333f1be51bf3070235591b3a72f007 and Knots "
            "99ee26e9df0e63a5d1e0ab6bd46b1862ce67648b; sha256:"
            "7c4a87f43afaf667b4c2187af92ebdd27310a24cec113f973e058e3300a76002"
        )
        self.assertEqual(
            {item["license_basis"] for item in proposal["entries"]},
            {license_basis},
        )
        self.assertEqual(
            {item["patch_sha256"] for item in proposal["entries"]},
            {item["expected_patch_sha256"] for item in materials.values()},
        )
        self.assertEqual(len(materials), 10)
        self.assertEqual(
            list(materials),
            ["l7:" + identifier for identifier in sorted(key.removeprefix("l7:") for key in materials)],
        )
        for material in materials.values():
            payload = (root / material["patch"]).read_bytes()
            self.assertEqual(material["expected_patch_sha256"], "sha256:" + hashlib.sha256(payload).hexdigest())
        provenance = json.loads((root / "provenance-proposal.json").read_text())
        self.assertEqual(provenance["composition"]["stage_a_result_tree"], "sha1:56f97d3a9199c1fb191e1b8a21f2caae3901b7f6")
        self.assertEqual(provenance["composition"]["stage_b_required_tree"], provenance["composition"]["stage_a_result_tree"])
        roots = ROOT / "contrib/roots/replay-29.3-release"
        reports = []
        for _ in range(2):
            with tempfile.TemporaryDirectory() as directory:
                source = Path(directory) / "source"
                state = Path(directory) / "state"
                review = Path(directory) / "review"
                subprocess.run(["git", "clone", "--no-local", str(ROOT), str(source)], check=True, capture_output=True)
                git(source, "checkout", "--detach", "99ee26e9df0e63a5d1e0ab6bd46b1862ce67648b")
                state.mkdir()
                review.mkdir()
                command = [
                    sys.executable, str(SCRIPT), "replay", "--repository", str(source),
                    "--revision", "99ee26e9df0e63a5d1e0ab6bd46b1862ce67648b",
                    "--expected-tree", "56f97d3a9199c1fb191e1b8a21f2caae3901b7f6",
                    "--manifest", str(roots / "adaptation-manifest-29.3.json"),
                    "--materials", str(roots / "replay-materials.json"),
                    "--materials-root", str(roots), "--state-directory", str(state),
                    "--apply",
                ]
                subprocess.run(command, check=True, capture_output=True)
                self.assertEqual(git(state / "owned-candidate", "write-tree"), "a5708dcbf1d2611360fab68fc6a8e504db1ba95d")
                subprocess.run([sys.executable, str(SCRIPT), "report", "--state", str(state / "replay-state-0016.json"), "--output-directory", str(review)], check=True, capture_output=True)
                reports.append(tuple(
                    path.read_bytes()
                    for path in (
                        state / "replay-state-0016.json",
                        review / "replay-review.json",
                        review / "replay-review.txt",
                    )
                ))
        self.assertEqual(*reports)
        with tempfile.TemporaryDirectory() as directory:
            result = self.l7_core_replay(Path(directory), root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                git(Path(directory) / "state" / "owned-candidate", "write-tree"),
                "56f97d3a9199c1fb191e1b8a21f2caae3901b7f6",
            )

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
            states.mkdir()
            review.mkdir()
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

    def test_checked_in_29_3_materials_stop_at_each_manual_boundary(self):
        """The calibrated release cannot be mistaken for an automatic replay."""
        manifest = ROOT / "contrib/roots/adaptation-manifest-29.3.json"
        materials = ROOT / "contrib/roots/replay-29.3/replay-materials.json"
        units = REPLAY.read_manifest_units(manifest)
        envelope = REPLAY.read_replay_materials(materials, units)
        self.assertEqual(len(units), 16)
        self.assertEqual(
            {record["mechanism"] for record in envelope["materials"].values()},
            {"manual"},
        )

    def test_release_specific_29_3_calibration_reconstructs_roots_release(self):
        """L7's replacement scope is locked independently of the L3 manifest."""
        base = "99ee26e9df0e63a5d1e0ab6bd46b1862ce67648b"
        base_tree = "56f97d3a9199c1fb191e1b8a21f2caae3901b7f6"
        target_tree = "a5708dcbf1d2611360fab68fc6a8e504db1ba95d"
        materials_root = ROOT / "contrib/roots/replay-29.3-release"
        artifacts = []
        contract = json.loads((materials_root / "calibration-evidence-contract.json").read_text())
        self.assertEqual(contract["schema_version"], 1)
        digest = contract.pop("digest")
        self.assertEqual(digest, "sha256:" + hashlib.sha256(
            json.dumps(contract, sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest())
        proposal = json.loads((materials_root / "manual-review-proposal.json").read_text())
        expected = [(item["id"], item["mechanism"] == "patch") for item in proposal["entries"]]
        self.assertTrue({name for item in proposal["entries"] for name in item["evidence"]["invariants"]}.issubset({"no-rdts-bip110-enforcement", "core-valid-block-acceptance", "policy-rejection-block-acceptance", "conservative-configurable-defaults", "knots-compatibility", "startup-help-config", "roots-feature-regression"}))
        for _ in range(2):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                source = root / "source"
                state = root / "state"
                review = root / "review"
                subprocess.run(
                    ["git", "clone", "--no-local", str(ROOT), str(source)],
                    check=True, capture_output=True,
                )
                git(source, "checkout", "--detach", base)
                state.mkdir()
                review.mkdir()
                command = [
                    sys.executable, str(SCRIPT), "replay", "--repository", str(source),
                    "--revision", base, "--expected-tree", base_tree, "--manifest",
                    str(materials_root / "adaptation-manifest-29.3.json"), "--materials",
                    str(materials_root / "replay-materials.json"), "--materials-root",
                    str(materials_root), "--state-directory", str(state), "--apply",
                ]
                subprocess.run(command, check=True, capture_output=True, text=True)
                self.assertEqual(git(state / "owned-candidate", "write-tree"), target_tree)
                final_state = state / "replay-state-0016.json"
                subprocess.run(
                    [sys.executable, str(SCRIPT), "report", "--state", str(final_state),
                     "--output-directory", str(review)],
                    check=True, capture_output=True, text=True,
                )
                outcomes = json.loads(final_state.read_text())["completed_units"]
                self.assertEqual(len(outcomes), 16)
                self.assertEqual([(item["unit"], item["outcome"] == "applied") for item in outcomes], expected)
                self.assertEqual(sum(item["outcome"] == "applied" for item in outcomes), 7)
                self.assertEqual(sum(item["outcome"] == "manual" for item in outcomes), 9)
                artifacts.append(tuple(path.read_bytes() for path in (
                    final_state, review / "replay-review.json", review / "replay-review.txt",
                )))
        self.assertEqual(*artifacts)

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
