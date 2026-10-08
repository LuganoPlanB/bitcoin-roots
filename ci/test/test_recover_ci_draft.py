#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import copy
import importlib.util
import os
import sys
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'ci/release' / filename)
    module = importlib.util.module_from_spec(spec)
    with patch.object(sys, 'path', [str(ROOT / 'ci/release'), *sys.path]):
        spec.loader.exec_module(module)
    return module


recovery = load('recovery', 'recover-ci-draft.py')
creator = load('ci_creator_recovery', 'create-ci-draft.py')


class RecoveryTest(unittest.TestCase):
    def setUp(self):
        self.environment = {'GITHUB_REPOSITORY': recovery.REPOSITORY, 'GITHUB_REF': recovery.BRANCH,
                            'GITHUB_EVENT_NAME': 'workflow_dispatch', 'RECOVERY_RUN': str(recovery.RUN),
                            'RELEASE_TAG': recovery.TAG, 'RELEASE_COMMIT': recovery.SOURCE}
        self.original = {'repository': {'full_name': recovery.REPOSITORY},
                         'head_repository': {'full_name': recovery.REPOSITORY}, 'workflow_id': recovery.WORKFLOW,
                         'path': '.github/workflows/release.yml', 'event': 'push', 'head_branch': recovery.TAG,
                         'head_sha': recovery.SOURCE, 'status': 'completed', 'conclusion': 'failure'}
        self.jobs = [{'id': identity, 'name': name, 'status': 'completed', 'conclusion': 'success'}
                     for identity, name in recovery.PRODUCERS.items()]
        self.assets = [{'id': identity, 'name': name, 'expired': False, 'size_in_bytes': 10,
                        'workflow_run': {'id': recovery.RUN, 'head_sha': recovery.SOURCE}} for identity, name in recovery.ARTIFACTS.items()]
        self.releases = [[]]
        self.refs = {f'refs/tags/{recovery.TAG}': recovery.TAG_OBJECT,
                     f'refs/tags/{recovery.TAG}^{{}}': recovery.SOURCE,
                     'refs/heads/roots/30.3': recovery.SOURCE, 'refs/heads/main': recovery.MAIN}

    def api(self, path, pages=False):
        if path.endswith('/jobs?per_page=100'):
            return [{'total_count': len(self.jobs), 'jobs': self.jobs}]
        if path.endswith('/artifacts?per_page=100'):
            return [{'total_count': len(self.assets), 'artifacts': self.assets}]
        if path.startswith('releases?'):
            return self.releases
        if path == f'actions/runs/{recovery.RUN}':
            return self.original
        head = recovery.SOURCE if path.endswith('37835146685') else '673c29ac0e8ad616a6054c2325b9bc50e84b7ac0'
        return {'repository': {'full_name': recovery.REPOSITORY}, 'workflow_id': 352575464, 'path': '.github/workflows/ci.yml',
                'event': 'pull_request', 'head_sha': head, 'status': 'completed', 'conclusion': 'success'}

    def verify(self):
        with patch.dict(os.environ, self.environment), patch.object(recovery, 'api', side_effect=self.api), \
                patch.object(recovery, 'run', return_value='\n'.join(f'{oid}\t{ref}' for ref, oid in self.refs.items())):
            return recovery.verify()

    def test_exact_fixed_recovery_passes(self):
        self.assertEqual(self.verify()['artifacts'], recovery.ARTIFACTS)

    def test_wrong_branch_input_repository_and_event_fail(self):
        for key in self.environment:
            with self.subTest(key=key):
                original = self.environment[key]
                self.environment[key] = 'wrong'
                with self.assertRaises(ValueError):
                    self.verify()
                self.environment[key] = original

    def test_wrong_original_run_identity_fails(self):
        for key in ('workflow_id', 'path', 'event', 'head_branch', 'head_sha', 'status', 'conclusion', 'repository'):
            with self.subTest(key=key):
                original = copy.deepcopy(self.original)
                self.original[key] = {} if key == 'repository' else 'wrong'
                with self.assertRaises(ValueError):
                    self.verify()
                self.original = original

    def test_failed_missing_duplicate_or_renamed_producer_fails(self):
        for failure in ('failed', 'missing', 'duplicate', 'renamed'):
            original = copy.deepcopy(self.jobs)
            if failure == 'failed':
                self.jobs[0]['conclusion'] = 'failure'
            elif failure == 'missing':
                self.jobs.pop()
            elif failure == 'duplicate':
                self.jobs.append(self.jobs[0])
            else:
                self.jobs[0]['id'] += 1
            with self.subTest(failure=failure), self.assertRaises(ValueError):
                self.verify()
            self.jobs = original

    def test_changed_expired_empty_missing_or_wrong_run_artifact_fails(self):
        for key, value in [('id', 1), ('name', 'wrong'), ('expired', True), ('expired', None), ('size_in_bytes', 0),
                           ('workflow_run', {'id': 1})]:
            original = copy.deepcopy(self.assets)
            self.assets[0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.verify()
            self.assets = original
        self.assets.pop()
        with self.assertRaises(ValueError):
            self.verify()

    def test_changed_live_refs_or_existing_release_fail(self):
        for ref in self.refs:
            original = self.refs[ref]
            self.refs[ref] = '0' * 40
            with self.subTest(ref=ref), self.assertRaises(ValueError):
                self.verify()
            self.refs[ref] = original
        self.releases = [[{'tag_name': recovery.TAG, 'draft': True}]]
        with self.assertRaises(ValueError):
            self.verify()

    def test_source_binding_preserves_tagged_checkout_and_default(self):
        args = SimpleNamespace(source_dir=ROOT, tag=recovery.TAG, commit=recovery.SOURCE,
                               run_id=str(recovery.RUN), assembly_run_id='123')
        responses = {('git', 'rev-parse', '--show-toplevel'): str(ROOT),
                     ('git', 'status', '--porcelain'): '',
                     ('git', 'rev-parse', 'refs/tags/v30.3-roots.1^{tag}'): recovery.TAG_OBJECT}
        with patch.object(creator.draft, 'ROOT', ROOT), patch.object(creator.draft, 'SCRIPTS', ROOT / 'ci/release'), \
                patch.object(creator.draft, 'run', side_effect=lambda command: responses[tuple(command)]):
            creator.bind_source(args)
            self.assertEqual(creator.draft.ROOT, ROOT)
            responses[('git', 'status', '--porcelain')] = ' M source'
            with self.assertRaises(ValueError):
                creator.bind_source(args)
            args.source_dir = None
            args.assembly_run_id = None
            creator.bind_source(args)
            self.assertEqual(creator.draft.ROOT, ROOT)
            args.assembly_run_id = '123'
            with self.assertRaises(ValueError):
                creator.bind_source(args)

    def test_source_binding_rejects_wrong_tag_run_commit_or_directory(self):
        args = SimpleNamespace(source_dir=ROOT, tag=recovery.TAG, commit=recovery.SOURCE,
                               run_id=str(recovery.RUN), assembly_run_id='123')
        for name in ('tag', 'commit', 'run_id', 'assembly_run_id'):
            original = getattr(args, name)
            setattr(args, name, '' if name == 'assembly_run_id' else 'wrong')
            with self.subTest(name=name), self.assertRaises(ValueError):
                creator.bind_source(args)
            setattr(args, name, original)
        with patch.object(creator.draft, 'run', return_value='/wrong/source'), self.assertRaises(ValueError):
            creator.bind_source(args)

    def test_incomplete_paginated_inventory_fails(self):
        with patch.object(recovery, 'api', return_value=[{'total_count': 7, 'jobs': self.jobs}]), self.assertRaises(ValueError):
            recovery.inventory('unused', 'jobs')

    def test_failed_source_ci_proof_fails(self):
        original = self.api
        def failed(path, pages=False):
            value = original(path, pages)
            if path.endswith('37835146685'):
                value['conclusion'] = 'failure'
            return value
        with patch.dict(os.environ, self.environment), patch.object(recovery, 'api', side_effect=failed), self.assertRaises(ValueError):
            recovery.verify()

    def test_default_jobs_unchanged_and_recovery_signs_only_after_proofs(self):
        workflow = (ROOT / '.github/workflows/release.yml').read_text()
        self.assertEqual(workflow.count("if: github.event_name != 'workflow_dispatch' || inputs.recovery_run == ''"), 4)
        normal, block = workflow.split('  recover-signed-draft:', 1)
        self.assertIn("if: github.event_name == 'push'", normal)
        self.assertIn('run: ./ci/test_run_all.sh', normal)
        self.assertNotIn('./ci/test_run_all.sh', block)
        self.assertIn('environment: release', block)
        self.assertIn('artifact-ids: ' + ','.join(map(str, recovery.ARTIFACTS)), block)
        self.assertIn('--run-id 37849202986', block)
        self.assertIn('--assembly-run-id "$GITHUB_RUN_ID"', block)
        self.assertLess(block.index('cmp "$RUNNER_TEMP/recovery-expected.patch"'), block.index('BITCOIN_ROOTS_GPG_SK:'))
        self.assertNotIn('--clobber', block)


if __name__ == '__main__':
    unittest.main()
