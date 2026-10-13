#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from pathlib import Path
import json
import os
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLANG = shutil.which('clang++')


@unittest.skipUnless(CLANG and shutil.which('cmake') and shutil.which('pkg-config'),
                     'CMake, Clang and pkg-config are required')
class HomebrewLibeventHeadersTest(unittest.TestCase):
    def build(self, discovery, *, apple=True, mismatch=False, implicit=True,
              project_warning=False, brew_success=True, alias=True, extra_mismatch=False):
        with tempfile.TemporaryDirectory(prefix='roots-homebrew-libevent-') as directory:
            work = Path(directory)
            cellar = work / 'Cellar/libevent/2.1.13'
            prefix = work / 'homebrew prefix'
            headers = cellar / 'include/event2'
            headers.mkdir(parents=True)
            # Reproduce the native libevent documentation diagnostic, without
            # changing or copying third-party source into the repository.
            (headers / 'event.h').write_text(
                '/** @param missing Not a parameter. */\n'
                'void event_probe(int actual);\n')
            (headers / 'http.h').write_text(
                '#include <cstdint>\nstruct evhttp_connection {};\n'
                'inline void evhttp_connection_get_peer(evhttp_connection*, '
                'const char**, uint16_t*) {}\n')
            (prefix / 'include').mkdir(parents=True)
            if alias:
                if mismatch:
                    shutil.copytree(headers, prefix / 'include/event2')
                else:
                    (prefix / 'include/event2').symlink_to(headers, target_is_directory=True)
            other = work / 'other-libevent'
            if extra_mismatch:
                shutil.copytree(headers, other / 'include/event2')
            tools = work / 'tools'
            tools.mkdir()
            brew = tools / 'brew'
            brew.write_text('#!/bin/sh\n' + (
                'case "$*" in\n'
                f'  "--prefix") printf "%s\\n" {shlex.quote(str(prefix))} ;;\n'
                '  *) exit 1 ;;\nesac\n' if brew_success else 'exit 1\n'))
            brew.chmod(0o755)
            config = work / 'config'
            config.mkdir()
            pc = work / 'pkgconfig'
            pc.mkdir()
            configs = ['set(Libevent_VERSION "2.1.13")\n']
            for component in ('core', 'extra', 'pthreads'):
                selected = other if extra_mismatch and component == 'extra' else cellar
                (selected / 'lib').mkdir(exist_ok=True)
                configs.append(
                    f'if(NOT TARGET libevent::{component})\n'
                    f'  add_library(libevent::{component} INTERFACE IMPORTED)\n'
                    f'  set_target_properties(libevent::{component} PROPERTIES\n'
                    f'    INTERFACE_INCLUDE_DIRECTORIES "{selected / "include"}")\n'
                    'endif()\n')
                (pc / f'libevent_{component}.pc').write_text(
                    f'prefix={selected}\nlibdir=${{prefix}}/lib\nincludedir=${{prefix}}/include\n'
                    f'Name: libevent_{component}\nDescription: Test fixture\nVersion: 2.1.13\n'
                    'Libs: -L${libdir}\nCflags: -I${includedir}\n')
            (config / 'LibeventConfig.cmake').write_text(''.join(configs))
            (config / 'LibeventConfigVersion.cmake').write_text(
                'set(PACKAGE_VERSION "2.1.13")\nset(PACKAGE_VERSION_COMPATIBLE TRUE)\n')
            source = work / 'source'
            source.mkdir()
            (source / 'probe.cpp').write_text(
                '#include <event2/event.h>\n' + (
                    '/** @param wrong Not a parameter. */\nvoid project_probe(int actual);\n'
                    if project_warning else ''))
            implicit_include = (
                f'list(APPEND CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "{prefix / "include"}")\n'
                if implicit else '')
            (source / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.22)\n'
                'project(HomebrewLibeventHeaders LANGUAGES CXX)\n'
                f'set(CMAKE_HOST_APPLE {"TRUE" if apple else "FALSE"})\n'
                f'{implicit_include}'
                f'list(PREPEND CMAKE_MODULE_PATH "{ROOT / "cmake/module"}")\n'
                'find_package(Libevent 2.1.8 MODULE REQUIRED)\n'
                'foreach(component core extra pthreads)\n'
                '  add_library(probe_${component} OBJECT probe.cpp)\n'
                '  target_link_libraries(probe_${component} PRIVATE libevent::${component})\n'
                '  target_compile_options(probe_${component} PRIVATE -Wdocumentation -Werror)\n'
                'endforeach()\n')
            environment = {**os.environ, 'PATH': f'{tools}:{os.environ["PATH"]}',
                           'PKG_CONFIG_LIBDIR': str(pc), 'PKG_CONFIG_PATH': ''}
            environment.pop('CPATH', None)
            build = work / 'build'
            configure = subprocess.run(
                ['cmake', '-S', str(source), '-B', str(build),
                 f'-DCMAKE_CXX_COMPILER={CLANG}', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                 f'-DLibevent_DIR={config if discovery == "config" else work / "absent"}'],
                capture_output=True, text=True, env=environment)
            self.assertEqual(configure.returncode, 0, configure.stdout + configure.stderr)
            if discovery == 'pkgconfig':
                self.assertIn('Found PkgConfig:', configure.stdout)
            else:
                self.assertIn(f'Found Libevent: {config}', configure.stdout)
            commands = json.loads((build / 'compile_commands.json').read_text())
            self.assertEqual(len(commands), 3)
            result = subprocess.run(
                ['cmake', '--build', str(build), '--parallel', '1'],
                capture_output=True, text=True,
                env={**environment, 'CPATH': str(prefix / 'include')})
            for entry in commands:
                argv = shlex.split(entry['command'])
                system_dirs = [argv[i + 1] for i, arg in enumerate(argv[:-1]) if arg == '-isystem']
                expected = apple and not mismatch and brew_success and alias
                if extra_mismatch and 'probe_extra.dir' in entry['command']:
                    expected = False
                self.assertEqual(str(prefix / 'include') in system_dirs, expected,
                                 entry['command'] + result.stderr)
                self.assertIn('-Werror', argv)
                self.assertIn('-Wdocumentation', argv)
            return result

    def both(self, **kwargs):
        results = []
        for discovery in ('config', 'pkgconfig'):
            with self.subTest(discovery=discovery):
                results.append(self.build(discovery, **kwargs))
        return results

    def test_implicit_matching_alias_emits_flag_and_compiles(self):
        for result in self.both():
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_explicit_matching_alias_compiles(self):
        for result in self.both(implicit=False):
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_project_documentation_warnings_still_fail(self):
        for result in self.both(project_warning=True):
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('source/probe.cpp:', result.stderr)
            self.assertIn('[-Werror,-Wdocumentation]', result.stderr)

    def test_mismatched_installation_is_not_hidden(self):
        for result in self.both(mismatch=True):
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('homebrew prefix/include/event2/event.h:', result.stderr)

    def test_mismatched_component_is_not_hidden(self):
        for result in self.both(extra_mismatch=True):
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('homebrew prefix/include/event2/event.h:', result.stderr)

    def test_non_apple_host_is_unchanged(self):
        for result in self.both(apple=False):
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('[-Werror,-Wdocumentation]', result.stderr)

    def test_failed_brew_query_is_unchanged(self):
        for result in self.both(brew_success=False):
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('[-Werror,-Wdocumentation]', result.stderr)

    def test_missing_alias_uses_selected_system_headers(self):
        for result in self.both(alias=False):
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
