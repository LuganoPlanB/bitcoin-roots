#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Explicit release metadata and v32 platform packaging contracts."""
import json
from contextlib import contextmanager
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ReleaseVersionTest(unittest.TestCase):
    @contextmanager
    def platform_configuration(self, year=None, tag=None):
        config = (ROOT / 'CMakeLists.txt').read_text()
        metadata = config[config.index('set(CLIENT_NAME '):].split('#=============================', 1)[0]
        if year is not None:
            metadata = re.sub(r'set\(COPYRIGHT_YEAR [^)]+\)', f'set(COPYRIGHT_YEAR "{year}")', metadata)
        with tempfile.TemporaryDirectory(prefix='roots-platform-metadata-') as directory:
            source = Path(directory)
            lines = ['cmake_minimum_required(VERSION 3.22)', metadata]
            for original, generated in [('cmake/bitcoin-build-config.h.in', 'bitcoin-build-config.h'),
                                        ('share/qt/Info.plist.in', 'Info.plist'), ('share/setup.nsi.in', 'setup.nsi')]:
                lines.append(f'configure_file("{ROOT / original}" "${{CMAKE_BINARY_DIR}}/{generated}" @ONLY)')
            (source / 'CMakeLists.txt').write_text('\n'.join(lines) + '\n')
            command = ['cmake', '-S', str(source), '-B', str(source / 'build')]
            if tag is not None:
                command.append(f'-DCLIENT_VERSION_TAG={tag}')
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            yield source / 'build'

    @unittest.skipUnless(shutil.which('c++'), 'C++ preprocessor required for configured resource metadata')
    def test_configured_platform_notices_keep_foundation_and_core_ranges_separate(self):
        current_year = int(re.search(r'set\(COPYRIGHT_YEAR "?(\d+)', (ROOT / 'CMakeLists.txt').read_text()).group(1))
        for year, tag in [(current_year, None), (current_year + 1, 'v32.1-roots.6')]:
            with self.subTest(year=year, tag=tag), self.platform_configuration(year, tag) as build:
                expected = f'2026 Plan-B Foundation; 2009-{year} The Bitcoin Core developers'
                result = subprocess.run(['c++', '-E', '-P', '-x', 'c++', '-DRC_INVOKED',
                    '-I' + str(build), '-I' + str(ROOT / 'src'), '-'],
                    input='#include <clientversion.h>\nCOPYRIGHT_STR\n', capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                expanded = ''.join(json.loads(value) for value in re.findall(r'"(?:[^"\\]|\\.)*"', result.stdout))
                self.assertEqual(expanded, expected)
                plist = plistlib.loads((build / 'Info.plist').read_bytes())
                self.assertEqual(plist['NSHumanReadableCopyright'],
                    f'32.1.0, Copyright © 2026 Plan-B Foundation; © 2009-{year} The Bitcoin Core developers')
                installer = (build / 'setup.nsi').read_text()
                self.assertIn(f'VIAddVersionKey LegalCopyright "Copyright (C) {expected}"', installer)

    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required for configured numeric identity')
    def test_configured_platform_metadata_preserves_native_numeric_and_product_identity(self):
        with self.platform_configuration() as build:
            plist = plistlib.loads((build / 'Info.plist').read_bytes())
            for key in ['CFBundleVersion', 'CFBundleShortVersionString']:
                self.assertEqual(plist[key], '32.1.0')
            self.assertEqual(plist['CFBundleIdentifier'], 'org.bitcoinfoundation.Bitcoin-Qt')
            self.assertEqual(plist['CFBundleURLTypes'][0]['CFBundleURLSchemes'], ['bitcoin'])
            installer = (build / 'setup.nsi').read_text()
            self.assertIn('VIProductVersion 32.1.0.0', installer)
            self.assertIn('VIAddVersionKey ProductVersion "32.1-roots.1"', installer)
            self.assertIn('Name "Bitcoin Roots"', installer)
            result = subprocess.run(['c++', '-std=c++20', '-fsyntax-only', '-x', 'c++',
                '-I' + str(build), '-I' + str(ROOT / 'src'), '-'],
                input='#include <clientversion.h>\n#include <string_view>\n'
                      'static_assert(CLIENT_VERSION == 320100);\n'
                      'static_assert(std::string_view(CLIENT_NAME) == "Bitcoin Roots");\n'
                      'static_assert(std::string_view(CLIENT_VERSION_STRING) == "32.1-roots.1");\n',
                capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)

    def configure(self, tag=None):
        config = (ROOT / 'CMakeLists.txt').read_text()
        block = config[config.index('set(CLIENT_VERSION_STRING '):].split('#=============================', 1)[0]
        metadata = '\n'.join(re.search(rf'set\({name} [^)]+\)', config).group(0) for name in
                             ['CLIENT_VERSION_MAJOR', 'CLIENT_VERSION_MINOR', 'CLIENT_VERSION_BUILD', 'CLIENT_VERSION_RC'])
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.22)\nproject(ReleaseMetadata VERSION 32.1.0 LANGUAGES NONE)\n' + metadata + '\n' + block + '\nfile(WRITE "${CMAKE_BINARY_DIR}/version" "${CLIENT_VERSION_STRING}")\n')
            command = ['cmake', '-S', str(source), '-B', str(source / 'build')]
            if tag is not None:
                command.append(f'-DCLIENT_VERSION_TAG={tag}')
            result = subprocess.run(command, capture_output=True, text=True)
            version = source / 'build/version'
            return result, version.read_text() if version.exists() else None

    def test_explicit_v32_roots_tag_is_compiled_without_git_metadata(self):
        for tag in ['v32.1-roots.1', 'v32.1-roots.6']:
            result, version = self.configure(tag)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(version, tag[1:])

    def test_wrong_core_base_and_invalid_revision_are_rejected(self):
        for tag in ['v31.1-roots.6', 'v32x1-roots.1', 'v32.1-roots.0', 'v32.1-knots.1', '']:
            result, _ = self.configure(tag)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('CLIENT_VERSION_TAG must be a Roots release tag', result.stderr)

    def test_default_product_version_preserves_numeric_and_wire_identity(self):
        result, version = self.configure()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(version, '32.1-roots.1')
        config = (ROOT / 'CMakeLists.txt').read_text()
        for value in ['set(CLIENT_NAME "Bitcoin Roots")', 'set(CLIENT_VERSION_MAJOR 32)',
                      'set(CLIENT_VERSION_MINOR 1)', 'set(CLIENT_VERSION_BUILD 0)',
                      'set(COPYRIGHT_HOLDERS "Plan-B Foundation")']:
            self.assertIn(value, config)
        self.assertIn('UA_NAME("Satoshi")', (ROOT / 'src/clientversion.cpp').read_text())

    def test_five_platforms_readonly_rehearsal_and_tag_only_signed_new_draft(self):
        workflow = (ROOT / '.github/workflows/release.yml').read_text()
        for platform in ['linux-x86_64', 'linux-aarch64', 'darwin-x86_64', 'darwin-arm64', 'windows-x86_64']:
            self.assertIn('bitcoin-roots-' + platform, workflow)
        before, publish = workflow.split('  publish-release:', 1)
        self.assertNotIn('secrets.', before)
        self.assertIn('permissions:\n  contents: read', before)
        self.assertIn("if: github.event_name == 'push'", publish)
        self.assertIn('environment: release', publish)
        self.assertIn("REQUIRE_RELEASE_SIGNATURE: '1'", publish)
        self.assertIn('ci/release/create-ci-draft.py', publish)
        self.assertNotIn('--clobber', workflow)
        self.assertIn('python3 -m unittest discover -s ci/test', workflow)

    def test_current_platform_paths_and_dependencies(self):
        workflow = (ROOT / '.github/workflows/release.yml').read_text()
        self.assertIn('Xcode_16.2.app', workflow)
        self.assertNotIn('libevent', workflow)
        self.assertIn('packages=("${BASE_BUILD_DIR}"/bitcoin-macos-app.zip)', workflow)
        self.assertIn('sudo python3 ci/release/archive.py stage-copying "$BASE_OUTDIR"', workflow)
        self.assertIn('-DVCPKG_MANIFEST_FEATURES=miniupnpc', workflow)
        self.assertIn('miniupnpc', json.loads((ROOT / 'vcpkg.json').read_text())['features'])
        for path in re.findall(r'(?:ci|contrib)/[A-Za-z0-9_./-]+\.(?:asc|py|sh)', workflow):
            self.assertTrue((ROOT / path).is_file(), path)


if __name__ == '__main__':
    unittest.main()
