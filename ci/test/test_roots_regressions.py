#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Exercise actual affected selections, execution budgets and failure artifacts."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('roots_regressions', ROOT / 'ci/test/roots-regressions.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class RootsRegressionTest(unittest.TestCase):
    def test_affected_wallet_retains_authorization_regressions(self):
        plan = MODULE.selection(['src/wallet/spend.cpp'], 'fast')
        self.assertIn('wallet_sweepprivkeys.py', plan['functional'])
        self.assertIn('wallet_signer.py', plan['functional'])
        self.assertNotIn('rpc_net.py', plan['functional'])
        self.assertEqual(plan['budgets']['fuzz'], 5)
        self.assertLess(plan['budgets']['functional'], MODULE.selection(None, 'nightly')['budgets']['functional'])

    def test_unknown_incomplete_and_ambiguous_fail_open(self):
        baseline = MODULE.selection(None, 'fast')
        for files in [[], ['unclassified/file'], ['/src/policy/a.cpp'], ['src//policy/a.cpp'], ['src/../policy/a.cpp'], [None], 'src/policy/a.cpp']:
            with self.subTest(files=files):
                self.assertEqual(MODULE.selection(files, 'fast'), baseline)
        self.assertEqual(MODULE.selection(['doc/man/bitcoind.1'], 'fast'), baseline)
        self.assertEqual(MODULE.selection(['share/examples/bitcoin.conf'], 'fast'), baseline)

    def test_mempool_args_selects_policy_and_runtime_dependencies(self):
        plan = MODULE.selection(['src/node/mempool_args.cpp'], 'fast')
        self.assertEqual(plan['groups'], ['mining', 'policy', 'runtime'])
        self.assertIn('feature_policy_args.py', plan['functional'])
        self.assertIn('feature_config_args.py', plan['functional'])
        self.assertNotIn('wallet_signer.py', plan['functional'])

    def test_mixed_changes_take_union_and_all_tests_exist(self):
        wallet = MODULE.selection(['src/wallet/wallet.cpp'], 'fast')
        peers = MODULE.selection(['src/net_processing.cpp'], 'fast')
        mixed = MODULE.selection(['src/wallet/wallet.cpp', 'src/net_processing.cpp'], 'fast')
        self.assertEqual(set(mixed['functional']), set(wallet['functional']) | set(peers['functional']))
        for name in MODULE.selection(None, 'fast')['functional']:
            self.assertTrue((ROOT / 'test/functional' / name).is_file(), name)
        sources = '\n'.join(path.read_text() for path in (ROOT / 'src/test/fuzz').glob('*.cpp'))
        for target in MODULE.selection(None, 'fast')['fuzz']:
            self.assertIn('FUZZ_TARGET(' + target, sources)

    def test_execution_uses_time_budgets_and_keeps_failed_output(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build / 'bin').mkdir()
            fake = build / 'bin/fuzz'
            fake.write_text('#!' + sys.executable + '\nimport os,sys\nprint(os.environ["FUZZ"], *sys.argv[1:], flush=True)\nsys.exit(3)\n')
            fake.chmod(0o755)
            for cadence, expected in [('fast', 5), ('nightly', 60)]:
                artifacts = build / cadence
                plan = MODULE.selection(['src/wallet/wallet.cpp'], cadence)
                self.assertEqual(MODULE.execute(plan, 'fuzz', build, artifacts, []), 1)
                results = json.loads((artifacts / 'fuzz-results.json').read_text())['results']
                self.assertEqual(len(results), 2)
                for record in results:
                    self.assertEqual(record['exit'], 3)
                    self.assertEqual(record['timeout'], expected + 10)
                    self.assertIn('-max_total_time=' + str(expected), record['command'])
                    self.assertNotIn('-runs=', ' '.join(record['command']))
                    self.assertIn(record['target'], (artifacts / (record['target'] + '.log')).read_text())

    def test_real_timeout_is_failure_with_retained_log(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'timeout.log'
            result = MODULE.run_bounded([sys.executable, '-c', 'import time; print("owned slow fixture", flush=True); time.sleep(30)'], .2, log)
            self.assertEqual(result, 124)
            self.assertIn('owned slow fixture', log.read_text())
            self.assertEqual(MODULE.run_bounded([str(Path(directory) / 'absent')], 1, log), 127)
            self.assertTrue(log.stat().st_size)


if __name__ == '__main__':
    unittest.main()
