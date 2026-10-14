#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Execute the actual nightly matrix selector and PR result gate locally."""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def run_block(file, name):
    text = file.read_text()
    start = text.index('- name: ' + name)
    rest = text[start:]
    match = re.search(r'        run: \|\n((?:          [^\n]*\n|\n)+)', rest)
    if not match:
        raise AssertionError(name)
    return '\n'.join(line[10:] if line.startswith('          ') else line for line in match[1].splitlines())


class WorkflowSelectionTest(unittest.TestCase):
    def nightly(self, suite):
        block = run_block(ROOT / '.github/workflows/nightly.yml', 'Select requested Linux assurance suites')
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'output'
            result = subprocess.run(['bash', '-c', block], env=os.environ | {'SUITE': suite, 'GITHUB_OUTPUT': str(output)}, capture_output=True, text=True, timeout=5)
            matrices = dict(line.split('=', 1) for line in output.read_text().splitlines()) if output.exists() else {}
            return result, {key: json.loads(value) for key, value in matrices.items()}

    def test_all_preserves_extended_platform_and_sanitizer_coverage(self):
        result, matrices = self.nightly('all')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(matrices['value']['include']), 4)
        self.assertEqual(len(matrices['sanitizers']['include']), 3)
        for matrix in matrices.values():
            for row in matrix['include']:
                self.assertTrue((ROOT / row['file-env']).is_file())
                self.assertGreater(row['timeout'], 60)

    def test_specific_suites_do_not_create_unrelated_matrix_jobs(self):
        for suite in ['tsan', 'msan', 'sanitizers', 'tidy', 'i686', 'platforms', 'previous-releases', 'extended-functional', 'fuzz', 'macos-fuzz', 'windows-fuzz', 'arm32', 'kernel', 'macos-smoke', 'windows-smoke']:
            with self.subTest(suite=suite):
                result, matrices = self.nightly(suite)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertLessEqual(sum(len(m['include']) for m in matrices.values()), 3)
        result, _ = self.nightly('unrecognized-suite')
        self.assertNotEqual(result.returncode, 0)

    def test_selected_failures_cannot_pass_the_pr_result_gate(self):
        block = run_block(ROOT / '.github/workflows/ci.yml', 'Enforce selected required results')
        env = {'CLASSIFY': 'success', 'LINT': 'success'}
        for key in ['GUI', 'SANITIZERS', 'FUZZ', 'NIGHTLY_SANITIZERS', 'NIGHTLY_FUZZ', 'NIGHTLY_PLATFORMS', 'NIGHTLY_FULL']:
            env[key] = 'skipped'
            env[key + '_SELECTED'] = 'false'
        base = subprocess.run(['bash', '-c', block], env=os.environ | env, capture_output=True, timeout=5)
        self.assertEqual(base.returncode, 0, base.stderr)
        for name in ['GUI', 'SANITIZERS', 'FUZZ', 'NIGHTLY_FUZZ']:
            for status in ['failure', 'cancelled', 'skipped']:
                with self.subTest(job=name, status=status):
                    failed = subprocess.run(['bash', '-c', block], env=os.environ | env | {name + '_SELECTED': 'true', name: status}, capture_output=True, timeout=5)
                    self.assertNotEqual(failed.returncode, 0)


if __name__ == '__main__':
    unittest.main()
