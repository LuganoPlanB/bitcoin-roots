#!/usr/bin/env python3
# Copyright (C) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Prepared inputs are verified and reused without per-consumer network access."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('shared_download_utils', ROOT / 'test/download_utils.py')
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


class SharedTestAssetsTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='qa-assets-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name) / 'prepared inputs ü'
        self.unit = self.root / 'unit_test_data'
        self.unit.mkdir(parents=True)
        (self.root / 'LICENSE').write_bytes(b'Synthetic test input license notice\n')
        self.data = b'[{"synthetic": "shared script vectors"}]\n'
        (self.unit / 'script_assets_test.json').write_bytes(self.data)
        self.destination = Path(temporary.name) / 'Windows output ü'
        for patcher in [
            mock.patch.object(assets, 'SCRIPT_ASSETS_SHA256', hashlib.sha256(self.data).hexdigest()),
            mock.patch.dict(os.environ, {'ROOTS_QA_ASSETS_DIR': str(self.root)}),
        ]:
            patcher.start()
            self.addCleanup(patcher.stop)

    def prepare(self, kind='unit_test_data', revision=None):
        with mock.patch.object(assets.subprocess, 'check_output', return_value=revision or assets.QA_ASSETS_REVISION):
            assets.prepare_qa_assets(self.root, kind)

    def test_prepared_inputs_copy_to_windows_layout_without_http(self):
        self.prepare()
        with mock.patch.object(assets, 'download_from_url') as network:
            assets.download_script_assets(self.destination)
            assets.download_script_assets(self.destination)
        network.assert_not_called()
        self.assertEqual((self.destination / 'script_assets_test.json').read_bytes(), self.data)
        self.assertEqual((self.unit / assets.QA_LICENSE).read_bytes(), (self.root / 'LICENSE').read_bytes())

    def test_linux_consumer_reads_prepared_directory_without_writing(self):
        self.prepare()
        with mock.patch.object(assets, 'download_from_url') as network, mock.patch.object(assets.shutil, 'copyfile') as copy:
            assets.download_script_assets(self.unit)
        network.assert_not_called()
        copy.assert_not_called()

    def test_missing_manifest_does_not_fall_back_to_http(self):
        with mock.patch.object(assets, 'download_from_url') as network, self.assertRaises(OSError):
            assets.download_script_assets(self.destination)
        network.assert_not_called()
        self.assertFalse(self.destination.exists())

    def test_missing_corrupt_and_extra_files_are_rejected(self):
        self.prepare()
        path = self.unit / 'script_assets_test.json'
        manifest = (self.unit / assets.QA_MANIFEST).read_bytes()
        for fault in ['missing', 'truncated', 'extra', 'revision']:
            with self.subTest(fault=fault):
                path.write_bytes(self.data)
                (self.unit / assets.QA_MANIFEST).write_bytes(manifest)
                if fault == 'missing':
                    path.unlink()
                elif fault == 'truncated':
                    path.write_bytes(self.data[:8])
                elif fault == 'extra':
                    (self.unit / 'unexpected').write_bytes(b'not prepared')
                else:
                    value = json.loads(manifest)
                    value['revision'] = '0' * 40
                    (self.unit / assets.QA_MANIFEST).write_text(json.dumps(value))
                with mock.patch.object(assets, 'download_from_url') as network, self.assertRaises(RuntimeError):
                    assets.download_script_assets(self.destination)
                network.assert_not_called()
                (self.unit / 'unexpected').unlink(missing_ok=True)

    def test_unexpected_checkout_and_script_vectors_are_rejected(self):
        with self.assertRaises(RuntimeError):
            self.prepare(revision='0' * 40)
        (self.unit / 'script_assets_test.json').write_bytes(b'wrong source data')
        with self.assertRaises(RuntimeError):
            self.prepare()
        self.assertFalse((self.unit / assets.QA_MANIFEST).exists())

    @unittest.skipIf(os.name == 'nt', 'symlink creation requires Windows developer privileges')
    def test_prepared_symlink_is_rejected(self):
        self.prepare()
        path = self.unit / 'script_assets_test.json'
        target = self.unit / 'outside'
        target.write_bytes(self.data)
        path.unlink()
        path.symlink_to(target)
        with self.assertRaises(RuntimeError):
            assets.verify_qa_assets(self.root, 'unit_test_data')

    def test_fuzz_handoff_checks_every_file_and_never_clones(self):
        fuzz = self.root / 'fuzz_corpora' / 'script'
        fuzz.mkdir(parents=True)
        seed = fuzz / 'synthetic-seed'
        seed.write_bytes(b'synthetic corpus input')
        self.prepare('fuzz_corpora')
        with mock.patch.object(assets.subprocess, 'run') as git:
            assets.download_fuzz_assets(self.destination)
            git.assert_not_called()
            self.assertEqual((self.destination / 'fuzz_corpora/script/synthetic-seed').read_bytes(), seed.read_bytes())
            seed.write_bytes(b'truncated')
            with self.assertRaises(RuntimeError):
                assets.download_fuzz_assets(self.destination)
            git.assert_not_called()

    @unittest.skipIf(os.name == 'nt', 'POSIX read-only mode enforcement')
    def test_readonly_fuzz_handoff_allows_missing_target_directories(self):
        corpus = self.root / 'fuzz_corpora'
        target = corpus / 'script'
        target.mkdir(parents=True)
        seed = target / 'synthetic-seed'
        seed.write_bytes(b'public synthetic input')
        self.prepare('fuzz_corpora')
        manifest = (corpus / assets.QA_MANIFEST).read_bytes()
        for path in [seed, corpus / assets.QA_MANIFEST]:
            path.chmod(0o400)
        for directory in [target, corpus]:
            directory.chmod(0o500)
        try:
            with mock.patch.object(assets.subprocess, 'run') as git:
                assets.download_fuzz_assets(self.destination)
            git.assert_not_called()
            working = self.destination / 'fuzz_corpora'
            # The actual fuzz runner creates absent compiled-target directories.
            for name in ['carrier_policy_weight', 'dynamic_dust_fee', 'policy_args', 'subdust_fee_penalty']:
                (working / name).mkdir()
                (working / name / 'new-seed').write_bytes(b'new synthetic input')
            (working / 'script/synthetic-seed').write_bytes(b'changed working input')
            self.assertEqual(seed.read_bytes(), b'public synthetic input')
            self.assertEqual((corpus / assets.QA_MANIFEST).read_bytes(), manifest)
            assets.verify_qa_assets(self.root, 'fuzz_corpora')
        finally:
            for directory in [corpus, target]:
                directory.chmod(0o700)
            for path in [seed, corpus / assets.QA_MANIFEST]:
                path.chmod(0o600)

    def test_empty_fuzz_corpus_is_rejected(self):
        (self.root / 'fuzz_corpora').mkdir()
        with self.assertRaises(RuntimeError):
            self.prepare('fuzz_corpora')

    def test_fuzz_copy_retry_cleans_partial_data_and_preserves_changed_inputs(self):
        target = self.root / 'fuzz_corpora' / 'script'
        target.mkdir(parents=True)
        (target / 'seed').write_bytes(b'synthetic input')
        self.prepare('fuzz_corpora')
        copyfile = assets.shutil.copyfile

        def interrupted(source, destination):
            copyfile(source, destination)
            raise OSError('synthetic interrupted copy')

        with mock.patch.object(assets.shutil, 'copyfile', side_effect=interrupted), self.assertRaises(OSError):
            assets.download_fuzz_assets(self.destination)
        self.assertEqual(list(self.destination.iterdir()), [])
        assets.download_fuzz_assets(self.destination)
        assets.download_fuzz_assets(self.destination)
        working_seed = self.destination / 'fuzz_corpora/script/seed'
        working_seed.write_bytes(b'newly discovered input')
        with self.assertRaises(RuntimeError):
            assets.download_fuzz_assets(self.destination)
        self.assertEqual(working_seed.read_bytes(), b'newly discovered input')

    def test_standalone_valid_file_is_reused_without_network(self):
        with mock.patch.dict(os.environ, {'ROOTS_QA_ASSETS_DIR': ''}), mock.patch.object(assets, 'download_from_url') as network:
            assets.download_script_assets(self.unit)
        network.assert_not_called()

    def test_standalone_download_retries_and_publishes_only_valid_bytes(self):
        attempts = []

        def download(url, path):
            attempts.append(url)
            path.write_bytes(self.data[:8] if len(attempts) == 1 else self.data)
            self.assertFalse((self.destination / 'script_assets_test.json').exists())
            if len(attempts) == 1:
                raise OSError('synthetic interrupted transfer')

        with mock.patch.dict(os.environ, {'ROOTS_QA_ASSETS_DIR': ''}), mock.patch.object(assets, 'download_from_url', side_effect=download), mock.patch.object(assets.time, 'sleep'):
            assets.download_script_assets(self.destination)
        self.assertEqual(len(attempts), 2)
        self.assertIn(assets.QA_ASSETS_REVISION, attempts[0])
        self.assertEqual(list(self.destination.iterdir()), [self.destination / 'script_assets_test.json'])
        self.assertEqual((self.destination / 'script_assets_test.json').read_bytes(), self.data)

    def test_standalone_corrupt_download_is_not_published_or_retried(self):
        def download(_url, path):
            path.write_bytes(b'corrupt synthetic data')

        with mock.patch.dict(os.environ, {'ROOTS_QA_ASSETS_DIR': ''}), mock.patch.object(assets, 'download_from_url', side_effect=download) as network, self.assertRaises(RuntimeError):
            assets.download_script_assets(self.destination)
        self.assertEqual(network.call_count, 1)
        self.assertEqual(list(self.destination.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
