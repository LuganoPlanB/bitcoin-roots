#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.

import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "contrib/devtools/roots-lineage.py"
LEDGER = ROOT / "contrib/roots/lineage-ledger.json"
CLASSIFICATION = ROOT / "contrib/roots/roots-knots-29.3-classification.json"


class RootsLineageTest(unittest.TestCase):
    def run_tool(self, *args, cwd=ROOT, check=True):
        return subprocess.run(["python3", SCRIPT, *args], cwd=cwd, text=True,
                              capture_output=True, check=check)

    def init_release(self, parent, name, tag, content):
        path = parent / name
        subprocess.run(["git", "init", path], check=True, capture_output=True)
        subprocess.run(["git", "-C", path, "config", "user.name", "Roots test"], check=True)
        subprocess.run(["git", "-C", path, "config", "user.email", "roots@test.invalid"], check=True)
        subprocess.run(["git", "-C", path, "remote", "add", "origin", f"https://example.invalid/{name}.git"], check=True)
        (path / "source.txt").write_text(content, encoding="utf-8")
        subprocess.run(["git", "-C", path, "add", "source.txt"], check=True)
        subprocess.run(["git", "-C", path, "commit", "-m", "fixture"], check=True, capture_output=True)
        subprocess.run(["git", "-C", path, "tag", "-a", tag, "-m", tag], check=True)
        return path

    def fixture_ledger(self, parent):
        releases = []
        for name, project, tag in (("core", "core", "v1.0"), ("knots", "knots", "v1.0.knots")):
            repository = self.init_release(parent, name, tag, "same tree\n")
            def rev(expression):
                return subprocess.run(["git", "-C", repository, "rev-parse", expression], text=True,
                                      capture_output=True, check=True).stdout.strip()
            releases.append({
                "id": f"{project}-{tag.removeprefix('v')}",
                "project": project,
                "release_label": tag.removeprefix("v"),
                "repository": f"https://example.invalid/{name}.git",
                "tag": tag,
                "tag_ref": f"refs/tags/{tag}",
                "resolution_status": "resolved",
                "retrieval": "local-fixture",
                "refspec": f"refs/tags/{tag}:refs/tags/{tag}",
                "tag_object": "sha1:" + rev(f"{tag}^{{tag}}"),
                "peeled_commit": "sha1:" + rev(f"{tag}^{{commit}}"),
                "tree": "sha1:" + rev(f"{tag}^{{tree}}"),
                "signature": "unsigned",
            })
        releases[1]["depends_on"] = [releases[0]["id"]]
        return {
            "schema_version": 1,
            "object_format": "sha1",
            "releases": releases,
            "fork_starts": [],
            "claims": [{
                "id": "fixture-tree", "left": releases[0]["id"], "right": releases[1]["id"],
                "level": "complete_tree", "status": "same",
                "scope": ["all Git tree entries"], "exclusions": [],
                "command": ["git", "diff-tree", "--no-commit-id"],
                "evidence_digest": "sha1:" + "1" * 40,
            }],
            "exceptions": [],
        }

    def write(self, path, value):
        path.write_text(json.dumps(value, sort_keys=True, indent=2) + "\n", encoding="utf-8")

    def test_checked_in_ledger_and_schema_are_valid(self):
        self.run_tool("validate", LEDGER)
        schema = json.loads((ROOT / "contrib/roots/lineage-ledger.schema.json").read_text(encoding="utf-8"))
        self.assertEqual(schema["properties"]["schema_version"]["const"], 1)
        ledger = json.loads(LEDGER.read_text(encoding="utf-8"))
        core_release = next(release for release in ledger["releases"] if release["id"] == "core-29.3")
        knots_release = next(release for release in ledger["releases"] if release["id"] == "knots-29.3.knots20260507")
        self.assertEqual((core_release["signature"], core_release["trust_status"]), ("unverified", "unverified"))
        self.assertEqual((knots_release["signature"], knots_release["trust_status"]), ("unverified", "unverified"))
        roots_release = next(release for release in ledger["releases"] if release["id"] == "roots-29.3-roots.1")
        self.assertEqual(roots_release["resolution_status"], "resolved")
        self.assertEqual(roots_release["tag_object"], "sha1:9b083fbac340b58ccf23c70d01958d62cd7eb79b")
        self.assertEqual(roots_release["peeled_commit"], "sha1:42098b53c57fb6818736c7b6732fa84ffe6ad391")
        self.assertEqual(roots_release["tree"], "sha1:a5708dcbf1d2611360fab68fc6a8e504db1ba95d")
        self.assertEqual((roots_release["signature"], roots_release["trust_status"]), ("unsigned", "not-available"))
        self.assertEqual({claim["status"] for claim in ledger["claims"]}, {"different"})
        self.assertEqual({claim["level"] for claim in ledger["claims"]}, {"complete_tree", "maintained_source", "generated_release_artifact", "build_input"})
        classification = json.loads(CLASSIFICATION.read_text(encoding="utf-8"))
        self.assertEqual(classification["schema_version"], 1)
        self.assertEqual(len(classification["records"]), 188)
        self.assertEqual(len({record["path"] for record in classification["records"]}), 188)
        self.assertEqual(classification["evidence_digest"], "sha1:b293eefeaaf6e71c023a999a46e739d095c47ad1")
        self.assertEqual(sum("hunk_digest" in record for record in classification["records"]), 145)
        self.assertEqual(sum(record.get("non_text", False) for record in classification["records"]), 43)
        manual = [record for record in classification["records"] if record["classification"]["category"] == "manual-invariant"]
        self.assertEqual([record["path"] for record in manual], ["src/init.cpp", "src/kernel/warning.h", "src/validation.cpp"])
        fork_start = ledger["fork_starts"][0]
        self.assertEqual(fork_start["commit"], "sha1:07580114c35e870e242621316ec8cd051a938787")
        parent = subprocess.run(["git", "show", "-s", "--format=%P", "07580114c35e870e242621316ec8cd051a938787"], text=True, capture_output=True, check=True).stdout.strip()
        tree = subprocess.run(["git", "rev-parse", "07580114c35e870e242621316ec8cd051a938787^{tree}"], text=True, capture_output=True, check=True).stdout.strip()
        self.assertEqual(fork_start["parent"], f"sha1:{parent}")
        self.assertEqual(fork_start["tree"], f"sha1:{tree}")
        raw = subprocess.run(["git", "diff", "--raw", "--no-renames", "--no-ext-diff", parent, fork_start["commit"].removeprefix("sha1:")], capture_output=True, check=True).stdout
        status_lines = raw.splitlines()
        self.assertEqual(fork_start["path_count"], len(status_lines))
        self.assertEqual(fork_start["status_counts"], {"added": 1, "deleted": 1, "modified": 38})
        self.assertEqual(fork_start["evidence_digest"], f"sha1:{hashlib.sha1(raw).hexdigest()}")

    def test_unavailable_record_regeneration_never_invokes_git_or_a_shell(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            temporary = Path(temporary_dir)
            fake_bin = temporary / "bin"
            fake_bin.mkdir()
            fake_git = fake_bin / "git"
            fake_git.write_text("#!/bin/sh\nexit 99\n", encoding="utf-8")
            fake_git.chmod(0o755)
            output = temporary / "ledger.json"
            result = subprocess.run(
                [sys.executable, SCRIPT, "regenerate", LEDGER, "--output", output],
                text=True,
                capture_output=True,
                env={"PATH": str(fake_bin)},
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(output.read_text(encoding="utf-8")), json.loads(LEDGER.read_text(encoding="utf-8")))
        source = SCRIPT.read_text(encoding="utf-8")
        self.assertNotIn("shell=True", source)
        self.assertNotIn('"fetch"', source)
        self.assertNotIn('"clone"', source)

    def test_regeneration_is_byte_stable_in_independent_repositories(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            temporary = Path(temporary_dir)
            ledger = self.fixture_ledger(temporary)
            source = temporary / "ledger.json"
            self.write(source, ledger)
            arguments = ("--repository", "core-1.0=core", "--repository", "knots-1.0.knots=knots")
            first = temporary / "first.json"
            second = temporary / "second.json"
            self.run_tool("regenerate", source, *arguments, "--output", first, cwd=temporary)
            self.run_tool("regenerate", source, *arguments, "--output", second, cwd=temporary)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(json.loads(first.read_text(encoding="utf-8")), ledger)
            duplicate = self.run_tool("regenerate", source, "--repository", "core-1.0=core", "--repository", "core-1.0=core", "--output", temporary / "duplicate.json", cwd=temporary, check=False)
            self.assertNotEqual(duplicate.returncode, 0)

    def test_rejects_unscoped_contradictory_and_unsafe_records(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            temporary = Path(temporary_dir)
            ledger = self.fixture_ledger(temporary)
            cases = []
            unscoped = copy.deepcopy(ledger); unscoped["claims"][0]["scope"] = []
            cases.append(unscoped)
            contradictory = copy.deepcopy(ledger); contradictory["claims"].append(copy.deepcopy(ledger["claims"][0])); contradictory["claims"][1]["id"] = "fixture-tree-again"; contradictory["claims"][1]["status"] = "different"
            cases.append(contradictory)
            mutable = copy.deepcopy(ledger); mutable["releases"][0]["refspec"] = "refs/heads/main:refs/tags/v1.0"
            cases.append(mutable)
            ambiguous_tag_ref = copy.deepcopy(ledger); ambiguous_tag_ref["releases"][0]["tag_ref"] = "v1.0"
            cases.append(ambiguous_tag_ref)
            mismatched_tag_ref = copy.deepcopy(ledger); mismatched_tag_ref["releases"][0]["tag_ref"] = "refs/tags/v1.1"
            cases.append(mismatched_tag_ref)
            mismatched_refspec = copy.deepcopy(ledger); mismatched_refspec["releases"][0]["refspec"] = "refs/tags/v1.0:refs/tags/v1.1"
            cases.append(mismatched_refspec)
            cross_project_tag = copy.deepcopy(ledger); cross_project_tag["releases"][0]["tag"] = "v1.0.knots"; cross_project_tag["releases"][0]["tag_ref"] = "refs/tags/v1.0.knots"; cross_project_tag["releases"][0]["refspec"] = "refs/tags/v1.0.knots:refs/tags/v1.0.knots"
            cases.append(cross_project_tag)
            for unsafe_url in ("http://example.invalid/core.git", "https://user@example.invalid/core.git", "https://example.invalid:443/core.git", "https://example.invalid/core.git?ref=v1.0", "https://example.invalid/roots/../core.git"):
                unsafe_repository = copy.deepcopy(ledger); unsafe_repository["releases"][0]["repository"] = unsafe_url
                cases.append(unsafe_repository)
            ambiguous_oid = copy.deepcopy(ledger); ambiguous_oid["releases"][0]["tag_object"] = "a" * 40
            cases.append(ambiguous_oid)
            absolute = copy.deepcopy(ledger); absolute["claims"][0]["scope"] = ["/tmp/source"]
            cases.append(absolute)
            timestamp = copy.deepcopy(ledger); timestamp["generated_at"] = "2026-01-01T00:00:00Z"
            cases.append(timestamp)
            cycle = copy.deepcopy(ledger); cycle["releases"][0]["depends_on"] = ["knots"]
            cases.append(cycle)
            missing_peeled = copy.deepcopy(ledger); del missing_peeled["releases"][0]["peeled_commit"]
            cases.append(missing_peeled)
            missing_tree = copy.deepcopy(ledger); del missing_tree["releases"][0]["tree"]
            cases.append(missing_tree)
            missing_tag_object = copy.deepcopy(ledger); del missing_tag_object["releases"][0]["tag_object"]
            cases.append(missing_tag_object)
            malformed_tag_object = copy.deepcopy(ledger); malformed_tag_object["releases"][0]["tag_object"] = "sha1:" + "z" * 40
            cases.append(malformed_tag_object)
            bad_key = copy.deepcopy(ledger); bad_key["releases"][0]["signature"] = "verified"; bad_key["releases"][0]["trusted_key_fingerprint"] = "REVOKED"
            cases.append(bad_key)
            revoked_key = copy.deepcopy(ledger); revoked_key["releases"][0]["signature"] = "verified"; revoked_key["releases"][0]["trusted_key_fingerprint"] = "A" * 40; revoked_key["releases"][0]["trust_status"] = "revoked"
            cases.append(revoked_key)
            verified_missing_object = copy.deepcopy(ledger); del verified_missing_object["releases"][0]["tree"]; verified_missing_object["releases"][0]["signature"] = "verified"; verified_missing_object["releases"][0]["trust_status"] = "verified"; verified_missing_object["releases"][0]["trusted_key_fingerprint"] = "A" * 40
            cases.append(verified_missing_object)
            contradictory_trust = copy.deepcopy(ledger); contradictory_trust["releases"][0]["signature"] = "not-available"; contradictory_trust["releases"][0]["trust_status"] = "verified"
            cases.append(contradictory_trust)
            unresolved = copy.deepcopy(ledger)
            unresolved["releases"].append({"id": "roots-1.0-roots", "project": "roots", "release_label": "1.0-roots", "repository": "https://example.invalid/roots.git", "tag": "v1.0-roots", "tag_ref": "refs/tags/v1.0-roots", "resolution_status": "unavailable", "retrieval": "local-fixture", "refspec": "refs/tags/v1.0-roots:refs/tags/v1.0-roots", "signature": "not-available", "trust_status": "not-available"})
            unresolved["claims"][0]["left"] = "roots-1.0-roots"
            cases.append(unresolved)
            malformed_unicode = copy.deepcopy(ledger); malformed_unicode["exceptions"].append({"id": "unicode", "release": "core-1.0", "state": "manual", "reason": "\ud800"})
            cases.append(malformed_unicode)
            for index, case in enumerate(cases):
                with self.subTest(index=index):
                    path = temporary / f"bad-{index}.json"
                    self.write(path, case)
                    self.assertNotEqual(self.run_tool("validate", path, check=False).returncode, 0)
            duplicate = temporary / "duplicate-key.json"
            duplicate.write_text('{"schema_version": 1, "schema_version": 1}\n', encoding="utf-8")
            self.assertNotEqual(self.run_tool("validate", duplicate, check=False).returncode, 0)
            oversized = temporary / "oversized.json"
            oversized.write_bytes(b" " * 1_000_001)
            self.assertNotEqual(self.run_tool("validate", oversized, check=False).returncode, 0)
            long_string = temporary / "long-string.json"
            long_string.write_text(json.dumps({"value": "x" * 16_385}), encoding="utf-8")
            self.assertNotEqual(self.run_tool("validate", long_string, check=False).returncode, 0)
            nested = temporary / "nested.json"
            nested.write_text("{" + '"x":{' * 33 + '"x":0' + "}" * 33 + "}", encoding="utf-8")
            self.assertNotEqual(self.run_tool("validate", nested, check=False).returncode, 0)
            missing = temporary / "missing.json"
            result = self.run_tool("validate", missing, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stderr, "roots-lineage: cannot read ledger JSON\n")

    def test_rejects_every_invalid_signature_and_trust_status_pair(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            temporary = Path(temporary_dir)
            ledger = self.fixture_ledger(temporary)
            signatures = ("verified", "unsigned", "unverified", "not-available")
            trust_statuses = ("verified", "unverified", "revoked", "expired", "not-available")
            invalid_pairs = {
                (signature, trust_status)
                for signature in signatures
                for trust_status in trust_statuses
                if (signature == "verified" and trust_status != "verified")
                or (trust_status == "verified" and signature != "verified")
                or (signature == "not-available" and trust_status != "not-available")
            }
            for index, (signature, trust_status) in enumerate(sorted(invalid_pairs)):
                with self.subTest(signature=signature, trust_status=trust_status):
                    case = copy.deepcopy(ledger)
                    release = case["releases"][0]
                    release["signature"] = signature
                    release["trust_status"] = trust_status
                    if signature == "verified":
                        release["trusted_key_fingerprint"] = "A" * 40
                    path = temporary / f"invalid-trust-{index}.json"
                    self.write(path, case)
                    self.assertNotEqual(self.run_tool("validate", path, check=False).returncode, 0)

    def test_accepts_a_syntactically_valid_future_roots_release_binding(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            temporary = Path(temporary_dir)
            ledger = self.fixture_ledger(temporary)
            ledger["releases"].append({
                "id": "roots-30.0-roots.1",
                "project": "roots",
                "release_label": "30.0-roots.1",
                "repository": "https://example.invalid/bitcoin-roots.git",
                "tag": "v30.0-roots.1",
                "tag_ref": "refs/tags/v30.0-roots.1",
                "resolution_status": "unavailable",
                "retrieval": "explicit-fetch-only",
                "refspec": "refs/tags/v30.0-roots.1:refs/tags/v30.0-roots.1",
                "signature": "not-available",
                "trust_status": "not-available",
            })
            path = temporary / "future-roots.json"
            self.write(path, ledger)
            self.run_tool("validate", path)

    def test_rejects_empty_unsafe_and_unicode_confusable_release_labels(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            temporary = Path(temporary_dir)
            for index, label in enumerate(("", " 30.0-roots.1", "30.0/../roots.1", "30.0-rοots.1")):
                with self.subTest(label=label):
                    case_dir = temporary / str(index)
                    case_dir.mkdir()
                    ledger = self.fixture_ledger(case_dir)
                    release = ledger["releases"][0]
                    release["release_label"] = label
                    path = temporary / f"unsafe-label-{index}.json"
                    self.write(path, ledger)
                    self.assertNotEqual(self.run_tool("validate", path, check=False).returncode, 0)

    def test_rejects_stale_tag_and_substituted_repository(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            temporary = Path(temporary_dir)
            ledger = self.fixture_ledger(temporary)
            path = temporary / "ledger.json"
            self.write(path, ledger)
            stale = copy.deepcopy(ledger)
            stale["releases"][0]["tree"] = "sha1:" + "0" * 40
            stale_path = temporary / "stale.json"
            self.write(stale_path, stale)
            result = self.run_tool("regenerate", stale_path, "--repository", "core-1.0=core", "--output", temporary / "out.json", cwd=temporary, check=False)
            self.assertNotEqual(result.returncode, 0)
            substituted = copy.deepcopy(ledger)
            substituted["releases"][0]["repository"] = "https://example.invalid/not-core.git"
            substituted_path = temporary / "substituted.json"
            self.write(substituted_path, substituted)
            result = self.run_tool("regenerate", substituted_path, "--repository", "core-1.0=core", "--output", temporary / "out.json", cwd=temporary, check=False)
            self.assertNotEqual(result.returncode, 0)
            missing_tag = copy.deepcopy(ledger)
            missing_tag["releases"][0]["tag"] = "v9.9"
            missing_tag["releases"][0]["refspec"] = "refs/tags/v9.9:refs/tags/v9.9"
            missing_tag_path = temporary / "missing-tag.json"
            self.write(missing_tag_path, missing_tag)
            result = self.run_tool("regenerate", missing_tag_path, "--repository", "core-1.0=core", "--output", temporary / "out.json", cwd=temporary, check=False)
            self.assertNotEqual(result.returncode, 0)
            shallow = temporary / "shallow"
            subprocess.run(["git", "clone", "--depth=1", f"file://{temporary / 'core'}", shallow], check=True, capture_output=True)
            subprocess.run(["git", "-C", shallow, "remote", "set-url", "origin", "https://example.invalid/core.git"], check=True)
            result = self.run_tool("regenerate", path, "--repository", "core-1.0=shallow", "--output", temporary / "out.json", cwd=temporary, check=False)
            self.assertNotEqual(result.returncode, 0)

    def test_comparison_levels_make_exclusions_visible(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            temporary = Path(temporary_dir)
            left = self.init_release(temporary, "left", "v1.0", "maintained\n")
            right = self.init_release(temporary, "right", "v1.0", "maintained\n")
            for repository, generated, policy in ((left, "one\n", "policy one\n"), (right, "two\n", "policy two\n")):
                (repository / "generated.txt").write_text(generated, encoding="utf-8")
                (repository / "policy.txt").write_text(policy, encoding="utf-8")
                subprocess.run(["git", "-C", repository, "add", "generated.txt", "policy.txt"], check=True)
                subprocess.run(["git", "-C", repository, "commit", "-m", "artifacts"], check=True, capture_output=True)
            raw = self.run_tool("compare", "--left", left, "--right", right, "--level", "complete_tree")
            self.assertEqual(json.loads(raw.stdout)["status"], "different")
            for level in ("maintained_source", "generated_release_artifact", "build_input"):
                with self.subTest(level=level):
                    unclassified = self.run_tool("compare", "--left", left, "--right", right, "--level", level, check=False)
                    self.assertNotEqual(unclassified.returncode, 0)
            narrowed = self.run_tool("compare", "--left", left, "--right", right, "--level", "maintained_source", "--exclude", "generated.txt", "--exclude", "policy.txt")
            self.assertEqual(json.loads(narrowed.stdout)["status"], "same")
            for level in ("generated_release_artifact", "build_input"):
                with self.subTest(level=level):
                    narrowed = self.run_tool("compare", "--left", left, "--right", right, "--level", level, "--exclude", "generated.txt", "--exclude", "policy.txt")
                    self.assertEqual(json.loads(narrowed.stdout)["status"], "same")
            unsupported = self.run_tool("compare", "--left", left, "--right", right, "--level", "policy", check=False)
            self.assertNotEqual(unsupported.returncode, 0)
            inventory = self.run_tool("inventory", "--left", left, "--right", right)
            records = json.loads(inventory.stdout)["records"]
            self.assertEqual({record["path"] for record in records}, {"generated.txt", "policy.txt"})
            self.assertTrue(all(record["classification"]["confidence"] == "path-derived" for record in records))


if __name__ == "__main__":
    unittest.main()
