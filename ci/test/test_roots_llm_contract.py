#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Clean-repository and mutation tests for the self-contained LLM contract."""

from __future__ import annotations

import copy
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "contrib/devtools/roots-llm-contract.py"
CONTRACT = ROOT / "contrib/roots/llm-execution-contract-v1.json"
HUMAN = ROOT / "contrib/roots/llm-execution-contract-v1.md"
VALIDATION_REF = "refs/remotes/origin/ci/l7-validation/39a5e302"


def run(command, *, check=True):
    result = subprocess.run([str(item) for item in command], check=False, text=True, capture_output=True)
    if check and result.returncode:
        raise AssertionError(
            f"command failed ({result.returncode}): {' '.join(str(item) for item in command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def git(repository, *arguments):
    return run(["git", "-C", repository, *arguments]).stdout.strip()


def load_tool():
    spec = importlib.util.spec_from_file_location("roots_llm_contract", SCRIPT)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


TOOL = load_tool()


class RootsLlmContractTest(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        self.contract = json.loads(CONTRACT.read_text(encoding="utf-8"))

    def assert_contract_error(self, value, code):
        with self.assertRaisesRegex(TOOL.ContractError, rf"^{code}:"):
            TOOL.validate_contract(value, ROOT)

    def isolated_contract_root(self, destination):
        repository = destination / "contract-root"
        shutil.copytree(ROOT / "contrib/roots", repository / "contrib/roots")
        tools = repository / "contrib/devtools"
        tools.mkdir(parents=True)
        shutil.copy2(SCRIPT, tools / SCRIPT.name)
        shutil.copy2(ROOT / "contrib/devtools/roots-replay.py", tools / "roots-replay.py")
        return repository

    def test_contract_is_versioned_bounded_and_independently_validated(self):
        result = run([sys.executable, SCRIPT, "validate", CONTRACT])
        self.assertIn(self.contract["digest"], result.stdout)
        self.assertEqual(self.contract["contract_version"], "1.0.0")
        self.assertEqual(len(self.contract["adaptations"]), 16)
        self.assertEqual(len(self.contract["context_packets"]), 16)
        self.assertEqual(
            [item["id"] for item in self.contract["adaptations"]],
            [item["id"] for item in self.contract["context_packets"]],
        )
        self.assertTrue(all(len(item["context_paths"]) <= 6 for item in self.contract["context_packets"]))
        self.assertTrue(all(item["scope"]["path_count"] <= 300 for item in self.contract["context_packets"]))
        serialized = CONTRACT.read_text(encoding="utf-8") + HUMAN.read_text(encoding="utf-8")
        for forbidden in ("BEGIN PRIVATE KEY", "ghp_", "/home/", "Authorization: Bearer"):
            self.assertNotIn(forbidden, serialized)
        for required in (
            'verify-tag v29.3', 'verify-tag v29.4', "offline-locked-mirror",
            "first-parent", "accept", "rewrite", "reject", "defer", "upstream",
            "llm-safe-stop.json", "verified Bitcoin Core tag",
        ):
            self.assertIn(required, HUMAN.read_text(encoding="utf-8"))

    def test_mutated_contracts_fail_with_precise_deterministic_diagnostics(self):
        mutations = []

        value = copy.deepcopy(self.contract)
        value["inputs"]["core_releases"][1]["tag"] = "v29.4-mutated"
        mutations.append((value, "E_TAG_LOCK"))

        value = copy.deepcopy(self.contract)
        value["inputs"]["core_releases"][1]["tree"] = "0" * 40
        mutations.append((value, "E_HASH_LOCK"))

        value = copy.deepcopy(self.contract)
        value["adaptations"][0], value["adaptations"][1] = value["adaptations"][1], value["adaptations"][0]
        mutations.append((value, "E_ORDER"))

        value = copy.deepcopy(self.contract)
        value["adaptations"][0]["dependencies"] = [value["adaptations"][1]["id"]]
        mutations.append((value, "E_DEPENDENCY"))

        value = copy.deepcopy(self.contract)
        value["context_packets"][0]["context_paths"][0] = "README.md"
        mutations.append((value, "E_PATH"))

        value = copy.deepcopy(self.contract)
        value["generators"][0]["inputs"].append("ambient build state")
        mutations.append((value, "E_GENERATOR"))

        value = copy.deepcopy(self.contract)
        value["expected_results"]["canonical_29_4_tree"] = "0" * 40
        mutations.append((value, "E_EXPECTED"))

        value = copy.deepcopy(self.contract)
        value["context_packets"][0]["manual_approval"]["question"] = "Use judgment."
        mutations.append((value, "E_MANUAL"))

        for mutated, code in mutations:
            for _ in range(2):
                self.assert_contract_error(mutated, code)

    def test_cli_rejects_malformed_nested_containers_and_json_without_tracebacks(self):
        mutations = []
        for mutate, code in (
            (lambda value: value["inputs"]["core_releases"].__setitem__(0, 7), "E_TAG_LOCK"),
            (lambda value: value["inputs"]["versioned_records"].__setitem__(0, 7), "E_INPUT"),
            (lambda value: value["allowed_remotes"].__setitem__(0, 7), "E_REMOTE"),
            (lambda value: value["adaptations"].__setitem__(0, 7), "E_ORDER"),
            (lambda value: value["context_packets"].__setitem__(0, 7), "E_CONTEXT"),
            (lambda value: value["schemas"].__setitem__("evidence_bundle", 7), "E_SCHEMA"),
        ):
            malformed = copy.deepcopy(self.contract)
            mutate(malformed)
            mutations.append((malformed, code))

        with tempfile.TemporaryDirectory() as temporary:
            root = self.isolated_contract_root(Path(temporary))
            contract = root / "contrib/roots/llm-execution-contract-v1.json"
            for malformed, code in mutations:
                diagnostics = []
                for _ in range(2):
                    contract.write_text(json.dumps(malformed, sort_keys=True), encoding="utf-8")
                    result = run([sys.executable, SCRIPT, "validate", contract], check=False)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertNotIn("Traceback", result.stderr)
                    self.assertIn(f"roots-llm-contract: {code}:", result.stderr)
                    diagnostics.append(result.stderr)
                self.assertEqual(*diagnostics)

            for payload in (b"{", b"\xff", b'{"schema_version":1,"schema_version":1}'):
                diagnostics = []
                for _ in range(2):
                    contract.write_bytes(payload)
                    result = run([sys.executable, SCRIPT, "validate", contract], check=False)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertNotIn("Traceback", result.stderr)
                    self.assertIn("roots-llm-contract: E_JSON:", result.stderr)
                    diagnostics.append(result.stderr)
                self.assertEqual(*diagnostics)

    def test_malformed_contract_rehearsal_writes_deterministic_safe_stop(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            root = self.isolated_contract_root(work)
            contract = root / "contrib/roots/llm-execution-contract-v1.json"
            for field, code in (("core_releases", "E_TAG_LOCK"), ("context_packets", "E_CONTEXT")):
                malformed = copy.deepcopy(self.contract)
                if field == "core_releases":
                    malformed["inputs"][field][0] = 7
                else:
                    malformed[field][0] = 7
                stops = []
                for run_number in (1, 2):
                    contract.write_text(json.dumps(malformed, sort_keys=True), encoding="utf-8")
                    output = work / f"safe-stop-{field}-{run_number}"
                    result = run([
                        sys.executable, SCRIPT, "rehearse", contract,
                        "--repository", ROOT, "--output-directory", output,
                        "--offline-locked-mirror",
                    ], check=False)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertNotIn("Traceback", result.stderr)
                    self.assertIn(f"roots-llm-contract: {code}:", result.stderr)
                    stop = output / "llm-safe-stop.json"
                    value = json.loads(stop.read_text(encoding="utf-8"))
                    self.assertEqual(value["code"], code)
                    self.assertEqual(value["status"], "safe-stop")
                    stops.append(stop.read_bytes())
                self.assertEqual(*stops)

    def clone_locked_source(self, destination):
        run(["git", "clone", "--quiet", "--no-local", ROOT, destination])
        git(destination, "fetch", "--quiet", "--no-tags", ROOT, f"{VALIDATION_REF}:{VALIDATION_REF}")

    def test_two_isolated_clean_repositories_produce_identical_evidence(self):
        results = []
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            for run_number in (1, 2):
                source = work / f"source-{run_number}"
                output = work / f"output-{run_number}"
                self.clone_locked_source(source)
                tags_before = git(source, "for-each-ref", "--format=%(refname) %(objectname)", "refs/tags")
                head_before = git(source, "rev-parse", "HEAD")
                run([
                    sys.executable, SCRIPT, "rehearse", CONTRACT,
                    "--repository", source, "--output-directory", output,
                    "--offline-locked-mirror",
                ])
                evidence = json.loads((output / "llm-evidence-bundle.json").read_text(encoding="utf-8"))
                report = json.loads((output / "llm-rehearsal-report.json").read_text(encoding="utf-8"))
                self.assertEqual(evidence["trees"], {
                    "core_to_knots_29_3": "56f97d3a9199c1fb191e1b8a21f2caae3901b7f6",
                    "roots_29_3": "a5708dcbf1d2611360fab68fc6a8e504db1ba95d",
                    "core_29_4": "39a5e30207a09962e78ae81c24cc65b1e478ef90",
                })
                self.assertEqual(len(evidence["commit_to_adaptation_map"]), 16)
                self.assertEqual(report["core_29_4"]["target_commit"], "cbc88cff9b35b95a549c0313e424e13093fcd6a1")
                self.assertEqual(report["status"], "passed")
                self.assertEqual(report["mode"], "offline-locked-mirror")
                self.assertFalse(report["tag_state_changed"])
                self.assertEqual(git(source, "rev-parse", "HEAD"), head_before)
                self.assertEqual(git(source, "for-each-ref", "--format=%(refname) %(objectname)", "refs/tags"), tags_before)
                results.append(((output / "llm-evidence-bundle.json").read_bytes(), (output / "llm-rehearsal-report.json").read_bytes()))
        self.assertEqual(results[0], results[1])

    def test_owning_runner_writes_deterministic_safe_stop(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            source = work / "source"
            output = work / "output"
            self.clone_locked_source(source)
            readme = source / "README.md"
            readme.write_text(readme.read_text(encoding="utf-8") + "dirty\n", encoding="utf-8")
            result = run([
                sys.executable, SCRIPT, "rehearse", CONTRACT,
                "--repository", source, "--output-directory", output,
                "--offline-locked-mirror",
            ], check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("E_DIRTY: rehearsal repository must be clean", result.stderr)
            safe_stop = json.loads((output / "llm-safe-stop.json").read_text(encoding="utf-8"))
            self.assertEqual(safe_stop, {
                "schema_version": 1,
                "status": "safe-stop",
                "code": "E_DIRTY",
                "action": "preserve evidence and request review",
                "resumable": True,
                "contract_digest": self.contract["digest"],
            })


if __name__ == "__main__":
    unittest.main()
