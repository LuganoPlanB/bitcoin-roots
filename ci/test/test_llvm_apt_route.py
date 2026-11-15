#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Exercise LLVM DNS recovery without network access or host DNS changes."""

import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ROUTE = load('llvm_apt_route', ROOT / 'ci/llvm-apt-route.py')
CONTAINER = load('ci_container', ROOT / 'ci/test/02_run_container.py')
PUBLIC = '146.75.122.49'


class LlvmAptRouteTest(unittest.TestCase):
    def test_normal_dns_uses_verified_original_hostname(self):
        with patch.object(ROUTE, 'addresses', return_value=[PUBLIC]) as resolve, \
                patch.object(ROUTE.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)) as curl:
            self.assertEqual(ROUTE.select_route(), (PUBLIC, ROUTE.HOST))
        resolve.assert_called_once_with(ROUTE.HOST)
        command = curl.call_args.args[0]
        self.assertIn(f'{ROUTE.HOST}:443:{PUBLIC}', command)
        self.assertEqual(command[-1], 'https://apt.llvm.org/llvm-snapshot.gpg.key')
        self.assertNotIn('--insecure', command)

    def test_servfail_uses_reviewed_cdn_with_original_tls_name(self):
        with patch.object(ROUTE, 'addresses', side_effect=[[], [PUBLIC]]) as resolve, \
                patch.object(ROUTE.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)):
            self.assertEqual(ROUTE.select_route(), (PUBLIC, ROUTE.FALLBACK))
        self.assertEqual([call.args[0] for call in resolve.call_args_list], [ROUTE.HOST, ROUTE.FALLBACK])

    def test_no_route_fails_instead_of_skipping_assurance(self):
        with patch.object(ROUTE, 'addresses', return_value=[]), \
                patch.object(ROUTE.subprocess, 'run') as curl:
            with self.assertRaises(RuntimeError):
                ROUTE.select_route()
        curl.assert_not_called()

    def test_tls_failure_is_not_bypassed(self):
        with patch.object(ROUTE, 'addresses', return_value=[PUBLIC]) as resolve, \
                patch.object(ROUTE.subprocess, 'run', return_value=subprocess.CompletedProcess([], 60)):
            with self.assertRaises(RuntimeError):
                ROUTE.select_route()
        resolve.assert_called_once_with(ROUTE.HOST)

    def test_timeout_is_bounded_and_propagated(self):
        with patch.object(ROUTE, 'addresses', return_value=[PUBLIC]), \
                patch.object(ROUTE.subprocess, 'run', side_effect=subprocess.TimeoutExpired('curl', 12)):
            with self.assertRaises(RuntimeError):
                ROUTE.select_route()

    def test_resolver_filters_and_deduplicates_addresses(self):
        output = f'{PUBLIC} STREAM\n{PUBLIC} DGRAM\n127.0.0.1 STREAM\nnot-an-ip STREAM\n'
        with patch.object(ROUTE.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, output)):
            self.assertEqual(ROUTE.addresses(ROUTE.HOST), [PUBLIC])

    def test_container_route_rejects_nonpublic_or_injected_values(self):
        self.assertEqual(CONTAINER.llvm_apt_host_args(''), [])
        self.assertEqual(CONTAINER.llvm_apt_host_args(PUBLIC), [f'--add-host=apt.llvm.org:{PUBLIC}'])
        for address in ['127.0.0.1', '10.1.2.3', '::1', PUBLIC + '\nOTHER=value', '--network=host']:
            with self.subTest(address=address), self.assertRaises(ValueError):
                CONTAINER.llvm_apt_host_args(address)


if __name__ == '__main__':
    unittest.main()
