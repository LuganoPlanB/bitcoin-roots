#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Check actual workflow dependencies and selection without a hosted runner."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def jobs(name):
    text = (ROOT / '.github/workflows' / name).read_text().split('\njobs:\n', 1)[1]
    return dict((match.group(1), match.group(2)) for match in re.finditer(r'^  ([\w-]+):\n(.*?)(?=^  [\w-]+:|\Z)', text, re.M | re.S))


def field(block, key):
    match = re.search(r'^    ' + re.escape(key) + r': (.*)$', block, re.M)
    return match.group(1) if match else ''


def selected(expression, values):
    # Evaluate only the narrow Boolean grammar used by these checked-in jobs.
    expression = expression.removeprefix('${{').removesuffix('}}').strip()
    expression = expression.replace('!cancelled()', 'True')
    expression = re.sub(r'(?:needs\.[\w-]+\.(?:outputs\.[\w-]+|result)|inputs\.[\w-]+)', lambda match: repr(values.get(match[0], '')), expression)
    expression = expression.replace('&&', ' and ').replace('||', ' or ')
    return eval(expression, {'__builtins__': {}}, {})


class SharedAssetsWorkflowTest(unittest.TestCase):
    def test_pr_docs_fast_selected_and_unknown_paths(self):
        pr = jobs('ci.yml')
        for outputs, expected_unit, expected_fuzz in [({}, False, False), ({'sanitizers': 'true'}, True, False), ({'gui': 'true'}, True, False), ({'nightly_fuzz': 'true'}, False, True), ({'nightly_full': 'true'}, True, True), ({'sanitizers': 'true', 'compat': 'true', 'platforms': 'true'}, True, False)]:
            values = {'needs.classify.outputs.' + key: value for key, value in outputs.items()}
            with self.subTest(outputs=outputs):
                self.assertEqual(selected(field(pr['test-assets'], 'if'), values), expected_unit or expected_fuzz)
                for kind, expected in [('unit', expected_unit), ('fuzz', expected_fuzz)]:
                    expression = re.search(r'^      ' + kind + r': (.*)$', pr['test-assets'], re.M)[1]
                    self.assertEqual(selected(expression, values), expected)
        self.assertNotIn('test-assets', field(pr['fuzz-smoke'], 'needs'))
        self.assertNotIn('restore-test-assets', pr['fuzz-smoke'])
        for job in ['gui', 'sanitizers', 'nightly-sanitizers', 'nightly-fuzz', 'nightly-platforms', 'nightly-full']:
            self.assertIn('test-assets', field(pr[job], 'needs'))
            self.assertIn('needs.test-assets.outputs.revision', pr[job])
        self.assertEqual(field(pr['required-result'], 'if'), 'always()')

    def test_nightly_reuses_caller_and_standalone_produces_only_selected_kinds(self):
        nightly = jobs('nightly.yml')
        block = nightly['test-assets']
        for suite in ['', 'all', 'sanitizers', 'tidy', 'extended-functional', 'fuzz', 'macos-fuzz', 'windows-fuzz', 'platforms', 'kernel']:
            for revision in ['', 'a' * 40]:
                values = {'needs.gate.outputs.run': 'true', 'inputs.suite': suite, 'inputs.qa-assets-revision': revision}
                with self.subTest(suite=suite, supplied=bool(revision)):
                    self.assertEqual(selected(field(block, 'if'), values), not revision and suite not in ['tidy', 'extended-functional'])
                    if suite in ['tidy', 'extended-functional']:
                        continue
                    for kind, expected in [('fuzz', suite in ['', 'all', 'fuzz', 'macos-fuzz', 'windows-fuzz']), ('unit', suite not in ['fuzz', 'macos-fuzz', 'windows-fuzz'])]:
                        expression = re.search(r'^      ' + kind + r': (.*)$', block, re.M)[1]
                        self.assertEqual(selected(expression, values), expected)
        self.assertFalse(selected(field(block, 'if'), {'needs.gate.outputs.run': 'false'}))

    def test_skipped_nested_producer_does_not_skip_real_consumers(self):
        nightly = jobs('nightly.yml')
        for job, suite in [('linux-fuzz', 'fuzz'), ('macos-fuzz', 'fuzz'), ('windows-fuzz', 'windows-fuzz'), ('arm32', 'arm32'), ('kernel', 'kernel'), ('windows-smoke', 'windows-smoke'), ('macos-smoke', 'macos-smoke')]:
            with self.subTest(job=job):
                block = nightly[job]
                self.assertIn('test-assets', field(block, 'needs'))
                self.assertIn('!cancelled()', field(block, 'if'))
                values = {'needs.gate.outputs.run': 'true', 'needs.test-assets.result': 'skipped', 'inputs.qa-assets-revision': 'a' * 40, 'inputs.suite': suite}
                self.assertTrue(selected(field(block, 'if'), values))
                values['inputs.qa-assets-revision'] = ''
                self.assertFalse(selected(field(block, 'if'), values))
                values['needs.test-assets.result'] = 'success'
                self.assertTrue(selected(field(block, 'if'), values))
                values['needs.gate.outputs.run'] = 'false'
                self.assertFalse(selected(field(block, 'if'), values))
                self.assertIn('restore-test-assets', block)
        self.assertIn('macos-smoke', field(nightly['macos-fuzz'], 'needs'))

    def test_all_acquisition_routes_and_release_policy(self):
        nightly = jobs('nightly.yml')
        self.assertNotIn('git clone', nightly['windows-fuzz'])
        self.assertIn('download_utils.py fuzz', nightly['windows-fuzz'])
        self.assertIn('qa-assets-working', nightly['windows-fuzz'])
        for job in ['windows-smoke', 'macos-smoke']:
            self.assertIn('DIR_UNIT_TEST_DATA', nightly[job])
        shell = (ROOT / 'ci/test/03_test_script.sh').read_text()
        self.assertIn('"${ROOTS_CI_CADENCE:-}" != fast', shell)
        self.assertNotIn('git clone', shell)
        self.assertIn('download_utils.py" fuzz', shell)
        self.assertIn('download_utils.py" unit', shell)
        reusable = jobs('reusable-sanitizer.yml')['sanitizer']
        self.assertIn('restore-test-assets', reusable)
        release = (ROOT / '.github/workflows/release.yml').read_text()
        self.assertNotIn('restore-test-assets', release)
        self.assertEqual(release.count("RUN_UNIT_TESTS: 'false'"), 2)
        self.assertEqual(release.count("RUN_FUZZ_TESTS: 'false'"), 2)

    def test_matrix_consumers_allow_skipped_producer_only_with_inputs_or_tidy(self):
        nightly = jobs('nightly.yml')
        for job in ['linux-assurance', 'sanitizers']:
            expression = field(nightly[job], 'if')
            # Matrix non-emptiness is exercised by the actual selector fixtures.
            expression = re.sub(r'fromJSON\(needs.gate.outputs.[\w-]+\).include\[0\] != null', 'True', expression)
            for suite, revision, expected in [('all', '', False), ('all', 'a' * 40, True), ('tidy', '', job == 'linux-assurance'), ('extended-functional', '', job == 'linux-assurance')]:
                with self.subTest(job=job, suite=suite, supplied=bool(revision)):
                    self.assertEqual(selected(expression, {'needs.gate.outputs.run': 'true', 'needs.test-assets.result': 'skipped', 'inputs.suite': suite, 'inputs.qa-assets-revision': revision}), expected)

    def test_transport_remains_same_run_and_one_archive_per_kind(self):
        producer = (ROOT / '.github/workflows/reusable-test-assets.yml').read_text()
        self.assertEqual(producer.count('repository: bitcoin-core/qa-assets'), 1)
        self.assertNotIn('compression-level', producer)
        self.assertIn('unit_test_data.tar', producer)
        self.assertIn('fuzz_corpora.tar', producer)
        action = (ROOT / '.github/actions/restore-test-assets/action.yml').read_text()
        self.assertNotIn('run-id:', action)
        self.assertNotIn('github-token:', action)
        self.assertLess(action.index('download_utils.py verify'), action.index('echo "QA_ASSETS_DIR='))


if __name__ == '__main__':
    unittest.main()
