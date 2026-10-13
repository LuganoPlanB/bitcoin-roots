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
class HomebrewCapnpHeadersTest(unittest.TestCase):
    def build(self, *, apple=True, mismatch=None, project_warning=False, implicit=True):
        with tempfile.TemporaryDirectory(prefix='roots-homebrew-capnp-') as directory:
            work = Path(directory)
            cellar = work / 'Cellar/capnp/1.5.0'
            prefix = work / 'homebrew prefix'
            (prefix / 'include').mkdir(parents=True)
            for family, filename in [('capnp', 'schema.h'), ('kj', 'windows-sanity.h')]:
                headers = cellar / 'include' / family
                headers.mkdir(parents=True)
                text = '#if UNDEFINED_DEPENDENCY_MACRO\n#endif\n'
                (headers / filename).write_text(text)
                alias = prefix / 'include' / family
                if mismatch == family:
                    alias.mkdir()
                    (alias / filename).write_text(text)
                else:
                    alias.symlink_to(headers, target_is_directory=True)
            tools = work / 'tools'
            tools.mkdir()
            brew = tools / 'brew'
            brew.write_text('#!/bin/sh\ncase "$*" in\n'
                            f'  "--prefix") printf "%s\\n" {shlex.quote(str(prefix))} ;;\n'
                            '  *) exit 1 ;;\nesac\n')
            brew.chmod(0o755)
            source = work / 'source'
            vendor = source / 'vendor'
            vendor.mkdir(parents=True)
            probe = '#include <capnp/schema.h>\n#include <kj/windows-sanity.h>\n'
            if project_warning:
                probe += '#if UNDEFINED_PROJECT_MACRO\n#endif\n'
            (vendor / 'probe.cpp').write_text(probe)
            (source / 'consumer.cpp').write_text(probe)
            # Model CapnProto's directory-scoped imported targets and exported
            # include directory, without editing or building vendored sources.
            (vendor / 'CMakeLists.txt').write_text(
                f'set(CAPNP_INCLUDE_DIRECTORY "{cellar / "include"}")\n'
                'add_library(CapnProto::kj INTERFACE IMPORTED)\n'
                'set_target_properties(CapnProto::kj PROPERTIES\n'
                f'  INTERFACE_INCLUDE_DIRECTORIES "{cellar / "include"}")\n'
                'foreach(target multiprocess mputil mpgen)\n'
                '  add_library(${target} OBJECT probe.cpp)\n'
                '  target_link_libraries(${target} PUBLIC CapnProto::kj)\n'
                'endforeach()\n'
                'foreach(target mpcalculator mpprinter mpexample)\n'
                '  add_library(${target} INTERFACE)\n'
                'endforeach()\n')
            implicit_include = (f'list(APPEND CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "{prefix / "include"}")\n'
                                if implicit else '')
            (source / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.22)\n'
                'project(HomebrewCapnpHeaders LANGUAGES CXX)\n'
                f'set(CMAKE_HOST_APPLE {"TRUE" if apple else "FALSE"})\n'
                f'{implicit_include}'
                'add_library(core_interface INTERFACE)\n'
                'target_compile_options(core_interface INTERFACE -Wundef -Werror)\n'
                f'include("{ROOT / "cmake/libmultiprocess.cmake"}")\n'
                'add_libmultiprocess(vendor)\n'
                'add_library(consumer OBJECT consumer.cpp)\n'
                'target_link_libraries(consumer PRIVATE multiprocess)\n')
            # Build-time CPATH reproduces alias selection without granting
            # CMake's configure-time CPATH implicit-path filtering exemption.
            configure_env = {**os.environ, 'PATH': f'{tools}:{os.environ["PATH"]}'}
            configure_env.pop('CPATH', None)
            build = work / 'build'
            configure = subprocess.run(
                ['cmake', '-S', str(source), '-B', str(build),
                 f'-DCMAKE_CXX_COMPILER={CLANG}', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'],
                capture_output=True, text=True, env=configure_env)
            self.assertEqual(configure.returncode, 0, configure.stdout + configure.stderr)
            commands = json.loads((build / 'compile_commands.json').read_text())
            self.assertEqual(len(commands), 4)
            result = subprocess.run(
                ['cmake', '--build', str(build), '--parallel', '2', '--target',
                 'multiprocess', 'mputil', 'mpgen', 'consumer'],
                capture_output=True, text=True,
                env={**configure_env, 'CPATH': str(prefix / 'include')})
            for entry in commands:
                argv = shlex.split(entry['command'])
                system_dirs = [argv[i + 1] for i, arg in enumerate(argv[:-1]) if arg == '-isystem']
                self.assertEqual(str(prefix / 'include') in system_dirs,
                                 apple and mismatch is None, entry['command'] + result.stderr)
                self.assertIn('-Werror', argv)
                self.assertIn('-Wundef', argv)
            return result

    def test_implicit_matching_headers_emit_flag_and_compile(self):
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_explicit_matching_headers_compile(self):
        result = self.build(implicit=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_project_warnings_still_fail(self):
        result = self.build(project_warning=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('UNDEFINED_PROJECT_MACRO', result.stderr)
        self.assertIn('[-Werror,-Wundef]', result.stderr)

    def test_mismatched_capnp_is_not_hidden(self):
        result = self.build(mismatch='capnp')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('homebrew prefix/include/capnp/schema.h:', result.stderr)

    def test_mismatched_kj_is_not_hidden(self):
        result = self.build(mismatch='kj')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('homebrew prefix/include/kj/windows-sanity.h:', result.stderr)

    def test_non_apple_host_is_unchanged(self):
        result = self.build(apple=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('UNDEFINED_DEPENDENCY_MACRO', result.stderr)


if __name__ == '__main__':
    unittest.main()
