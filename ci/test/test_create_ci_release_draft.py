#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import test_create_release_draft as fixtures

ROOT = fixtures.ROOT
RELEASE_TAG = fixtures.RELEASE_TAG


@unittest.skipUnless(shutil.which('gpg'), 'gpg is required')
class CreateCIDraftTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.key_home = Path(cls.temporary.name)
        cls.key_home.chmod(0o700)
        cls.gpg('--passphrase', '', '--quick-generate-key', 'CI Fixture <fixture@example.invalid>', 'ed25519', 'sign', '0')
        cls.fingerprint = cls.gpg('--with-colons', '--list-keys').stdout.decode().split('fpr:::::::::', 1)[1].split(':', 1)[0]
        cls.public = cls.gpg('--armor', '--export', cls.fingerprint).stdout
        cls.secret = cls.gpg('--armor', '--export-secret-keys', cls.fingerprint).stdout.decode()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @classmethod
    def gpg(cls, *args):
        return subprocess.run(['gpg', '--batch', '--homedir', cls.key_home, *args], check=True, capture_output=True)

    def setUp(self):
        self.fixture = fixtures.CreateReleaseDraftTest()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        f = self.fixture
        self.public_key = f.work / 'public.asc'
        self.public_key.write_bytes(self.public)
        subprocess.run([ROOT / 'ci/release/sign-manifest.sh', f.manifest, self.public_key, self.fingerprint],
                       env={**os.environ, 'BITCOIN_ROOTS_GPG_SK': self.secret, 'REQUIRE_RELEASE_SIGNATURE': '1'},
                       check=True, capture_output=True)

    def invoke(self, **environment):
        f = self.fixture
        return subprocess.run(['python3', f.repository / 'ci/release/create-ci-draft.py', '--tag', RELEASE_TAG,
                               '--repository', 'owner/repository', '--assets', f.assets, '--notes', f.notes,
                               '--public-key', self.public_key, '--fingerprint', self.fingerprint, '--run-id', '123'],
                              cwd=f.repository, capture_output=True, text=True,
                              env={**os.environ, 'PATH': f'{f.tools}:{os.environ["PATH"]}',
                                   'DRAFT_TEST_REMOTE': str(f.repository), 'DRAFT_TEST_RECORD': str(f.record), **environment})

    def test_actual_signed_ci_draft_creates_exact_eight_assets_with_provenance(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        record = json.loads(self.fixture.record.read_text())
        self.assertEqual(len(record['assets']), 8)
        self.assertIn('SHA512SUMS.asc', record['assets'])
        self.assertIn(self.fingerprint, record['notes'])
        self.assertIn(self.fixture.commit, record['notes'])
        self.assertIn('/actions/runs/123', record['notes'])
        self.assertIn('Packages include the project COPYING file.', record['notes'])
        self.assertNotIn('notices were collected', record['notes'])
        self.assertIn('--draft', record['args'])
        self.assertNotIn('--clobber', record['args'])

    def test_rejects_existing_draft_or_public_release_without_write(self):
        for is_draft in (True, False):
            with self.subTest(draft=is_draft):
                result = self.invoke(DRAFT_TEST_RELEASES=json.dumps([[{'tag_name': RELEASE_TAG, 'draft': is_draft}]]))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('already exists', result.stderr)
                self.assertFalse(self.fixture.record.exists())

    def test_rejects_missing_or_invalid_signature_and_extra_assets(self):
        signature = self.fixture.assets / 'SHA512SUMS.asc'
        original = signature.read_bytes()
        signature.unlink()
        self.assertNotEqual(self.invoke().returncode, 0)
        signature.write_bytes(b'Not a signature\n')
        self.assertNotEqual(self.invoke().returncode, 0)
        signature.write_bytes(original)
        (self.fixture.assets / 'unexpected').write_text('extra')
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertFalse(self.fixture.record.exists())

    def test_required_signing_rejects_missing_secret_and_wrong_public_fingerprint(self):
        environment = {k: v for k, v in os.environ.items() if k != 'BITCOIN_ROOTS_GPG_SK'}
        environment['REQUIRE_RELEASE_SIGNATURE'] = '1'
        command = [ROOT / 'ci/release/sign-manifest.sh', self.fixture.manifest, self.public_key, self.fingerprint]
        result = subprocess.run(command, env=environment, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Required release signing key', result.stderr)
        command[-1] = '0' * 40
        result = subprocess.run(command, env={**environment, 'BITCOIN_ROOTS_GPG_SK': self.secret}, capture_output=True)
        self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
