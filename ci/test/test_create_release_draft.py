#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import test_create_patch_series as patch_fixtures
import test_prepare_release as package_fixtures


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "ci/release/create-draft.py"
RELEASE_TAG = patch_fixtures.RELEASE_TAG
PLATFORMS = ("linux-x86_64", "linux-aarch64", "darwin-x86_64", "darwin-arm64", "windows-x86_64")


class CreateReleaseDraftTest(unittest.TestCase):
    def setUp(self):
        self.fixture = patch_fixtures.CreatePatchSeriesTest()
        self.repository, _ = self.fixture.make_release_repository()
        self.addCleanup(self.fixture.doCleanups)
        # Run the real helper from the isolated tagged source tree rather than
        # allowing a different checkout to supply its validators or licenses.
        shutil.copytree(ROOT / "ci/release", self.repository / "ci/release", ignore=shutil.ignore_patterns("__pycache__"))
        for source in ("COPYING", *package_fixtures.notice_fixtures.NOTICES.TRACKED_NOTICES.values()):
            destination = self.repository / source
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / source, destination)
        (self.repository / ".gitignore").write_text("__pycache__/\n")
        self.git("add", ".")
        self.git("commit", "-m", "release tools and notices")
        self.git("branch", "--force", "roots/29.4", "HEAD")
        self.git("tag", "--delete", RELEASE_TAG)
        self.git("tag", "-a", RELEASE_TAG, "-m", RELEASE_TAG)
        self.helper = self.repository / "ci/release/create-draft.py"
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.assets = self.work / "assets"
        self.assets.mkdir()
        self.commit = self.git("rev-parse", "HEAD").stdout.strip()
        self.package_fixture = package_fixtures.PrepareReleaseTest()
        for platform in PLATFORMS:
            name = f"bitcoin-roots-{platform}.{'tar.gz' if platform.startswith('linux') else 'zip'}"
            self.package_fixture.write_package(self.assets / name, "bitcoin-roots-29.4-roots.1", b"binary")
        self.patch = self.assets / "bitcoin-roots-29.4-roots.1.patch"
        subprocess.run([self.repository / "ci/release/create-patch-series.sh", RELEASE_TAG, self.patch],
                       cwd=self.repository, env={**os.environ, "RELEASE_CANONICAL_REF": "roots/29.4"},
                       check=True, capture_output=True)
        self.manifest = self.assets / "SHA512SUMS"
        self.refresh_manifest()
        self.notes = self.work / "notes.md"
        self.notes.write_text("Reviewed release notes and contributor credit\n")
        self.tools = self.work / "tools"
        self.tools.mkdir()
        self.record = self.work / "created.json"
        # Only network boundaries are replaced. The real git source validators,
        # archive/notice validators, patch exporter and replay execute offline.
        real_git = shutil.which("git")
        (self.tools / "git").write_text(
            "#!/usr/bin/env python3\nimport os,sys\n"
            "args=sys.argv[1:]\n"
            "if args[0]=='ls-remote': args[1]=os.environ['DRAFT_TEST_REMOTE']\n"
            f"os.execv({real_git!r}, ['git', *args])\n"
        )
        (self.tools / "gh").write_text(
            "#!/usr/bin/env python3\nimport hashlib,json,os,pathlib,sys\n"
            "args=sys.argv[1:]\n"
            "if args[:1]==['api']:\n"
            " if os.environ.get('DRAFT_TEST_API_FAILURE'): sys.exit(1)\n"
            " print(os.environ.get('DRAFT_TEST_RELEASES', '[[]]'))\n"
            "elif args[:2]==['release','create']:\n"
            " notes=pathlib.Path(args[args.index('--notes-file')+1]).read_text()\n"
            " assets={pathlib.Path(x).name:hashlib.sha512(pathlib.Path(x).read_bytes()).hexdigest() "
            "for x in args if pathlib.Path(x).is_file() and x!=args[args.index('--notes-file')+1]}\n"
            " pathlib.Path(os.environ['DRAFT_TEST_RECORD']).write_text(json.dumps({'args':args,'notes':notes,'assets':assets}))\n"
            " print('https://example.invalid/release/draft')\n"
            "else: sys.exit(2)\n"
        )
        for path in self.tools.iterdir():
            path.chmod(0o755)

    def git(self, *args):
        return subprocess.run(["git", "-C", self.repository, *args], check=True, capture_output=True, text=True)

    def refresh_manifest(self):
        names = sorted(path for path in self.assets.iterdir() if path.name != "SHA512SUMS")
        self.manifest.write_text("# Bitcoin Roots release: " + RELEASE_TAG + "\n" + "".join(
            f"{hashlib.sha512(path.read_bytes()).hexdigest()}  {path.name}\n" for path in names))

    def invoke(self, create=True, **environment):
        command = ["python3", self.helper, "--tag", RELEASE_TAG, "--commit", self.commit,
                   "--repository", "owner/repository", "--assets", self.assets, "--notes", self.notes,
                   "--manifest-sha512", hashlib.sha512(self.manifest.read_bytes()).hexdigest(),
                   "--notes-sha256", hashlib.sha256(self.notes.read_bytes()).hexdigest()]
        if create:
            command.append("--create-draft")
        return subprocess.run(command, cwd=self.repository, capture_output=True, text=True,
                              env={**os.environ, "PATH": f"{self.tools}:{os.environ['PATH']}",
                                   "DRAFT_TEST_REMOTE": str(self.repository),
                                   "DRAFT_TEST_RECORD": str(self.record), **environment})

    def assert_rejected(self, **environment):
        result = self.invoke(**environment)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertFalse(self.record.exists(), result.stderr)
        return result

    def test_creates_only_verified_draft_with_exact_snapshot_and_reviewed_notes(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        record = json.loads(self.record.read_text())
        self.assertIn("--draft", record["args"])
        self.assertIn("--verify-tag", record["args"])
        self.assertNotIn("--clobber", record["args"])
        self.assertEqual(record["notes"], self.notes.read_text())
        self.assertEqual(record["assets"], {path.name: hashlib.sha512(path.read_bytes()).hexdigest()
                                            for path in self.assets.iterdir()})
        self.assertTrue(all(str(self.assets) not in arg for arg in record["args"]))

    def test_verification_without_create_draft_has_no_write(self):
        result = self.invoke(create=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.record.exists())

    def test_refuses_existing_draft_or_public_release_on_any_page(self):
        for draft in (True, False):
            with self.subTest(draft=draft):
                result = self.assert_rejected(DRAFT_TEST_RELEASES=json.dumps(
                    [[{"tag_name": "v29.4-roots.9"}], [{"tag_name": RELEASE_TAG, "draft": draft}]]))
                self.assertIn("Release already exists", result.stderr)

    def test_release_inventory_network_failure_cannot_authorize_creation(self):
        self.assert_rejected(DRAFT_TEST_API_FAILURE="1")

    def test_rejects_missing_extra_symlink_and_modified_asset(self):
        path = self.assets / "bitcoin-roots-windows-x86_64.zip"
        original = path.read_bytes()
        path.unlink()
        self.assert_rejected()
        path.symlink_to(self.work / "missing")
        self.assert_rejected()
        path.unlink()
        path.write_bytes(original + b"alteration")
        self.assert_rejected()
        path.write_bytes(original)
        (self.assets / "unexpected").write_text("extra")
        self.assert_rejected()

    def test_rejects_duplicate_and_incomplete_manifest(self):
        self.manifest.write_text(self.manifest.read_text() + self.manifest.read_text().splitlines()[-1] + "\n")
        self.assert_rejected()
        self.manifest.write_text("\n".join(self.manifest.read_text().splitlines()[:2]) + "\n")
        self.assert_rejected()

    def test_rejects_patch_from_different_source_even_with_matching_checksum(self):
        self.patch.write_text("different patch\n")
        self.refresh_manifest()
        result = self.assert_rejected()
        self.assertIn("replay-verified tagged series", result.stderr)

    def test_accepts_canonical_copying_without_dependency_index(self):
        path = self.assets / "bitcoin-roots-linux-x86_64.tar.gz"
        self.package_fixture.write_package(path, "bitcoin-roots-29.4-roots.1", b"binary", notices=False)
        self.refresh_manifest()
        result = self.invoke(create=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.record.exists())

    def test_rejects_changed_remote_canonical_or_annotated_tag(self):
        self.git("branch", "--force", "roots/29.4", "v29.4")
        result = self.assert_rejected()
        self.assertIn("Remote canonical tip or annotated tag", result.stderr)
        self.git("branch", "--force", "roots/29.4", self.commit)
        self.git("tag", "--delete", RELEASE_TAG)
        self.git("tag", RELEASE_TAG, self.commit)
        self.assert_rejected()

    def test_rejects_wrong_head_and_dirty_source(self):
        self.git("checkout", "--detach", "v29.4")
        self.assert_rejected()
        self.git("checkout", "--detach", self.commit)
        (self.repository / "source").write_text("dirty\n")
        self.assert_rejected()

    def test_rejects_untracked_source_file(self):
        (self.repository / "untracked-release-input").write_text("unreviewed\n")
        result = self.assert_rejected()
        self.assertIn("untracked changes", result.stderr)

    def test_reviewed_digest_anchors_reject_replaced_manifest_or_notes(self):
        # Snapshot failures are checked directly to preserve the pre-change
        # reviewed digests, unlike invoke() which deliberately repins fixtures.
        import importlib.util
        import sys
        from types import SimpleNamespace
        sys.path.insert(0, str(ROOT / "ci/release"))
        self.addCleanup(sys.path.remove, str(ROOT / "ci/release"))
        spec = importlib.util.spec_from_file_location("draft_helper", SCRIPT)
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        args = SimpleNamespace(tag=RELEASE_TAG, assets=self.assets, notes=self.notes,
                               manifest_sha512=hashlib.sha512(self.manifest.read_bytes()).hexdigest(),
                               notes_sha256=hashlib.sha256(self.notes.read_bytes()).hexdigest())
        for target in (self.notes, self.manifest):
            original = target.read_bytes()
            target.write_bytes(original + b"replacement")
            with tempfile.TemporaryDirectory() as directory, self.assertRaises(ValueError):
                helper.snapshot_packet(args, Path(directory))
            target.write_bytes(original)


if __name__ == "__main__":
    unittest.main()
