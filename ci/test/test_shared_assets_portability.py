#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Portable transport/copy regressions using the actual restore action shell."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import unittest
from unittest.mock import patch

import test_shared_assets as fixtures

assets, REVISION, ROOT = fixtures.assets, fixtures.REVISION, fixtures.ROOT


def restore_block():
    text = (ROOT / '.github/actions/restore-test-assets/action.yml').read_text()
    return '\n'.join(line[8:] for line in text.split('      run: |\n', 1)[1].splitlines())


# Do not inherit SharedAssetsTest: inherited cases would duplicate discovery.
class AssetPortabilityTest(unittest.TestCase):
    setUp = fixtures.SharedAssetsTest.setUp
    prepare = fixtures.SharedAssetsTest.prepare
    prepared = fixtures.SharedAssetsTest.prepared

    def action(self, runner_temp, kind, revision=REVISION):
        (self.root / 'test').mkdir(exist_ok=True)
        helper = self.root / 'test/download_utils.py'
        if not helper.exists():
            shutil.copyfile(ROOT / 'test/download_utils.py', helper)
        output = self.root / 'github-env'
        output.unlink(missing_ok=True)
        result = subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', restore_block()], cwd=self.root, env=os.environ | {'RUNNER_TEMP': str(runner_temp), 'KIND': kind, 'QA_ASSETS_REVISION': revision, 'PYTHON': sys.executable, 'GITHUB_ENV': str(output)}, capture_output=True, text=True, timeout=15)
        return result, output

    def archive(self, runner_temp, kind):
        folder = self.root / runner_temp / 'qa-assets'
        folder.mkdir(parents=True, exist_ok=True)
        archive = folder / (kind + '.tar')
        subprocess.run(['tar', '-cf', str(archive), '-C', str(self.source), kind], check=True, capture_output=True)
        return archive

    def test_drive_letter_failure_and_relative_action_unit_fuzz_roundtrip(self):
        for runner in ['D:/runner', 'D:/runner with spaces', 'D:/runner ₿ Unicode']:
            for kind in ['unit_test_data', 'fuzz_corpora']:
                with self.subTest(runner=runner, kind=kind):
                    self.prepare(kind)
                    archive = self.archive(Path(runner), kind)
                    drive_path = str(archive.relative_to(self.root))
                    # GNU tar interprets D: as a remote host; false prevents any
                    # network transport while exercising that original parser.
                    old = subprocess.run(['tar', '--rsh-command=false', '-xf', drive_path], cwd=self.root, capture_output=True, text=True, timeout=5)
                    self.assertNotEqual(old.returncode, 0)
                    self.assertIn('D', old.stderr)
                    result, output = self.action(Path(runner), kind)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn('QA_ASSETS_DIR=' + runner + '/qa-assets', output.read_text())
                    self.assertEqual(assets.qa_files(archive.parent / kind), assets.qa_files(self.source / kind))
                    self.assertEqual(list(archive.parent.glob('.restore.*')), [])
                    with tarfile.open(archive) as tar:
                        self.assertTrue(all(member.name == kind or member.name.startswith(kind + '/') for member in tar))

    def test_corrupt_transfer_wrong_revision_and_retry_never_export_partial_inputs(self):
        for kind in ['unit_test_data', 'fuzz_corpora']:
            with self.subTest(kind=kind):
                self.prepare(kind)
                runner = Path('D:/retry ₿')
                archive = self.archive(runner, kind)
                correct = archive.read_bytes()
                archive.write_bytes(correct[:100])
                result, output = self.action(runner, kind)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())
                self.assertFalse((archive.parent / kind).exists())
                self.assertEqual(list(archive.parent.glob('.restore.*')), [])
                archive.write_bytes(correct)
                result, output = self.action(runner, kind, 'b' * 40)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())
                self.assertFalse((archive.parent / kind).exists())
                self.assertEqual(list(archive.parent.glob('.restore.*')), [])
                result, output = self.action(runner, kind)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(output.exists())

    def test_failed_restore_preserves_previously_verified_shared_inputs(self):
        self.prepare('fuzz_corpora')
        runner = Path('D:/preserve')
        archive = self.archive(runner, 'fuzz_corpora')
        result, _ = self.action(runner, 'fuzz_corpora')
        self.assertEqual(result.returncode, 0, result.stderr)
        before = assets.qa_files(archive.parent / 'fuzz_corpora')
        archive.write_bytes(b'partial')
        result, output = self.action(runner, 'fuzz_corpora')
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())
        self.assertEqual(assets.qa_files(archive.parent / 'fuzz_corpora'), before)

    def test_invalid_kind_or_revision_never_mutates_or_exports(self):
        runner = Path('D:/invalid')
        self.archive(runner, 'fuzz_corpora')
        for kind, revision in [('../escape', REVISION), ('fuzz_corpora', ''), ('fuzz_corpora', 'invalid')]:
            with self.subTest(kind=kind, revision=revision):
                result, output = self.action(runner, kind, revision)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(output.exists())

    def test_independent_writable_fuzz_copy_and_changed_corpus_preserved(self):
        self.prepare('fuzz_corpora')
        self.prepared()
        source = self.source / 'fuzz_corpora'
        before = assets.qa_files(source)
        for path in source.rglob('*'):
            path.chmod(0o555 if path.is_dir() else 0o444)
        source.chmod(0o555)
        self.addCleanup(lambda: [p.chmod(0o755 if p.is_dir() else 0o644) for p in [source, *source.rglob('*')]])
        first, second = self.root / 'first', self.root / 'second'
        assets.download_fuzz_assets(first)
        assets.download_fuzz_assets(second)
        assets.download_fuzz_assets(first)  # Exact copies may be reused.
        changed = first / 'fuzz_corpora/target/.hidden'
        self.assertNotEqual(changed.stat().st_ino, (source / 'target/.hidden').stat().st_ino)
        changed.write_bytes(b'new fuzzer seed')
        (first / 'fuzz_corpora/new-target').mkdir()
        with self.assertRaises(RuntimeError):
            assets.download_fuzz_assets(first)
        self.assertEqual(changed.read_bytes(), b'new fuzzer seed')
        self.assertEqual(assets.qa_files(source), before)
        self.assertEqual(assets.qa_files(second / 'fuzz_corpora'), before)
        with self.assertRaises(RuntimeError):
            assets.download_fuzz_assets(self.source)

    def test_interrupted_prepared_copies_cleanup_and_preserve_destinations(self):
        for kind in ['unit_test_data', 'fuzz_corpora']:
            self.prepare(kind)
        self.prepared()
        unit = self.root / 'unit'
        unit.mkdir()
        (unit / 'script_assets_test.json').write_text('["existing"]')
        def partial(_source, target):
            target.write_bytes(b'partial')
            raise OSError('interrupted copy')
        with patch.object(assets.shutil, 'copyfile', side_effect=partial):
            for acquire, destination in [(assets.download_script_assets, unit), (assets.download_fuzz_assets, self.root / 'fuzz')]:
                with self.subTest(acquire=acquire.__name__), self.assertRaises(OSError):
                    acquire(destination)
        self.assertEqual((unit / 'script_assets_test.json').read_text(), '["existing"]')
        self.assertEqual(list(unit.iterdir()), [unit / 'script_assets_test.json'])
        self.assertEqual(list((self.root / 'fuzz').iterdir()), [])
        assets.download_script_assets(unit)
        assets.download_fuzz_assets(self.root / 'fuzz')

    def test_unit_transfer_retry_and_failure_cleanup(self):
        destination = self.root / 'direct unit ₿'
        attempts = []
        def transfer(_url, path):
            attempts.append(path)
            path.write_text('[1]' if len(attempts) == 2 else 'partial')
            if len(attempts) == 1:
                raise OSError('interrupted transfer')
        with patch.object(assets, 'download_from_url', side_effect=transfer), patch.object(assets.time, 'sleep'):
            assets.download_script_assets(destination)
        self.assertEqual(len(attempts), 2)
        self.assertEqual(list(destination.iterdir()), [destination / 'script_assets_test.json'])
        failure = self.root / 'direct failure'
        with patch.object(assets, 'download_from_url', side_effect=OSError('offline')), patch.object(assets.time, 'sleep'), self.assertRaises(OSError):
            assets.download_script_assets(failure)
        self.assertEqual(list(failure.iterdir()), [])

    def test_standalone_fuzz_partial_clone_cleanup_and_retry(self):
        destination = self.root / 'direct fuzz ₿'
        attempts = []
        def clone(command, **_kwargs):
            if command[1] != 'clone':
                return
            checkout = Path(command[-1])
            attempts.append(checkout)
            (checkout / 'fuzz_corpora').mkdir(parents=True)
            (checkout / 'fuzz_corpora/seed').write_bytes(b'seed')
            if len(attempts) == 1:
                raise subprocess.CalledProcessError(128, command)
            (checkout / 'LICENSE').write_text('fixture license')
        with patch.object(assets.subprocess, 'run', side_effect=clone), patch.object(assets.subprocess, 'check_output', return_value=REVISION):
            with self.assertRaises(subprocess.CalledProcessError):
                assets.download_fuzz_assets(destination)
            self.assertFalse(destination.exists())
            self.assertFalse(attempts[0].exists())
            assets.download_fuzz_assets(destination)
        self.assertEqual(list(self.root.glob('.qa-assets-*')), [])
        self.assertTrue((destination / 'fuzz_corpora/seed').is_file())
