#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "ci/release/prepare-release.sh"
ARCHIVE_TOOL = ROOT / "ci/release/archive.py"
PUBLIC_KEY = ROOT / "contrib/release/bitcoin-roots-release-key.asc"
RELEASE_WORKFLOW = ROOT / ".github/workflows/release.yml"
EVIDENCE_TOOL = ROOT / "ci/release/roots-release-evidence.py"


class PrepareReleaseTest(unittest.TestCase):
    def source_repository(self, work):
        repository = work / "source"
        for relative in (
            "contrib/roots/lineage-ledger.json",
            "contrib/roots/adaptation-manifest-29.3.json",
            "contrib/roots/replay-29.4-proposal/acceptance-evidence.json",
        ):
            target = repository / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)
        subprocess.run(["git", "init", "--quiet", repository], check=True)
        subprocess.run(["git", "-C", repository, "config", "user.email", "test@example.invalid"], check=True)
        subprocess.run(["git", "-C", repository, "config", "user.name", "Test"], check=True)
        subprocess.run(["git", "-C", repository, "add", "contrib"], check=True)
        subprocess.run(["git", "-C", repository, "commit", "--quiet", "-m", "accepted inputs"], check=True)
        return repository

    def test_prepare_rejects_dirty_canonical_inputs(self):
        relative_inputs = (
            "contrib/roots/lineage-ledger.json",
            "contrib/roots/adaptation-manifest-29.3.json",
            "contrib/roots/replay-29.4-proposal/acceptance-evidence.json",
        )
        for relative in relative_inputs:
            with self.subTest(relative=relative), tempfile.TemporaryDirectory() as temporary:
                work = Path(temporary)
                source = self.source_repository(work)
                downloads = work / "downloads"
                downloads.mkdir()
                self.write_package(
                    downloads / "bitcoin-roots-linux-x86_64.tar.gz",
                    "bitcoin-roots-29.3-roots.1",
                    b"package",
                )
                evidence, revision = self.evidence(work, source)
                source_path = source / relative
                source_path.write_bytes(source_path.read_bytes() + b"\n")
                result = subprocess.run(
                    [
                        SCRIPT,
                        downloads,
                        work / "output",
                        PUBLIC_KEY,
                        "v29.3-roots.1",
                        "1",
                        evidence,
                        source,
                        revision,
                    ],
                    capture_output=True,
                    text=True,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("canonical input differs from source revision", result.stderr)
                self.assertFalse((work / "output" / "SHA512SUMS").exists())

    def test_rejects_wrong_source_revision_and_tree(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            downloads = work / "downloads"
            downloads.mkdir()
            self.write_package(downloads / "bitcoin-roots-linux-x86_64.tar.gz", "bitcoin-roots-29.3-roots.1", b"package")
            evidence, revision = self.evidence(work)
            result = subprocess.run([SCRIPT, downloads, work / "output", PUBLIC_KEY, "v29.3-roots.1", "1", evidence, ROOT, "0" * 40], text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((work / "output" / "SHA512SUMS").exists())
    def prepared_release(self, work):
        downloads, output = work / "downloads", work / "output"
        downloads.mkdir()
        package = downloads / "bitcoin-roots-linux-x86_64.tar.gz"
        self.write_package(package, "bitcoin-roots-29.3-roots.1", b"package")
        evidence, revision = self.evidence(work)
        subprocess.run([SCRIPT, downloads, output, PUBLIC_KEY, "v29.3-roots.1", "1", evidence, ROOT, revision], check=True)
        return output

    @unittest.skipUnless(shutil.which("gpg"), "gpg is unavailable")
    def test_disposable_signature_verifies(self):
        with tempfile.TemporaryDirectory() as temporary:
            prepared = self.prepared_release(Path(temporary))
            home = Path(temporary) / "signer"
            verify = Path(temporary) / "verify"
            home.mkdir()
            verify.mkdir()
            environment = {**os.environ, "GNUPGHOME": str(home)}
            subprocess.run(["gpg", "--batch", "--passphrase", "", "--quick-generate-key", "test@example.invalid", "default", "default", "never"], env=environment, check=True)
            manifest = prepared / "SHA512SUMS"
            subprocess.run(["sha512sum", "--check", "SHA512SUMS"], cwd=prepared, check=True)
            evidence = json.loads((prepared / "roots-release-evidence.json").read_text())
            self.assertEqual([item["path"] for item in evidence["artifact_links"]], ["contrib/roots/lineage-ledger.json", "contrib/roots/adaptation-manifest-29.3.json", "contrib/roots/replay-29.4-proposal/acceptance-evidence.json"])
            for item in evidence["artifact_links"]:
                self.assertEqual(item["sha256"], "sha256:" + hashlib.sha256((ROOT / item["path"]).read_bytes()).hexdigest())
            subprocess.run(["gpg", "--batch", "--armor", "--detach-sign", manifest], env=environment, check=True)
            public = subprocess.run(["gpg", "--batch", "--armor", "--export", "test@example.invalid"], env=environment, check=True, capture_output=True).stdout
            verify_environment = {**os.environ, "GNUPGHOME": str(verify)}
            subprocess.run(["gpg", "--batch", "--import"], env=verify_environment, input=public, check=True)
            subprocess.run(["gpg", "--batch", "--verify", manifest.with_suffix(".asc"), manifest], env=verify_environment, check=True)
            manifest.write_text("tampered\n")
            self.assertNotEqual(subprocess.run(["gpg", "--batch", "--verify", manifest.with_suffix(".asc"), manifest], env=verify_environment).returncode, 0)
    def evidence(self, work, source_repository=ROOT):
        output = "roots-release-evidence.json"
        revision = subprocess.run(["git", "-C", source_repository, "rev-parse", "HEAD"], check=True, text=True, capture_output=True).stdout.strip()
        tree = subprocess.run(["git", "-C", source_repository, "rev-parse", "HEAD^{tree}"], check=True, text=True, capture_output=True).stdout.strip()
        subprocess.run(
            [sys.executable, EVIDENCE_TOOL, "--ledger", ROOT / "contrib/roots/lineage-ledger.json",
             "--manifest", ROOT / "contrib/roots/adaptation-manifest-29.3.json", "--replay-result",
             ROOT / "contrib/roots/replay-29.4-proposal/acceptance-evidence.json", "--source-repository", source_repository,
             "--source-revision", revision, "--candidate-tree", "sha1:" + tree, "--output", output],
            cwd=work, check=True,
        )
        return work / output, revision

    def write_package(self, path, root, content):
        member = f"{root}/bin/bitcoin-qt"
        if path.name.endswith(".tar.gz"):
            with tarfile.open(path, mode="w:gz") as package:
                info = tarfile.TarInfo(member)
                info.size = len(content)
                package.addfile(info, io.BytesIO(content))
        else:
            with zipfile.ZipFile(path, mode="w") as package:
                package.writestr(member, content)

    def test_prepares_sorted_manifest_and_ignores_old_checksums(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            downloads = work / "downloads"
            output = work / "output"
            (downloads / "linux").mkdir(parents=True)
            (downloads / "darwin-x86_64").mkdir()
            (downloads / "darwin-arm64").mkdir()
            packages = {
                downloads / "linux/bitcoin-roots-linux-x86_64.tar.gz": b"linux package",
                downloads / "darwin-x86_64/bitcoin-roots-darwin-x86_64.zip": b"macOS x86_64 package",
                downloads / "darwin-arm64/bitcoin-roots-darwin-arm64.zip": b"macOS arm64 package",
            }
            archive_root = "bitcoin-roots-29.3.0-roots.1"
            for path, content in packages.items():
                self.write_package(path, archive_root, content)
            (downloads / "linux/bitcoin-roots-linux-x86_64.tar.gz.sha256").write_text("ignored\n")
            (downloads / "darwin-x86_64/SHA256SUMS").write_text("ignored\n")
            evidence, revision = self.evidence(work)

            subprocess.run(
                [SCRIPT, downloads, output, PUBLIC_KEY, "v29.3.0-roots.1", "3", evidence, ROOT, revision],
                check=True,
            )

            manifest = (output / "SHA512SUMS").read_text()
            self.assertIn("# Bitcoin Roots release: v29.3.0-roots.1\n", manifest)
            for line in PUBLIC_KEY.read_text().splitlines():
                self.assertIn(f"# {line}\n", manifest)
            checksum_lines = [line for line in manifest.splitlines() if not line.startswith("#")]
            archive_hashes = {
                path.name: hashlib.sha512(path.read_bytes()).hexdigest() for path in packages
            }
            expected = [
                f"{archive_hashes['bitcoin-roots-darwin-arm64.zip']}  bitcoin-roots-darwin-arm64.zip",
                f"{archive_hashes['bitcoin-roots-darwin-x86_64.zip']}  bitcoin-roots-darwin-x86_64.zip",
                f"{archive_hashes['bitcoin-roots-linux-x86_64.tar.gz']}  bitcoin-roots-linux-x86_64.tar.gz",
                f"{hashlib.sha512(evidence.read_bytes()).hexdigest()}  roots-release-evidence.json",
            ]
            self.assertEqual(checksum_lines, expected)
            self.assertEqual(
                sorted(path.name for path in output.iterdir()),
                [
                    "SHA512SUMS",
                    "bitcoin-roots-darwin-arm64.zip",
                    "bitcoin-roots-darwin-x86_64.zip",
                    "bitcoin-roots-linux-x86_64.tar.gz",
                    "roots-release-evidence.json",
                ],
            )
            subprocess.run(["sha512sum", "--check", "SHA512SUMS"], cwd=output, check=True)

    def test_rejects_duplicate_package_names(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            downloads = work / "downloads"
            output = work / "output"
            (downloads / "one").mkdir(parents=True)
            (downloads / "two").mkdir()
            (downloads / "one/package.zip").write_bytes(b"one")
            (downloads / "two/package.zip").write_bytes(b"two")
            evidence, revision = self.evidence(work)

            result = subprocess.run(
                [SCRIPT, downloads, output, PUBLIC_KEY, "test", "2", evidence, ROOT, revision],
                capture_output=True,
                text=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Duplicate release package name: package.zip", result.stderr)

    def test_archive_root_uses_release_version_or_commit(self):
        cases = [
            ({"RELEASE_TAG": "v29.3-roots.1"}, "bitcoin-roots-29.3-roots.1"),
            ({"RELEASE_TAG": "v30.0+roots.1"}, "bitcoin-roots-30.0+roots.1"),
            ({"GITHUB_SHA": "0123456789abcdef"}, "bitcoin-roots-git-0123456789ab"),
        ]
        for environment, expected in cases:
            with self.subTest(environment=environment):
                result = subprocess.run(
                    [sys.executable, ARCHIVE_TOOL, "root-name"],
                    env={**os.environ, "RELEASE_TAG": "", "GITHUB_SHA": "", **environment},
                    capture_output=True,
                    text=True,
                    check=True,
                )
                self.assertEqual(result.stdout.strip(), expected)

    def test_rejects_archive_without_versioned_root(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            downloads = work / "downloads"
            output = work / "output"
            downloads.mkdir()
            package = downloads / "bitcoin-roots-windows-x86_64.zip"
            self.write_package(package, "bitcoin-roots-wrong-version", b"package")
            evidence, revision = self.evidence(work)

            result = subprocess.run(
                [SCRIPT, downloads, output, PUBLIC_KEY, "v29.3-roots.1", "1", evidence, ROOT, revision],
                capture_output=True,
                text=True,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Expected root directory bitcoin-roots-29.3-roots.1", result.stderr)

    def test_release_workflow_wraps_every_platform_archive(self):
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        self.assertEqual(workflow.count("ci/release/archive.py root-name"), 3)
        self.assertEqual(workflow.count("ci/release/archive.py validate"), 3)
        self.assertIn('--transform "flags=r;s|^\\.|${archive_root}|"', workflow)
        self.assertIn('unzip -q "${packages[0]}" -d "${staging_parent}/${archive_root}"', workflow)
        self.assertIn('zip -qry "${archive}" "${archive_root}"', workflow)
        self.assertIn("roots-release-evidence.py", workflow)
        self.assertIn("EXPECTED_PACKAGE_COUNT + 3", workflow)
        self.assertIn("gpg --batch --verify release-assets/SHA512SUMS.asc release-assets/SHA512SUMS", workflow)
        self.assertIn("gh release create", workflow)


if __name__ == "__main__":
    unittest.main()
