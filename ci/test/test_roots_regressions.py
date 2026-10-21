#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Exercise actual affected selections, execution budgets and failure artifacts."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('roots_regressions', ROOT / 'ci/test/roots-regressions.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class RootsRegressionTest(unittest.TestCase):
    def test_affected_wallet_retains_authorization_regressions(self):
        plan = MODULE.selection(['src/wallet/spend.cpp'], 'fast')
        self.assertIn('wallet_sweepprivkeys.py', plan['functional'])
        self.assertIn('wallet_signer.py', plan['functional'])
        self.assertIn('wallet_exportaddresskey.py', plan['functional'])
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

    def test_network_changes_cover_peer_permission_and_fetch_boundaries(self):
        expected = {
            'p2p_filter.py', 'p2p_invalid_messages.py', 'rpc_net.py',
            'p2p_permissions.py', 'p2p_addr_relay.py', 'p2p_getaddr_caching.py',
            'p2p_roots_controls.py', 'rpc_getblockfrompeer.py',
        }
        for path in ['src/net.cpp', 'src/net_permissions.cpp', 'src/net_permissions.h',
                     'src/net_processing.cpp', 'src/protocol.h',
                     *('test/functional/' + name for name in expected)]:
            with self.subTest(path=path):
                plan = MODULE.selection([path], 'fast')
                self.assertEqual(plan['groups'], ['net'])
                self.assertEqual(set(plan['functional']), expected)
                self.assertEqual(plan['budgets']['functional'], 900)

    def test_peer_and_blockchain_rpc_changes_add_network_coverage(self):
        network = MODULE.selection(['src/net.cpp'], 'fast')
        existing = MODULE.selection(['src/rpc/mempool.cpp'], 'fast')
        for path in ['src/rpc/net.cpp', 'src/rpc/blockchain.cpp']:
            with self.subTest(path=path):
                plan = MODULE.selection([path], 'fast')
                self.assertEqual(plan['groups'], ['net', 'policy', 'runtime'])
                self.assertEqual(set(plan['functional']),
                                 set(network['functional']) | set(existing['functional']))

    def test_fallback_retains_all_groups_and_documented_counts(self):
        plan = MODULE.selection(None, 'fast')
        self.assertEqual(plan['groups'], ['mining', 'net', 'policy', 'runtime', 'wallet'])
        self.assertEqual(len(plan['functional']), 22)
        self.assertEqual(len(plan['fuzz']), 9)
        self.assertEqual(plan['budgets']['functional'], 900)

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
                summary = io.StringIO()
                with contextlib.redirect_stderr(summary):
                    self.assertEqual(MODULE.execute(plan, 'fuzz', build, artifacts, []), 1)
                results = json.loads((artifacts / 'fuzz-results.json').read_text())['results']
                self.assertEqual(len(results), 2)
                for record in results:
                    self.assertEqual(record['exit'], 3)
                    self.assertEqual(record['timeout'], expected + 10)
                    self.assertIn('-max_total_time=' + str(expected), record['command'])
                    self.assertNotIn('-runs=', ' '.join(record['command']))
                    self.assertIn(record['target'], (artifacts / (record['target'] + '.log')).read_text())
                    self.assertEqual(record['log'], record['target'] + '.log')
                    self.assertIn(record['target'] + ' failed (exit 3)', summary.getvalue())

    def test_real_timeout_is_failure_with_retained_log(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'timeout.log'
            result = MODULE.run_bounded([sys.executable, '-c', 'import time; print("owned slow fixture", flush=True); time.sleep(30)'], .2, log)
            self.assertEqual(result, 124)
            self.assertIn('owned slow fixture', log.read_text())
            self.assertEqual(MODULE.run_bounded([str(Path(directory) / 'absent')], 1, log), 127)
            self.assertTrue(log.stat().st_size)

    def test_failure_summary_is_bounded_and_full_output_is_retained(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            runner = build / 'test/functional/test_runner.py'
            runner.parent.mkdir(parents=True)
            runner.write_text('import sys\nprint("x" * 20000, flush=True)\nprint("specific failure", file=sys.stderr)\nsys.exit(4)\n')
            artifacts = build / 'host-artifacts'
            summary = io.StringIO()
            with contextlib.redirect_stderr(summary):
                result = MODULE.execute(MODULE.selection(['src/wallet/spend.cpp'], 'fast'), 'functional', build, artifacts, [])
            self.assertEqual(result, 1)
            record = json.loads((artifacts / 'functional-results.json').read_text())['results'][0]
            self.assertEqual(record['log'], 'functional.log')
            self.assertEqual(record['exit'], 4)
            self.assertGreater((artifacts / record['log']).stat().st_size, 20000)
            self.assertIn('specific failure', summary.getvalue())
            self.assertIn('exit 4', summary.getvalue())
            self.assertLess(len(summary.getvalue()), 3500)

    def test_container_binds_artifacts_and_forwards_explicit_path(self):
        spec = importlib.util.spec_from_file_location('container', ROOT / 'ci/test/02_run_container.py')
        container = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(container)
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            artifacts = work / 'host-artifacts'
            env = {'USER': work.name, 'CONTAINER_NAME': 'diagnostic-fixture',
                   'PATH': os.environ.get('PATH', '/usr/bin'), 'ROOTS_ARTIFACTS_DIR': str(artifacts),
                   'BASE_READ_ONLY_DIR': str(ROOT), 'BASE_ROOT_DIR': str(work / 'guest'),
                   'CCACHE_DIR': str(work / 'ccache'), 'DEPENDS_DIR': str(work / 'depends'),
                   'PREVIOUS_RELEASES_DIR': str(work / 'previous'), 'CI_IMAGE_NAME_TAG': 'fixture',
                   'CI_IMAGE_PLATFORM': 'linux', 'FILE_ENV': 'fixture'}
            env_file = Path(f"/tmp/env-{env['USER']}-{env['CONTAINER_NAME']}")
            self.addCleanup(env_file.unlink, missing_ok=True)
            calls = []

            def run(command, **kwargs):
                calls.append(command)
                if command[:2] == ['bash', '-c']:
                    return SimpleNamespace(stdout='export ROOTS_ARTIFACTS_DIR=\n')
                if command[:2] == ['docker', 'run']:
                    self.assertTrue(artifacts.is_dir())
                    self.assertIn(f'--mount=type=bind,src={artifacts},dst={artifacts}', command)
                    self.assertIn(f'ROOTS_ARTIFACTS_DIR={artifacts}', env_file.read_text())
                    return SimpleNamespace(stdout='fixture-container\n')
                if command[:2] == ['docker', 'exec'] and command[-1].endswith('/03_test_script.sh'):
                    # Emulate writes through the declared bind mount. No actual
                    # Docker build, dependency install or fuzz target executes.
                    log = artifacts / 'functional.log'
                    result = MODULE.run_bounded([sys.executable, '-c', 'import sys; print("host-visible failure"); sys.exit(3)'], 5, log)
                    (artifacts / 'functional-results.json').write_text(json.dumps({'exit': result, 'log': log.name}))
                return SimpleNamespace(stdout='', returncode=0)

            with patch.dict(os.environ, env, clear=True), patch.object(container, 'run', side_effect=run):
                container.main()
            self.assertIn('host-visible failure', (artifacts / 'functional.log').read_text())
            self.assertEqual(json.loads((artifacts / 'functional-results.json').read_text())['exit'], 3)
            self.assertTrue(any(command[:2] == ['docker', 'run'] for command in calls))


if __name__ == '__main__':
    unittest.main()
