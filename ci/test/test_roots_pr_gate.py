#!/usr/bin/env python3
"""Focused contract tests for the trusted Roots pull-request gate."""
from __future__ import annotations
import copy
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "contrib/devtools/roots-pr-gate.py"
ACCOUNTING_SCRIPT = ROOT / "contrib/devtools/roots-continuous-accounting.py"
WORKFLOW = ROOT / ".github/workflows/roots-portability.yml"
TOOLS = ROOT / "contrib/devtools"
RECORD = "contrib/roots/continuous-accounting-pr.json"

def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

GATE, ACCOUNTING = load(SCRIPT, "roots_pr_gate"), load(ACCOUNTING_SCRIPT, "roots_accounting")

def git(repository, *args):
    return subprocess.run(["git", "-C", str(repository), *args], check=True, text=True, capture_output=True).stdout.strip()

def entry(path, kind, digest, disposition="update"):
    risk = "high" if path in {".github/workflows/roots-portability.yml", "contrib/devtools/roots-pr-gate.py"} else ("critical" if path == "src/validation.cpp" else ("medium" if path.startswith(("src/", "generated/", "ci/test/")) else "low"))
    tests = "ci/test/test_roots_pr_gate.py" if path in {".github/workflows/roots-portability.yml", "contrib/devtools/roots-pr-gate.py"} or path.startswith("ci/test/") else ("doc test" if path.startswith("doc/") else ("generator test" if path.startswith("generated/") else ("functional test" if path == "src/validation.cpp" else "ci/test/test_roots_pr_gate.py")))
    return {"path": path, "kind": kind, "digest": digest, "disposition": disposition,
            "adaptation": "roots-post-methodology-pr-gate-v1", "risk": risk,
            "tests": tests, "dependencies": "none", "provenance": "reviewed",
            "replay_impact": "replay", "scope": path, "rationale": "specific PR atom"}

def run_gate(repository, record, base, candidate):
    candidate_root = record.parents[2]
    return GATE.gate(repository, record, candidate_root / "contrib/roots/adaptation-manifest-29.3.json", candidate_root / "contrib/roots/methodology-v1.json", record.parent / "post-methodology-adaptations.json", base, candidate, TOOLS)

