#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


promotion = load('promotion', 'ci/verify-promotion-ci.py')
classifier = load('classifier', 'ci/change-classifier.py')


class PromotionCITest(unittest.TestCase):
    def setUp(self):
        self.repository = 'owner/roots'
        self.head, self.base, self.candidate = 'a' * 40, 'b' * 40, 'c' * 40
        repo = {'full_name': self.repository}
        self.event = {'repository': repo, 'pull_request': {
            'head': {'ref': 'promote/30.3-fix', 'sha': self.head, 'repo': repo},
            'base': {'ref': 'main', 'sha': self.base, 'repo': repo}, 'labels': []}}
        self.source_pr = {'head': {'sha': self.candidate, 'repo': repo, 'ref': 'topic/30.3/fix'},
                          'base': {'ref': 'roots/30.3', 'repo': repo}}
        self.run = {'id': 10, 'head_sha': self.candidate, 'event': 'pull_request',
                    'repository': repo, 'head_repository': repo,
                    'workflow_id': 4, 'path': '.github/workflows/ci.yml',
                    'status': 'completed', 'conclusion': 'success',
                    'created_at': '2026-10-08T10:00:00Z', 'head_branch': 'topic/30.3/fix',
                    'pull_requests': []}
        self.runs = [self.run]
        self.jobs = [{'name': name, 'status': 'completed', 'conclusion': 'success'}
                     for name in ('classify changes', 'lint', 'required result')]
        self.differences = ['AGENTS.md', 'doc/website/index.md', 'test/lint/lint-files.py']

    def git(self, *args):
        if args[1] == 'rev-parse':
            return self.head
        if args[1] == 'show':
            return self.base + ' ' + self.candidate
        if args[1] == 'ls-remote':
            return self.candidate + '\trefs/heads/roots/30.3'
        self.assertEqual(args, ('git', 'diff', '--no-renames', '--name-only', self.candidate, self.head))
        return '\n'.join(self.differences)

    def api(self, path, paginated=False):
        if path.endswith('/actions/workflows/ci.yml'):
            return {'id': 4}
        if '/actions/workflows/ci.yml/runs?' in path:
            self.assertTrue(paginated)
            self.assertIn('head_sha=' + self.candidate, path)
            return [{'total_count': len(self.runs), 'workflow_runs': self.runs}]
        if '/commits/' in path and '/pulls?' in path:
            self.assertTrue(paginated)
            return [[self.source_pr]]
        self.assertTrue(paginated)
        self.assertIn('/actions/runs/10/jobs?', path)
        return [{'total_count': len(self.jobs), 'jobs': self.jobs}]

    def verify(self):
        return promotion.verify(self.event, self.git, self.api)

    def test_exact_successful_canonical_ci_and_precise_tree_parity(self):
        self.assertEqual(self.run['pull_requests'], [])
        self.assertEqual(self.verify()['candidate'], self.candidate)
        for path in ('ci/release/runner-notices.py', '.github/workflows/ci.yml',
                     'src/node.cpp', 'test/functional/feature.py'):
            self.differences = [path]
            with self.assertRaisesRegex(ValueError, 'differs from canonical'):
                self.verify()

    def test_ordinary_pr_never_reuses_and_fork_promotion_is_rejected(self):
        self.event['pull_request']['head']['ref'] = 'topic/30.3/fix'
        self.assertIsNone(self.verify())
        self.event['pull_request']['head']['ref'] = 'promote/30.3-fix'
        self.event['pull_request']['head']['repo'] = {'full_name': 'fork/roots'}
        with self.assertRaisesRegex(ValueError, 'same repository'):
            self.verify()

    def test_latest_failed_or_running_run_cannot_use_older_success(self):
        newer = {**self.run, 'id': 11, 'created_at': '2026-10-08T11:00:00Z'}
        self.runs.append(newer)
        for status, conclusion in [('completed', 'failure'), ('in_progress', None),
                                   ('completed', 'cancelled')]:
            newer.update(status=status, conclusion=conclusion)
            with self.assertRaisesRegex(ValueError, 'Latest exact candidate'):
                self.verify()

    def test_wrong_candidate_target_repo_workflow_and_missing_job_fail(self):
        for field, value in [('head_sha', 'd' * 40), ('workflow_id', 99),
                             ('path', '.github/workflows/release.yml'),
                             ('head_repository', {'full_name': 'fork/roots'})]:
            old = self.run[field]
            self.run[field] = value
            with self.assertRaisesRegex(ValueError, 'identity mismatch'):
                self.verify()
            self.run[field] = old
        self.source_pr['base']['ref'] = 'main'
        with self.assertRaisesRegex(ValueError, 'canonical-target'):
            self.verify()
        self.source_pr['base']['ref'] = 'roots/30.3'
        self.jobs.pop()
        with self.assertRaisesRegex(ValueError, 'required evidence'):
            self.verify()

    def test_ambiguous_associated_pr_or_wrong_run_branch_cannot_authorize_reuse(self):
        def ambiguous(path, paginated=False):
            result = self.api(path, paginated)
            if '/commits/' in path:
                result[0].append(dict(self.source_pr))
            return result
        with self.assertRaisesRegex(ValueError, 'unique exact canonical-target'):
            promotion.verify(self.event, self.git, ambiguous)
        self.run['head_branch'] = 'unrelated-branch'
        with self.assertRaisesRegex(ValueError, 'canonical-target'):
            self.verify()

    def test_incomplete_pagination_fails(self):
        def truncated(path, paginated=False):
            result = self.api(path, paginated)
            if '/runs?' in path:
                result[0]['total_count'] += 1
            return result
        with self.assertRaisesRegex(ValueError, 'inventory is incomplete'):
            promotion.verify(self.event, self.git, truncated)

    def test_git_parity_detects_mode_symlink_and_move_into_exception(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            def git(*args):
                return promotion.run('git', '-C', temporary, *args)
            git('init', '--quiet')
            git('config', 'user.name', 'CI parity fixture')
            git('config', 'user.email', 'fixture@invalid')
            (directory / 'src').mkdir()
            script = directory / 'src/tool.py'
            script.write_text('fixture\n')
            (directory / 'src/link').symlink_to('one')
            moved = directory / 'src/moved.txt'
            moved.write_text('file that must remain in product\n')
            git('add', '.')
            git('commit', '--quiet', '-m', 'fixture base')
            base = git('rev-parse', 'HEAD')
            script.chmod(0o755)
            (directory / 'src/link').unlink()
            (directory / 'src/link').symlink_to('two')
            (directory / 'doc/website').mkdir(parents=True)
            moved.rename(directory / 'doc/website/moved.txt')
            git('add', '.')
            git('commit', '--quiet', '-m', 'mode link and move')
            differences = git('diff', '--no-renames', '--name-only', base, 'HEAD').splitlines()
            self.assertTrue({'src/tool.py', 'src/link', 'src/moved.txt'} <= set(differences))

    def test_reuse_keeps_baseline_and_labels_force_selected_fresh_coverage(self):
        policy = json.loads((ROOT / 'ci/change-classifier-policy.json').read_text())
        normal = classifier.result_for(['ci/release/runner-notices.py'], [], False, False, policy)
        reused = classifier.result_for(['ci/release/runner-notices.py'], [], False, False, policy, True)
        self.assertTrue(normal['selected']['platforms'])
        self.assertTrue(reused['selected']['baseline'])
        self.assertFalse(any(reused['selected'][key] for key in classifier.BROAD_SELECTION))
        for label in classifier.KNOWN_LABELS:
            expected = classifier.result_for(['ci/test/fix.py'], [label], False, False, policy)
            actual = classifier.result_for(['ci/test/fix.py'], [label], False, False, policy, True)
            self.assertEqual(actual, expected)
        incomplete = classifier.result_for(None, [], True, True, policy, True)
        self.assertTrue(incomplete['broad'])

    def test_explicit_label_bypasses_reuse_and_workflow_keeps_required_baseline(self):
        self.event['pull_request']['labels'] = [{'name': 'ci:full'}]
        with tempfile.TemporaryDirectory() as temporary:
            event_path, output = Path(temporary) / 'event.json', Path(temporary) / 'output'
            event_path.write_text(json.dumps(self.event))
            with patch.dict(os.environ, {'GITHUB_EVENT_PATH': str(event_path), 'GITHUB_OUTPUT': str(output)}), \
                    patch.object(promotion, 'verify', side_effect=AssertionError('no reuse')):
                promotion.main()
            self.assertEqual(output.read_text(), 'reuse=false\n')
        workflow = (ROOT / '.github/workflows/ci.yml').read_text()
        self.assertIn('run: python3 ci/verify-promotion-ci.py', workflow)
        self.assertIn('check_selected true "$CLASSIFY" classify', workflow)
        self.assertIn('check_selected true "$LINT" lint', workflow)


if __name__ == '__main__':
    unittest.main()
