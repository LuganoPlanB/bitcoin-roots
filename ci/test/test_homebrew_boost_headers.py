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


@unittest.skipUnless(CLANG and shutil.which('cmake'), 'CMake and Clang are required')
class HomebrewBoostHeadersTest(unittest.TestCase):
    def build(self, *, apple=True, matching=True, project_warning=False, implicit_alias=False):
        with tempfile.TemporaryDirectory(prefix='roots-homebrew-boost-') as directory:
            work = Path(directory)
            cellar = work / 'Cellar/boost/1.92.0'
            headers = cellar / 'include/boost'
            headers.mkdir(parents=True)
            header = ('struct Base { virtual void lock() {} };\n'
                      'template<class T> struct Derived: Base { void lock() {} };\n')
            (headers / 'signal.hpp').write_text(header)
            (headers / 'config.hpp').write_text('')
            prefix = work / 'homebrew prefix'
            (prefix / 'include').mkdir(parents=True)
            if matching:
                (prefix / 'include/boost').symlink_to(headers, target_is_directory=True)
            else:
                # A different installation must not gain system classification.
                (prefix / 'include/boost').mkdir()
                (prefix / 'include/boost/signal.hpp').write_text(header)
            config = cellar / 'lib/cmake/Boost-1.92.0'
            config.mkdir(parents=True)
            (config / 'BoostConfig.cmake').write_text(
                'add_library(Boost::headers INTERFACE IMPORTED)\n'
                'set_target_properties(Boost::headers PROPERTIES\n'
                f'  INTERFACE_INCLUDE_DIRECTORIES "{cellar / "include"}")\n')
            (config / 'BoostConfigVersion.cmake').write_text(
                'set(PACKAGE_VERSION "1.92.0")\n'
                'set(PACKAGE_VERSION_COMPATIBLE TRUE)\n')
            tools = work / 'tools'
            tools.mkdir()
            brew = tools / 'brew'
            brew.write_text('#!/bin/sh\ncase "$*" in\n'
                            f'  "--prefix boost") printf "%s\\n" {shlex.quote(str(cellar))} ;;\n'
                            f'  "--prefix") printf "%s\\n" {shlex.quote(str(prefix))} ;;\n'
                            '  *) exit 1 ;;\nesac\n')
            brew.chmod(0o755)
            source = work / 'source'
            source.mkdir()
            (source / 'main.cpp').write_text(
                '#include <boost/signal.hpp>\nDerived<int> item;\n'
                + ('struct Own: Base { void lock() {} };\nOwn own;\n' if project_warning else ''))
            implicit_include = (f'list(APPEND CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "{prefix / "include"}")\n'
                                if implicit_alias else '')
            (source / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.22)\n'
                'project(HomebrewBoostHeaders LANGUAGES CXX)\n'
                f'set(CMAKE_HOST_APPLE {"TRUE" if apple else "FALSE"})\n'
                f'{implicit_include}'
                f'include("{ROOT / "cmake/module/AddBoostIfNeeded.cmake"}")\n'
                'add_boost_if_needed()\n'
                'add_library(probe OBJECT main.cpp)\n'
                'target_link_libraries(probe PRIVATE Boost::headers)\n'
                'target_compile_options(probe PRIVATE -Wsuggest-override -Werror)\n')
            # Model an earlier linked-prefix search path without changing the
            # configured Boost dependency. This does not assert runner CPATH.
            environment = {**os.environ, 'PATH': f'{tools}:{os.environ["PATH"]}',
                           'CPATH': str(prefix / 'include')}
            build = work / 'build'
            configure_environment = environment.copy()
            if implicit_alias:
                # CMake exempts configure-time CPATH entries from implicit-dir
                # filtering. Model the filtered path without that exemption;
                # the build still selects the linked headers through CPATH.
                configure_environment.pop('CPATH')
            configure = subprocess.run(
                ['cmake', '-S', str(source), '-B', str(build),
                 f'-DCMAKE_CXX_COMPILER={CLANG}', f'-DBoost_DIR={config}',
                 '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'],
                capture_output=True, text=True, env=configure_environment)
            self.assertEqual(configure.returncode, 0, configure.stdout + configure.stderr)
            command = json.loads((build / 'compile_commands.json').read_text())[0]['command']
            arguments = shlex.split(command)
            system_dirs = [arguments[i + 1] for i, argument in enumerate(arguments[:-1])
                           if argument == '-isystem']
            self.assertEqual(str(prefix / 'include') in system_dirs, apple and matching, command)
            self.assertIn('-Werror', arguments)
            self.assertIn('-Wsuggest-override', arguments)
            return subprocess.run(['cmake', '--build', str(build), '--parallel', '2'],
                                  capture_output=True, text=True, env=environment)

    def test_matching_linked_headers_keep_system_classification(self):
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_project_override_warnings_still_fail(self):
        result = self.build(project_warning=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('main.cpp:', result.stderr)
        self.assertIn('[-Werror,-Wsuggest-override]', result.stderr)

    def test_implicit_alias_retains_generated_system_flag(self):
        result = self.build(implicit_alias=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_implicit_alias_keeps_project_warnings_strict(self):
        result = self.build(implicit_alias=True, project_warning=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('main.cpp:', result.stderr)
        self.assertIn('[-Werror,-Wsuggest-override]', result.stderr)

    def test_different_boost_installation_is_not_hidden(self):
        result = self.build(matching=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('homebrew prefix/include/boost/signal.hpp:', result.stderr)

    def test_other_hosts_do_not_classify_homebrew_alias(self):
        result = self.build(apple=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('homebrew prefix/include/boost/signal.hpp:', result.stderr)


if __name__ == '__main__':
    unittest.main()
