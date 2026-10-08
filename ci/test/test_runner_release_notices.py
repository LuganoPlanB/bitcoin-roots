#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import importlib.util
import io
import json
import os
from types import SimpleNamespace
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'ci/release'))
spec = importlib.util.spec_from_file_location('runner', ROOT / 'ci/release/runner-notices.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class RunnerNoticeTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)

    def archive(self, name='source/LICENSE'):
        archive = self.work / 'source.tar.gz'
        with tarfile.open(archive, 'w:gz') as out:
            entry = tarfile.TarInfo(name)
            data = b'Exact upstream copyright and license\n'
            entry.size = len(data)
            out.addfile(entry, io.BytesIO(data))
        return archive, runner.sha256(archive)

    def test_pinned_depends_receipt_uses_real_archive_and_host_selection(self):
        source, checksum = self.archive()
        depends = self.work / 'depends'
        (depends / 'packages').mkdir(parents=True)
        (depends / 'packages/boost.mk').write_text('pinned recipe\n')
        installed = self.work / 'installed'
        installed.mkdir()
        (installed / 'toolchain.cmake').write_text('installed toolchain\n')
        values = {'packages': 'boost', 'host_prefix': str(installed), 'SOURCES_PATH': str(self.work),
                  'boost_version': '1.88.0', 'boost_source': str(source), 'boost_sha256_hash': checksum,
                  'boost_download_path': 'https://example.invalid', 'boost_download_file': 'source.tar.gz',
                  'boost_extra_sources': ''}
        def execute(*args):
            if args[0] == 'git':
                return 'a' * 40
            return '\n'.join(x.removeprefix('print-') + '=' + values[x.removeprefix('print-')]
                             for x in args if x.startswith('print-'))
        with patch.object(runner, 'run', side_effect=execute):
            result = runner.depends_notices(depends, 'aarch64-linux-gnu', self.work / 'notices')
            self.assertEqual(result['boost']['source_sha256'], checksum)
            receipt = json.loads(Path(result['boost']['evidence']).read_text())
            self.assertEqual(receipt['host'], 'aarch64-linux-gnu')
            self.assertEqual(receipt['source_commit'], 'a' * 40)
            source.unlink()
            with self.assertRaises(FileNotFoundError):
                runner.depends_notices(depends, 'aarch64-linux-gnu', self.work / 'missing')

    def test_source_hash_and_unsafe_member_fail_closed(self):
        source, checksum = self.archive()
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            runner.source_notices(source, '0' * 64, self.work / 'bad')
        source, checksum = self.archive('../LICENSE')
        with self.assertRaisesRegex(ValueError, 'Unsafe'):
            runner.source_notices(source, checksum, self.work / 'bad')

    def test_brew_retains_actual_receipt_matching_version_and_source(self):
        source, checksum = self.archive()
        prefix = self.work / 'installed'
        prefix.mkdir()
        (prefix / 'INSTALL_RECEIPT.json').write_text('{"built_as_bottle":true}')
        formula = {'name': 'boost', 'installed': [{'version': '1.88.0'}],
                   'versions': {'stable': '1.88.0'}, 'urls': {'stable': {'url': 'https://example.invalid/source', 'checksum': checksum}}}
        def execute(*args):
            if args[:2] == ('brew', 'deps'):
                # Homebrew recurses by default; multi-formula intersection
                # drops dependencies that only one requested formula needs.
                self.assertEqual(args, ('brew', 'deps', '--installed', '--union',
                                       'qt@6', 'boost', 'libevent', 'zeromq',
                                       'qrencode', 'miniupnpc', 'capnp'))
                required = runner.notices.required_components('darwin-arm64') - {'qt', 'sqlite', 'openssl', 'icu'}
                return '\n'.join(sorted(required | {'ca-certificates', 'qt', 'qtbase',
                                                     'qtwebengine', 'openssl@3', 'icu4c@78'}))
            if args[:2] == ('brew', 'info'):
                if args[-1] == 'boost':
                    return json.dumps({'formulae': [formula]})
                if args[-1] not in {'ca-certificates', 'qt', 'qt@6', 'qtwebengine'}:
                    return json.dumps({'formulae': [{**formula, 'name': args[-1]}]})
                return json.dumps({'formulae': [{'name': args[-1], 'urls': {'stable': {'url': 'not-an-archive'}}}]})
            if args[:2] == ('brew', '--prefix'):
                return str(prefix)
            return 'fixture system identity'
        with patch.object(runner, 'run', side_effect=execute), patch.object(runner, 'acquire', return_value=source):
            result = runner.brew_notices(self.work / 'brew')
            receipt = json.loads(Path(result['boost']['evidence']).read_text())
            self.assertTrue(receipt['installed_receipt']['built_as_bottle'])
            self.assertEqual(result['sqlite']['role'], 'system-library')
            self.assertTrue(runner.notices.required_components('darwin-arm64') <= result.keys())
            self.assertIn('qt', result)
            self.assertNotIn('ca-certificates', result)
            self.assertNotIn('qtwebengine', result)
            formula['installed'][0]['version'] = '1.87.0'
            with self.assertRaisesRegex(ValueError, 'version mismatch'):
                runner.brew_notices(self.work / 'wrong-version')
            formula['installed'][0]['version'] = '1.88.0'
            (prefix / 'INSTALL_RECEIPT.json').unlink()
            with self.assertRaises(FileNotFoundError):
                runner.brew_notices(self.work / 'missing-receipt')

    def test_vcpkg_uses_installed_static_port_version_and_copyright(self):
        installed = self.work / 'vcpkg-installed'
        (installed / 'vcpkg').mkdir(parents=True)
        status = 'Package: qtbase\nVersion: 6.8.2\nArchitecture: x64-windows-static\nStatus: install ok installed\n'
        (installed / 'vcpkg/status').write_text(status)
        copyright = installed / 'x64-windows-static/share/qtbase/copyright'
        copyright.parent.mkdir(parents=True)
        copyright.write_text('Exact installed Qt copyright\n')
        with patch.object(runner, 'run', return_value='a' * 40):
            result = runner.vcpkg_notices(installed, self.work / 'output')
            self.assertEqual(result['qt']['version'], '6.8.2')
            self.assertEqual(Path(result['qt']['files'][0]).read_bytes(), copyright.read_bytes())
            (installed / 'vcpkg/status').write_text(status + '\n' + status)
            with self.assertRaisesRegex(ValueError, 'Duplicate'):
                runner.vcpkg_notices(installed, self.work / 'duplicate')
            (installed / 'vcpkg/status').write_text(status.replace('x64-windows-static', 'x64-windows'))
            self.assertEqual(runner.vcpkg_notices(installed, self.work / 'wrong-triplet'), {})
            (installed / 'vcpkg/status').write_text(status)
            copyright.unlink()
            with self.assertRaisesRegex(ValueError, 'copyright missing'):
                runner.vcpkg_notices(installed, self.work / 'missing')

    def test_vcpkg_aggregates_real_boost_ports_and_required_components(self):
        installed = self.work / 'vcpkg'
        (installed / 'vcpkg').mkdir(parents=True)
        ports = sorted(runner.notices.required_components('windows-x86_64') - {'boost', 'qt', 'sqlite', 'qrencode'})
        ports += ['boost-multi-index', 'boost-signals2', 'qtbase', 'sqlite3', 'libqrencode']
        paragraphs = []
        for name in ports:
            version = '1.87.0' if name.startswith('boost-') else '1.0'
            paragraphs.append(f'Package: {name}\nVersion: {version}\nArchitecture: x64-windows-static\nStatus: install ok installed\n')
            notice = installed / 'x64-windows-static/share' / name / 'copyright'
            notice.parent.mkdir(parents=True)
            notice.write_text(f'Actual installed {name} copyright\n')
        (installed / 'vcpkg/status').write_text('\n'.join(paragraphs))
        with patch.object(runner, 'run', return_value='a' * 40):
            result = runner.vcpkg_notices(installed, self.work / 'receipts')
        self.assertTrue(runner.notices.required_components('windows-x86_64') <= result.keys())
        self.assertEqual(len(result['boost']['files']), 2)
        descriptor = self.work / 'descriptor.json'
        descriptor.write_text(json.dumps(result))
        runner.notices.collect('windows-x86_64', descriptor, self.work / 'collected')

    def test_release_notice_platform_passes_container_allowlist_without_signing_secret(self):
        spec = importlib.util.spec_from_file_location('container', ROOT / 'ci/test/02_run_container.py')
        container = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(container)
        env = {'USER': self.work.name, 'CONTAINER_NAME': 'notice-fixture',
               'RELEASE_NOTICE_PLATFORM': 'linux-aarch64', 'BITCOIN_ROOTS_GPG_SK': 'fixture-never-export'}
        path = Path(f"/tmp/env-{env['USER']}-{env['CONTAINER_NAME']}")
        self.addCleanup(path.unlink, missing_ok=True)
        with patch.dict(os.environ, env, clear=True), patch.object(container, 'run', return_value=SimpleNamespace(stdout='export CONTAINER_NAME=fixture')):
            container.main()
        text = path.read_text()
        self.assertIn('RELEASE_NOTICE_PLATFORM=linux-aarch64', text)
        self.assertNotIn('BITCOIN_ROOTS_GPG_SK', text)
        self.assertNotIn('fixture-never-export', text)

    def test_all_runner_workflow_blocks_stage_and_validate_before_upload(self):
        workflow = (ROOT / '.github/workflows/release.yml').read_text()
        for job, following in [('linux-release', 'windows-release'), ('windows-release', 'macos-release'), ('macos-release', 'publish-release')]:
            block = workflow.split(f'  {job}:', 1)[1].split(f'  {following}:', 1)[0]
            if job == 'linux-release':
                self.assertIn('export RELEASE_NOTICE_PLATFORM=', block)
                hook = (ROOT / 'ci/test/03_test_script.sh').read_text()
                self.assertIn('if [[ -n "${RELEASE_NOTICE_PLATFORM:-}" ]]', hook)
                self.assertIn('--depends "$DEPENDS_DIR" --host "$HOST"', hook)
            else:
                self.assertLess(block.index('ci/release/runner-notices.py'), block.index('ci/release/notices.py stage'))
            self.assertLess(block.index('ci/release/notices.py stage'), block.index('ci/release/notices.py validate'))
            self.assertLess(block.index('ci/release/notices.py validate'), block.index('uses: actions/upload-artifact'))


if __name__ == '__main__':
    unittest.main()