class RootsPrGateTest(unittest.TestCase):
    def repositories(self, paths):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root, base_repo = Path(temporary.name), Path(temporary.name) / "base"
        subprocess.run(["git", "init", str(base_repo)], check=True, capture_output=True)
        git(base_repo, "config", "user.email", "test@example.invalid")
        git(base_repo, "config", "user.name", "Test")
        for path in paths:
            target = base_repo / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("base\n")
        git(base_repo, "add", ".")
        git(base_repo, "commit", "-m", "base")
        base = git(base_repo, "rev-parse", "HEAD")
        fork, trusted = root / "fork", root / "trusted"
        for clone in (fork, trusted): subprocess.run(["git", "clone", str(base_repo), str(clone)], check=True, capture_output=True)
        git(fork, "config", "user.email", "test@example.invalid")
        git(fork, "config", "user.name", "Test")
        for path in paths: (fork / path).write_text("candidate\n")
        git(fork, "commit", "-am", "candidate data")
        changed = git(fork, "rev-parse", "HEAD")
        owners = {path: unit["id"] for unit in json.loads((ROOT / "contrib/roots/adaptation-manifest-29.3.json").read_text())["units"] for path in unit["touched"]["paths"]}
        changes = [entry(*atom, "exempt" if atom[0].startswith(("doc/", "README")) else "update") for atom in sorted(ACCOUNTING.atoms(fork, base, changed))]
        for item in changes:
            if item["path"] in owners:
                item["adaptation"] = owners[item["path"]]
        record = fork / RECORD
        record.parent.mkdir(parents=True, exist_ok=True)
        record.write_text(json.dumps({"schema_version": 1, "changes": changes}, sort_keys=True))
        git(fork, "add", RECORD)
        git(fork, "commit", "-m", "account atoms")
        registry_paths = sorted(path for path in paths if path not in owners)
        (fork / "contrib/roots/post-methodology-adaptations.json").write_text(json.dumps({"schema_version": 1, "units": [{"id": "roots-post-methodology-pr-gate-v1", "paths": registry_paths}]}))
        methodology = json.loads((ROOT / "contrib/roots/methodology-v1.json").read_text())
        for item in methodology["frozen_inputs"]:
            source, destination = ROOT / item["path"], fork / item["path"]
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(source.read_bytes())
        for name in ("adaptation-manifest-29.3.json", "adaptation-manifest.schema.json", "methodology-v1.json"):
            destination = fork / "contrib/roots" / name
            destination.write_bytes((ROOT / "contrib/roots" / name).read_bytes())
        return trusted, fork, base, git(fork, "rev-parse", "HEAD")

    def transfer(self, trusted, fork, candidate):
        bundle = trusted.parent / "candidate.bundle"
        git(fork, "update-ref", "refs/roots-pr/candidate", candidate)
        git(fork, "bundle", "create", str(bundle), "refs/roots-pr/candidate")
        git(trusted, "fetch", "--no-tags", str(bundle), "refs/roots-pr/candidate:refs/roots-pr/candidate")

    def test_offline_fork_matrix_is_exactly_accounted(self):
        paths = ["README.md", "doc/guide.md", "generated/output.txt", "src/validation.cpp", "src/util/tool.cpp"]
        trusted, fork, base, candidate = self.repositories(paths)
        self.transfer(trusted, fork, candidate)
        report = run_gate(trusted, fork / RECORD, base, candidate)
        self.assertEqual(report["atom_count"], len(paths))
        self.assertEqual({x["path"] for x in report["changes"]}, set(paths))
        self.assertEqual(next(x["risk"] for x in report["changes"] if x["path"] == "src/validation.cpp"), "critical")

    def test_missing_extra_stale_wrong_owner_and_embargo_fail_closed(self):
        trusted, fork, base, candidate = self.repositories(["doc/guide.md", "src/validation.cpp"])
        self.transfer(trusted, fork, candidate)
        record = json.loads((fork / RECORD).read_text())
        for mutate in (lambda v: v["changes"].pop(), lambda v: v["changes"].append(copy.deepcopy(v["changes"][0])), lambda v: v["changes"][0].__setitem__("digest", "sha256:" + "0" * 64), lambda v: v["changes"][0].__setitem__("adaptation", "")):
            invalid = copy.deepcopy(record)
            mutate(invalid)
            (fork / RECORD).write_text(json.dumps(invalid))
            with self.assertRaises(GATE.GateError): run_gate(trusted, fork / RECORD, base, candidate)
        embargoed = copy.deepcopy(record)
        item = embargoed["changes"][0]
        embargoed["changes"][0] = {key: item[key] for key in ("path", "kind", "digest", "disposition", "adaptation")}
        embargoed["changes"][0].update({"disposition":"embargoed", "tracking_reference":"SEC-2026-001", "reconcile_by":"2026-12-31", "reconciliation_state":"pending"})
        observed = ACCOUNTING.atoms(trusted, base, candidate) - {atom for atom in ACCOUNTING.atoms(trusted, base, candidate) if atom[0] == RECORD}
        with self.assertRaises(ValueError): ACCOUNTING.validate(embargoed, observed, public_release=True)

    def test_candidate_replacements_never_execute_and_missing_object_fails(self):
        trusted, fork, base, candidate = self.repositories(["doc/guide.md"])
        with self.assertRaises(GATE.GateError): run_gate(trusted, fork / RECORD, base, candidate)
        self.transfer(trusted, fork, candidate)
        marker = fork / "executed"
        payload = f"from pathlib import Path\nPath({str(marker)!r}).write_text('bad')\nraise RuntimeError()\n"
        for name in ("roots-continuous-accounting.py", "roots-pr-gate.py"):
            target = fork / "contrib/devtools" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(payload)
        run_gate(trusted, fork / RECORD, base, candidate)
        self.assertFalse(marker.exists())

    def test_workflow_is_pinned_read_only_and_offline(self):
        workflow = WORKFLOW.read_text()
        for required in ("pull_request:", "contents: read", "path: trusted-base", "path: candidate", "fetch-depth: 0", "path: bootstrap-validator", "ref: 6f85dfb728ca0976658e0fbd26101e7df5e35a14", "validator_root=trusted-base", "validator_root=bootstrap-validator", "bundle create", "--record candidate/contrib/roots/continuous-accounting-pr.json", 'test "$(wc -c < roots-portability-pr-report.json)" -le 131072'): self.assertIn(required, workflow)
        for forbidden in ("pull_request_target", "contents: write", "actions/cache", "save-caches", "secrets.", "github.token", "fetch --no-tags origin", "python3 candidate/"): self.assertNotIn(forbidden, workflow)
        actions = re.findall(r"^\s*uses:\s*([^\s#]+)", workflow, re.MULTILINE)
        self.assertTrue(actions and all(re.fullmatch(r"actions/checkout@[0-9a-f]{40}", action) for action in actions))

    def test_manifest_schema_methodology_and_plausible_wrong_owner_fail_closed(self):
        trusted, fork, base, candidate = self.repositories(["doc/guide.md"])
        self.transfer(trusted, fork, candidate)
        root = fork / "contrib/roots"
        manifest = root / "adaptation-manifest-29.3.json"
        methodology = root / "methodology-v1.json"
        schema = root / "adaptation-manifest.schema.json"
        for path in (manifest, methodology, schema): path.write_bytes((ROOT / "contrib/roots" / path.name).read_bytes())
        registry = root / "post-methodology-adaptations.json"
        record = json.loads((fork / RECORD).read_text())
        record["changes"][0]["adaptation"] = "roots-roots-build-ci-build-or-release"
        (fork / RECORD).write_text(json.dumps(record))
        with self.assertRaises(Exception): GATE.gate(trusted, fork / RECORD, manifest, methodology, registry, base, candidate, TOOLS)
        record["changes"][0]["adaptation"] = "roots-post-methodology-pr-gate-v1"
        (fork / RECORD).write_text(json.dumps(record))
        schema.write_text("{}")
        with self.assertRaises(Exception): GATE.gate(trusted, fork / RECORD, manifest, methodology, registry, base, candidate, TOOLS)
        schema.write_bytes((ROOT / "contrib/roots/adaptation-manifest.schema.json").read_bytes())
        methodology.write_text("{}")
        with self.assertRaises(Exception): GATE.gate(trusted, fork / RECORD, manifest, methodology, registry, base, candidate, TOOLS)

    def test_path_policies_and_registry_ambiguity_fail_closed(self):
        paths = [".github/workflows/roots-portability.yml", "ci/test/example.py", "contrib/devtools/roots-pr-gate.py", "doc/guide.md", "generated/output.txt", "src/util/tool.cpp", "src/validation.cpp"]
        trusted, fork, base, candidate = self.repositories(paths)
        self.transfer(trusted, fork, candidate)
        original = json.loads((fork / RECORD).read_text())
        mutations = [
            ("doc/guide.md", "risk", "high"),
            ("doc/guide.md", "tests", "unit test"),
            ("generated/output.txt", "risk", "low"),
            ("generated/output.txt", "tests", "unit test"),
            ("src/util/tool.cpp", "risk", "low"),
            ("src/util/tool.cpp", "tests", "unit only"),
            ("src/validation.cpp", "risk", "low"),
            ("src/validation.cpp", "tests", "unit test"),
            (".github/workflows/roots-portability.yml", "risk", "low"),
            (".github/workflows/roots-portability.yml", "tests", "x"),
            ("ci/test/example.py", "risk", "low"),
            ("ci/test/example.py", "tests", "x"),
            ("contrib/devtools/roots-pr-gate.py", "risk", "low"),
            ("contrib/devtools/roots-pr-gate.py", "tests", "x"),
        ]
        for path, field, value in mutations:
            record = copy.deepcopy(original)
            next(item for item in record["changes"] if item["path"] == path)[field] = value
            (fork / RECORD).write_text(json.dumps(record))
            with self.subTest(path=path, field=field, value=value):
                with self.assertRaises(GATE.GateError): run_gate(trusted, fork / RECORD, base, candidate)
        (fork / RECORD).write_text(json.dumps(original))
        registry_path = fork / "contrib/roots/post-methodology-adaptations.json"
        registry = json.loads(registry_path.read_text())
        for invalid in (
            {"schema_version": 1, "units": [{"id": "roots-roots-build-ci-build-or-release", "paths": ["fresh-id-collision"]}]},
            {"schema_version": 1, "units": [{"id": "roots-post-methodology-pr-gate-v1", "paths": ["fresh-a"]}, {"id": "roots-post-methodology-pr-gate-v1", "paths": ["fresh-b"]}]},
            {"schema_version": 1, "units": [{"id": "roots-first", "paths": ["fresh-shared"]}, {"id": "roots-second", "paths": ["fresh-shared"]}]},
            {"schema_version": 1, "units": [{"id": "roots-post-methodology-pr-gate-v1", "paths": ["../escape"]}]},
            {"schema_version": 1, "units": [{"id": "invalid_id", "paths": ["fresh-id"]}]},
            {"schema_version": 1, "units": [{"id": "roots-../escape", "paths": ["fresh-id"]}]},
            {"schema_version": 1, "units": [{"id": "roots-*", "paths": ["fresh-id"]}]},
            {"schema_version": 1, "units": [{"id": "roots-z", "paths": ["z-new"]}, {"id": "roots-a", "paths": ["a-new"]}]},
        ):
            registry_path.write_text(json.dumps(invalid))
            with self.assertRaises(GATE.GateError): run_gate(trusted, fork / RECORD, base, candidate)

    def test_registry_bootstrap_path_is_exact(self):
        manifest = {"units": []}
        record = {"changes": []}
        valid = {"schema_version": 1, "units": [{"id": "roots-post-methodology-pr-gate-v1", "paths": ["contrib/roots/post-methodology-adaptations.json"]}]}
        GATE.validate_ownership(record, manifest, valid)
        manifest = {"units": [{"id": "owned", "touched": {"paths": ["contrib/roots/other.json"]}}]}
        for path in ("contrib/roots/other.json", "../post-methodology-adaptations.json", "/post-methodology-adaptations.json"):
            invalid = {"schema_version": 1, "units": [{"id": "roots-post-methodology-pr-gate-v1", "paths": [path]}]}
            with self.assertRaises(GATE.GateError):
                GATE.validate_ownership(record, manifest, invalid)

    def test_candidate_control_symlinks_and_frozen_inputs_fail_closed(self):
        trusted, fork, base, candidate = self.repositories(["doc/guide.md"])
        self.transfer(trusted, fork, candidate)
        root = fork / "contrib/roots"
        for name in ("adaptation-manifest-29.3.json", "methodology-v1.json", "adaptation-manifest.schema.json"):
            target = root / name
            target.write_bytes((ROOT / "contrib/roots" / name).read_bytes())
        registry = root / "post-methodology-adaptations.json"
        for name in ("adaptation-manifest-29.3.json", "methodology-v1.json", "adaptation-manifest.schema.json", "post-methodology-adaptations.json"):
            target = root / name
            saved = target.read_bytes()
            target.unlink()
            target.symlink_to(ROOT / "contrib/roots" / name)
            with self.assertRaises(GATE.GateError): GATE.gate(trusted, fork / RECORD, root / "adaptation-manifest-29.3.json", root / "methodology-v1.json", registry, base, candidate, TOOLS)
            target.unlink()
            target.write_bytes(saved)
        manifest = root / "adaptation-manifest-29.3.json"
        value = json.loads(manifest.read_text())
        value["units"] = list(reversed(value["units"]))
        manifest.write_text(json.dumps(value))
        with self.assertRaises(GATE.GateError): GATE.gate(trusted, fork / RECORD, manifest, root / "methodology-v1.json", registry, base, candidate, TOOLS)

    def test_candidate_parent_symlinks_fail_closed(self):
        trusted, fork, base, candidate = self.repositories(["doc/guide.md"])
        self.transfer(trusted, fork, candidate)
        root = fork / "contrib/roots"
        registry = root / "post-methodology-adaptations.json"
        external = fork.parent / "external-contrib"
        (fork / "contrib").rename(external)
        (fork / "contrib").symlink_to(external, target_is_directory=True)
        with self.assertRaises(GATE.GateError): GATE.gate(trusted, fork / RECORD, root / "adaptation-manifest-29.3.json", root / "methodology-v1.json", registry, base, candidate, TOOLS)
        (fork / "contrib").unlink()
        external.rename(fork / "contrib")
        frozen_parent = fork / "contrib/roots/replay-29.3-release"
        external = fork.parent / "external-frozen"
        frozen_parent.rename(external)
        frozen_parent.symlink_to(external, target_is_directory=True)
        with self.assertRaises(GATE.GateError): GATE.gate(trusted, fork / RECORD, root / "adaptation-manifest-29.3.json", root / "methodology-v1.json", registry, base, candidate, TOOLS)

    def test_oversized_atoms_and_linked_record_fail_closed(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        linked = Path(temporary.name) / "record.json"
        linked.symlink_to(ROOT / RECORD)
        with self.assertRaises(GATE.GateError): run_gate(ROOT, linked, "0" * 40, "1" * 40)
        original = GATE.load_validator
        class Oversized:
            @staticmethod
            def atoms(*_): return {(f"path-{i}", "modify", "sha256:" + "0" * 64) for i in range(GATE.MAX_CHANGED_FILES + 1)}
        self.addCleanup(setattr, GATE, "load_validator", original)
        GATE.load_validator = lambda path, name: Oversized if name == "roots_continuous_accounting" else original(path, name)
        with self.assertRaises(GATE.GateError):
            run_gate(ROOT, ROOT / RECORD, "0" * 40, "1" * 40)

    def test_bounded_atom_report_fails_at_report_limit(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name) / "candidate"
        methodology = json.loads((ROOT / "contrib/roots/methodology-v1.json").read_text())
        for item in methodology["frozen_inputs"]:
            source, destination = ROOT / item["path"], root / item["path"]
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(source.read_bytes())
        for name in ("adaptation-manifest-29.3.json", "adaptation-manifest.schema.json", "methodology-v1.json"):
            destination = root / "contrib/roots" / name
            destination.write_bytes((ROOT / "contrib/roots" / name).read_bytes())
        registry_path = root / "contrib/roots/post-methodology-adaptations.json"
        record_path = root / "contrib/roots/record.json"
        paths = [f"generated/output-{i:04d}.txt" for i in range(500)]
        registry_path.write_text(json.dumps({"schema_version": 1, "units": [{"id": "roots-post-methodology-pr-gate-v1", "paths": paths}]}))
        changes = [entry(path, "modify", "sha256:" + f"{index:064x}") for index, path in enumerate(paths)]
        record_path.write_text(json.dumps({"schema_version": 1, "changes": changes}))
        original = GATE.load_validator
        class LargeAccounting:
            @staticmethod
            def atoms(*_): return {(path, "modify", "sha256:" + f"{index:064x}") for index, path in enumerate(paths)}
            @staticmethod
            def validate(value, observed): return None
        self.addCleanup(setattr, GATE, "load_validator", original)
        GATE.load_validator = lambda path, name: LargeAccounting if name == "roots_continuous_accounting" else original(path, name)
        with self.assertRaisesRegex(GATE.GateError, "report exceeds"):
            GATE.gate(root, record_path, root / "contrib/roots/adaptation-manifest-29.3.json", root / "contrib/roots/methodology-v1.json", registry_path, "0" * 40, "1" * 40, TOOLS)

if __name__ == "__main__": unittest.main()
