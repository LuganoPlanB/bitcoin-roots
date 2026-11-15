#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Exercise shared-input acquisition with synthetic inputs and no network."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('asset_download', ROOT / 'test/download_utils.py')
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)
REVISION = 'a' * 40


class SharedAssetsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'shared inputs ₿'
        self.source.mkdir()
        (self.source / 'LICENSE').write_text('Synthetic MIT fixture\n')
        (self.source / 'unit_test_data').mkdir()
        (self.source / 'unit_test_data/script_assets_test.json').write_text('[{"fixture": true}]')
        (self.source / 'fuzz_corpora/target').mkdir(parents=True)
        (self.source / 'fuzz_corpora/target/.hidden').write_bytes(b'hidden seed')
        self.environment = patch.dict(os.environ, {}, clear=False)
        self.environment.start()
        self.addCleanup(self.environment.stop)
        os.environ.pop('QA_ASSETS_DIR', None)
        os.environ.pop('QA_ASSETS_REVISION', None)

    def prepare(self, kind):
        with patch.object(assets.subprocess, 'check_output', return_value=REVISION):
            assets.prepare_qa_assets(self.source, kind)

    def prepared(self):
        os.environ.update(QA_ASSETS_DIR=str(self.source), QA_ASSETS_REVISION=REVISION)

    def test_both_kinds_record_revision_license_and_hidden_file_hashes(self):
        for kind in ['unit_test_data', 'fuzz_corpora']:
            with self.subTest(kind=kind):
                self.prepare(kind)
                self.prepared()
                files = assets.verify_qa_assets(self.source, kind)
                self.assertIn(assets.QA_LICENSE, files)
                manifest = json.loads((self.source / kind / assets.QA_MANIFEST).read_text())
                self.assertEqual(manifest['revision'], REVISION)
                self.assertEqual(manifest['files'], files)
        self.assertIn('target/.hidden', files)

    def test_prepared_unit_and_fuzz_use_no_network(self):
        self.prepare('unit_test_data')
        self.prepare('fuzz_corpora')
        self.prepared()
        with patch.object(assets, 'download_from_url', side_effect=AssertionError('network')), patch.object(assets.subprocess, 'run', side_effect=AssertionError('git')):
            assets.download_script_assets(self.root / 'unit')
            assets.download_fuzz_assets(self.root / 'working')
        self.assertEqual((self.root / 'unit/script_assets_test.json').read_bytes(), (self.source / 'unit_test_data/script_assets_test.json').read_bytes())
        self.assertEqual(assets.qa_files(self.root / 'working/fuzz_corpora'), assets.qa_files(self.source / 'fuzz_corpora'))

    def test_prepared_missing_or_corrupt_inputs_never_fall_back(self):
        for kind, acquire in [('unit_test_data', lambda: assets.download_script_assets(self.root / 'unit')), ('fuzz_corpora', lambda: assets.download_fuzz_assets(self.root / 'working'))]:
            with self.subTest(kind=kind):
                self.prepared()
                with patch.object(assets, 'download_from_url', side_effect=AssertionError('network')), patch.object(assets.subprocess, 'run', side_effect=AssertionError('git')):
                    with self.assertRaises((RuntimeError, OSError, ValueError)):
                        acquire()
                    self.prepare(kind)
                    (self.source / kind / assets.QA_LICENSE).write_text('corrupt')
                    with self.assertRaises(RuntimeError):
                        acquire()

    def test_wrong_revision_missing_license_and_changed_file_fail(self):
        for kind in ['unit_test_data', 'fuzz_corpora']:
            with self.subTest(kind=kind):
                self.prepare(kind)
                self.prepared()
                os.environ['QA_ASSETS_REVISION'] = 'b' * 40
                with self.assertRaises(RuntimeError):
                    assets.verify_qa_assets(self.source, kind)
                os.environ['QA_ASSETS_REVISION'] = REVISION
                (self.source / kind / assets.QA_LICENSE).unlink()
                with self.assertRaises(RuntimeError):
                    assets.verify_qa_assets(self.source, kind)

    def test_manifest_cannot_omit_license(self):
        self.prepare('fuzz_corpora')
        directory = self.source / 'fuzz_corpora'
        (directory / assets.QA_LICENSE).unlink()
        manifest = directory / assets.QA_MANIFEST
        data = json.loads(manifest.read_text())
        del data['files'][assets.QA_LICENSE]
        manifest.write_text(json.dumps(data))
        with self.assertRaises(RuntimeError):
            assets.verify_qa_assets(self.source, 'fuzz_corpora')

    def test_corrupt_manifest_and_empty_license_fail(self):
        for kind in ['unit_test_data', 'fuzz_corpora']:
            with self.subTest(kind=kind):
                self.prepare(kind)
                (self.source / kind / assets.QA_MANIFEST).write_text('invalid JSON')
                with self.assertRaises(ValueError):
                    assets.verify_qa_assets(self.source, kind)
                self.prepare(kind)
                (self.source / kind / assets.QA_LICENSE).write_text('')
                with self.assertRaises(RuntimeError):
                    assets.verify_qa_assets(self.source, kind)

    def test_empty_inputs_and_invalid_vectors_rejected(self):
        for content in ['', '{}', '[]', 'invalid']:
            (self.source / 'unit_test_data/script_assets_test.json').write_text(content)
            with self.subTest(content=content), self.assertRaises(RuntimeError):
                self.prepare('unit_test_data')
        (self.source / 'fuzz_corpora/target/.hidden').unlink()
        with self.assertRaises(RuntimeError):
            self.prepare('fuzz_corpora')

    def test_invalid_revision_and_missing_license_rejected_at_prepare(self):
        with patch.object(assets.subprocess, 'check_output', return_value='not-a-sha'), self.assertRaises(RuntimeError):
            assets.prepare_qa_assets(self.source, 'fuzz_corpora')
        (self.source / 'LICENSE').unlink()
        with self.assertRaises(RuntimeError):
            self.prepare('fuzz_corpora')

    def test_revision_without_prepared_directory_fails_closed(self):
        os.environ['QA_ASSETS_REVISION'] = REVISION
        for acquire in [assets.download_script_assets, assets.download_fuzz_assets]:
            with self.subTest(acquire=acquire.__name__), self.assertRaises(RuntimeError):
                acquire(self.root / 'destination')

    def test_standalone_unit_download_and_reuse(self):
        destination = self.root / 'unit'
        def download(_url, path):
            path.write_text('[1]')
        with patch.object(assets, 'download_from_url', side_effect=download) as fetch:
            assets.download_script_assets(destination)
            assets.download_script_assets(destination)
            self.assertEqual(fetch.call_count, 1)
        (destination / 'script_assets_test.json').write_text('{}')
        with self.assertRaises(RuntimeError):
            assets.download_script_assets(destination)

    def test_standalone_fuzz_clones_once_and_reports_revision(self):
        destination = self.root / 'standalone'
        def git(command, **_kwargs):
            if command[1] == 'clone':
                checkout = Path(command[-1])
                (checkout / 'fuzz_corpora').mkdir(parents=True)
                (checkout / 'fuzz_corpora/seed').write_bytes(b'seed')
                (checkout / 'LICENSE').write_text('Synthetic license')
        with patch.object(assets.subprocess, 'run', side_effect=git) as run, patch.object(assets.subprocess, 'check_output', return_value=REVISION):
            assets.download_fuzz_assets(destination)
            assets.download_fuzz_assets(destination)
        self.assertEqual(sum(call.args[0][1] == 'clone' for call in run.call_args_list), 1)
        self.assertEqual(sum(call.args[0][-2:] == ['log', '-1'] for call in run.call_args_list), 2)

    def test_empty_standalone_fuzz_is_rejected(self):
        destination = self.root / 'empty'
        (destination / 'fuzz_corpora').mkdir(parents=True)
        with self.assertRaises(RuntimeError):
            assets.download_fuzz_assets(destination)

    def test_prepared_symlink_and_unlisted_hidden_files_rejected(self):
        self.prepare('fuzz_corpora')
        directory = self.source / 'fuzz_corpora'
        (directory / '.extra').write_text('unlisted')
        with self.assertRaises(RuntimeError):
            assets.verify_qa_assets(self.source, 'fuzz_corpora')
        (directory / '.extra').unlink()
        (directory / 'link').symlink_to(directory / 'target/.hidden')
        with self.assertRaises(RuntimeError):
            assets.verify_qa_assets(self.source, 'fuzz_corpora')

    def test_cli_prepared_missing_inputs_fail(self):
        self.prepared()
        for command in ['unit', 'fuzz']:
            result = subprocess.run([os.sys.executable, str(ROOT / 'test/download_utils.py'), command, str(self.root / command)], capture_output=True, timeout=5)
            self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
