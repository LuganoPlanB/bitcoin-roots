#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "ci/release/sign-manifest.sh"


@unittest.skipUnless(shutil.which("gpg"), "gpg is required")
class SignManifestTest(unittest.TestCase):
    def test_unsigned_path_succeeds_without_signature(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            manifest = work / "SHA512SUMS"
            manifest.write_text("manifest\n", encoding="utf-8")
            result = subprocess.run(
                [SCRIPT, manifest, work / "missing.asc", "0" * 40],
                env={key: value for key, value in os.environ.items() if key != "BITCOIN_ROOTS_GPG_SK"},
                capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0)
            self.assertIn("publishing an unsigned manifest", result.stderr)
            self.assertFalse((work / "SHA512SUMS.asc").exists())

    def test_signs_and_verifies_with_matching_key(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            key_home = work / "key-home"
            key_home.mkdir(mode=0o700)
            identity = "Roots Release Test <roots-release@example.invalid>"
            subprocess.run(
                ["gpg", "--batch", "--homedir", key_home, "--passphrase", "", "--quick-generate-key", identity, "ed25519", "sign", "0"],
                check=True, capture_output=True,
            )
            fingerprint = subprocess.run(
                ["gpg", "--batch", "--homedir", key_home, "--with-colons", "--list-keys", identity],
                check=True, capture_output=True, text=True,
            ).stdout.split("fpr:::::::::", 1)[1].split(":", 1)[0]
            public_key = work / "public.asc"
            public_key.write_bytes(subprocess.run(
                ["gpg", "--batch", "--homedir", key_home, "--armor", "--export", fingerprint],
                check=True, capture_output=True,
            ).stdout)
            secret_key = subprocess.run(
                ["gpg", "--batch", "--homedir", key_home, "--armor", "--export-secret-keys", fingerprint],
                check=True, capture_output=True, text=True,
            ).stdout
            manifest = work / "SHA512SUMS"
            manifest.write_text("manifest\n", encoding="utf-8")

            subprocess.run(
                [SCRIPT, manifest, public_key, fingerprint],
                env={**os.environ, "BITCOIN_ROOTS_GPG_SK": secret_key},
                check=True, capture_output=True,
            )
            signature = work / "SHA512SUMS.asc"
            self.assertTrue(signature.is_file())

            verify_home = work / "verify-home"
            verify_home.mkdir(mode=0o700)
            subprocess.run(["gpg", "--batch", "--homedir", verify_home, "--import", public_key], check=True, capture_output=True)
            subprocess.run(["gpg", "--batch", "--homedir", verify_home, "--verify", signature, manifest], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
