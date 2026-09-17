#!/usr/bin/env python3
"""Release preparation, evidence asset, and signing-isolation tests."""

import hashlib
import importlib.util
import io
import json
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
BUILD_TOOL = ROOT / "ci/release/roots-build-evidence.py"
fixture_spec = importlib.util.spec_from_file_location("release_fixture", ROOT / "ci/test/test_roots_release_evidence.py")
FIXTURE = importlib.util.module_from_spec(fixture_spec)
fixture_spec.loader.exec_module(FIXTURE)


class PrepareReleaseTest(unittest.TestCase):
    def write_package(self, path, archive_root, content=b"package"):
        path.parent.mkdir(parents=True, exist_ok=True)
        member = f"{archive_root}/bin/bitcoind"
        if path.name.endswith(".tar.gz"):
            with tarfile.open(path, "w:gz") as package:
                info = tarfile.TarInfo(member)
                info.size = len(content)
                package.addfile(info, io.BytesIO(content))
        else:
            with zipfile.ZipFile(path, "w") as package:
                package.writestr(member, content)

    def setup_release(self, work):
        source, revision, _ = FIXTURE.ReleaseEvidenceTest().fixture(work)
        downloads = work / "downloads"
        archive_root = "bitcoin-roots-29.4-roots.1"
        for name in (
            "bitcoin-roots-linux-x86_64.tar.gz",
            "bitcoin-roots-darwin-arm64.zip",
            "bitcoin-roots-windows-x86_64.zip",
        ):
            package = downloads / name
            self.write_package(package, archive_root)
        tree = "sha1:" + FIXTURE.git(source, "rev-parse", "HEAD^{tree}")
        evidence_dir = work / "evidence"
        evidence_dir.mkdir()
        for package in list(downloads.iterdir()):
            subprocess.run([
                sys.executable, BUILD_TOOL, "attest", "--artifact", package,
                "--source-repository", source, "--source-revision", revision,
                "--output", downloads / (package.name + ".build-attestation.json"),
            ], check=True)
        subprocess.run([sys.executable, BUILD_TOOL, "aggregate", "--artifacts", downloads, "--source-repository", source, "--source-revision", revision, "--expected-count", "3", "--output", "roots-release-build-evidence.json"], cwd=evidence_dir, check=True)
        build = evidence_dir / "roots-release-build-evidence.json"
        subprocess.run([
            sys.executable, EVIDENCE_TOOL, "--ledger", ROOT / FIXTURE.CANONICAL[0], "--manifest", ROOT / FIXTURE.CANONICAL[1],
            "--replay-result", source / FIXTURE.CANONICAL[2], "--fixture", source / FIXTURE.CANONICAL[3],
            "--registry", source / FIXTURE.CANONICAL[4], "--accounting", source / FIXTURE.CANONICAL[5],
            "--release-accounting", source / FIXTURE.CANONICAL[6],
            "--build-evidence", build, "--source-repository", source, "--source-revision", revision,
            "--candidate-tree", tree, "--output", "roots-release-evidence.json",
        ], cwd=evidence_dir, check=True)
        return source, revision, downloads, evidence_dir / "roots-release-evidence.json", build

    def test_prepares_manifest_with_both_provenance_assets(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            source, revision, downloads, evidence, build = self.setup_release(work)
            output = work / "output"
            subprocess.run([SCRIPT, downloads, output, PUBLIC_KEY, "v29.4-roots.1", "3", evidence, build, source, revision], check=True)
            subprocess.run(["sha512sum", "--check", "SHA512SUMS"], cwd=output, check=True)
            names = sorted(path.name for path in output.iterdir())
            self.assertEqual(names, [
                "SHA512SUMS", "bitcoin-roots-darwin-arm64.zip", "bitcoin-roots-linux-x86_64.tar.gz",
                "bitcoin-roots-windows-x86_64.zip", "roots-release-build-evidence.json", "roots-release-evidence.json",
            ])
            manifest = (output / "SHA512SUMS").read_text()
            self.assertIn(hashlib.sha512(build.read_bytes()).hexdigest() + "  roots-release-build-evidence.json", manifest)

    def test_rejects_stale_build_evidence_and_dirty_canonical_input(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            source, revision, downloads, evidence, build = self.setup_release(work)
            package = next(downloads.glob("*linux*"))
            package.write_bytes(package.read_bytes() + b"tamper")
            result = subprocess.run([SCRIPT, downloads, work / "output", PUBLIC_KEY, "v29.4-roots.1", "3", evidence, build, source, revision], text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            source, revision, downloads, evidence, build = self.setup_release(work)
            path = source / FIXTURE.CANONICAL[3]
            path.write_bytes(path.read_bytes() + b"\n")
            result = subprocess.run([SCRIPT, downloads, work / "output", PUBLIC_KEY, "v29.4-roots.1", "3", evidence, build, source, revision], text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)

    def test_archive_root_uses_release_version_or_commit(self):
        result = subprocess.run([sys.executable, ARCHIVE_TOOL, "root-name"], env={"PATH": "/usr/bin:/bin", "RELEASE_TAG": "v29.4-roots.1", "GITHUB_SHA": ""}, text=True, capture_output=True, check=True)
        self.assertEqual(result.stdout.strip(), "bitcoin-roots-29.4-roots.1")

    def test_release_workflow_isolates_signing_and_publication(self):
        workflow = RELEASE_WORKFLOW.read_text()
        self.assertIn("prepare-release:", workflow)
        self.assertIn("sign-release:", workflow)
        self.assertIn("environment: release-signing", workflow)
        self.assertIn("needs: prepare-release", workflow)
        self.assertIn("needs: sign-release", workflow)
        self.assertEqual(workflow.count("roots-build-evidence.py attest"), 3)
        self.assertIn("roots-build-evidence.py\" aggregate", workflow)
        self.assertIn("--source-revision \"$GITHUB_SHA\"", workflow)
        self.assertIn("--release-accounting", workflow)
        sign = workflow.split("  sign-release:", 1)[1].split("  create-signed-draft:", 1)[0]
        publish = workflow.split("  create-signed-draft:", 1)[1]
        self.assertNotIn("actions/checkout", sign)
        self.assertNotIn("ci/", sign)
        self.assertIn("Unexpected unsigned release asset", sign)
        self.assertIn("-size +2147483648c", sign)
        self.assertIn("BITCOIN_ROOTS_GPG_SK", sign)
        self.assertNotIn("BITCOIN_ROOTS_GPG_SK", publish)
        self.assertNotIn("gpg --", publish)
        self.assertIn("contents: write", publish)
        self.assertIn("EXPECTED_PACKAGE_COUNT + 4", publish)

    def test_guarded_publication_workflow_changes_only_draft_visibility(self):
        workflow = (ROOT / ".github/workflows/publish-release.yml").read_text()
        script = ROOT / "ci/release/publish-verified-draft.sh"
        self.assertIn("workflow_dispatch:", workflow)
        self.assertIn("confirm_publication:", workflow)
        self.assertIn("verified_manifest_sha256:", workflow)
        self.assertIn("publish-verified-draft.sh", workflow)
        self.assertIn("actions/checkout@v6", workflow)
        self.assertIn("test -f ci/release/publish-verified-draft.sh", workflow)
        self.assertIn("bash ci/release/publish-verified-draft.sh", workflow)
        text = script.read_text()
        self.assertIn("gh release edit", text)
        for forbidden in ("git tag", "git push", "gpg", "gh release create", "actions/checkout"):
            self.assertNotIn(forbidden, text)

    def test_guarded_publication_rejects_without_confirmation_and_is_idempotent(self):
        script = ROOT / "ci/release/publish-verified-draft.sh"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fake_bin = root / "bin"
            fake_bin.mkdir()
            log = root / "gh.log"
            fake_gh = fake_bin / "gh"
            fake_gh.write_text("#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$GH_LOG\"\ncase \"$*\" in *tagName*) printf '%s\\n' v29.4-roots.1 ;; *isDraft*) printf '%s\\n' \"${GH_DRAFT:-true}\" ;; *'release download'*) while [ \"$1\" != --dir ]; do shift; done; mkdir -p \"$2\"; printf manifest > \"$2/SHA512SUMS\" ;; *'release edit'*) exit 0 ;; esac\n", encoding="utf-8")
            fake_gh.chmod(0o755)
            environment = {"PATH": str(fake_bin) + ":/usr/bin:/bin", "GH_LOG": str(log)}
            manifest_digest = hashlib.sha256(b"manifest").hexdigest()
            rejected = subprocess.run(["bash", script, "v29.4-roots.1", "false", manifest_digest], env=environment, text=True, capture_output=True)
            self.assertNotEqual(rejected.returncode, 0)
            published = subprocess.run(["bash", script, "v29.4-roots.1", "true", manifest_digest], env=environment, text=True, capture_output=True)
            self.assertEqual(published.returncode, 0, published.stderr)
            self.assertIn("release edit v29.4-roots.1 --draft=false", log.read_text())
            log.write_text("", encoding="utf-8")
            repeated = subprocess.run(["bash", script, "v29.4-roots.1", "true", manifest_digest], env={**environment, "GH_DRAFT": "false"}, text=True, capture_output=True)
            self.assertEqual(repeated.returncode, 0, repeated.stderr)
            self.assertNotIn("release edit", log.read_text())


if __name__ == "__main__":
    unittest.main()
