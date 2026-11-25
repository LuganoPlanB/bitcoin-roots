#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Exercise static ELF guards, cross-build wiring and musl release assembly."""

import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import tarfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import test_prepare_release as packages

ROOT = packages.ROOT
spec = importlib.util.spec_from_file_location('musl', ROOT / 'ci/release/validate-musl.py')
musl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(musl)
TRIPLES = {'x86_64': 'x86_64-linux-musl', 'arm_64': 'aarch64-linux-musl',
           'arm_hf': 'arm-linux-musleabihf', 'riscv_64': 'riscv64-linux-musl'}


class MuslReleaseTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='roots-musl-fixture-')
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        self.install = self.work / 'install'
        (self.install / 'bin').mkdir(parents=True)
        for name in musl.BINARIES:
            binary = self.install / 'bin' / name
            binary.write_bytes(b'ELF fixture')
            binary.chmod(0o755)

    def headers(self, arch):
        elf_class, machine = musl.TARGETS[arch]
        return f'  Class: {elf_class}\n  Machine: {machine}\n  Type: EXEC (Executable file)\n  Flags: hard-float ABI\n'

    def test_release_matrix_pins_sdk_and_requires_all_four_builds(self):
        workflow = (ROOT / '.github/workflows/reusable-musl-release.yml').read_text()
        block = workflow.split('  musl-release:\n', 1)[1]
        self.assertIn('runs-on: ubuntu-24.04', block)
        self.assertIn('dyne/musl-action@fe1d1871b096824ddb7aa158f68dd63dabdc9586', block)
        self.assertIn('version: v2.6.0', block)
        self.assertIn('target_arch: ${{ matrix.arch }}', block)
        for arch in TRIPLES:
            self.assertIn(f'arch: {arch}\n            sha256: ', block)
        self.assertIn('CC: ${{ steps.musl.outputs.CC }}', block)
        self.assertIn('CXX: ${{ steps.musl.outputs.CXX }}', block)
        self.assertLess(block.index('python3 ci/release/validate-musl.py'), block.index('tar -C'))
        self.assertLess(block.index('archive.py validate'), block.index('name: Upload unsigned release package'))
        release = (ROOT / '.github/workflows/release.yml').read_text()
        self.assertIn('musl-release', release.split('  publish-release:\n', 1)[1].split('    steps:', 1)[0])
        self.assertIn('uses: ./.github/workflows/reusable-musl-release.yml', release)
        self.assertNotIn('validate-release-source: false', release)
        self.assertIn('default: true\n        type: boolean', workflow)
        self.assertIn('if: inputs.validate-release-source', workflow)

    def test_draft_pr_builds_exact_head_with_readonly_token_and_no_release_actions(self):
        workflow = (ROOT / '.github/workflows/musl.yml').read_text()
        self.assertIn('pull_request:', workflow)
        self.assertNotIn('pull_request_target:', workflow)
        self.assertIn('types: [opened, reopened, synchronize]', workflow)
        self.assertIn('source-ref: ${{ github.event.pull_request.head.sha }}', workflow)
        self.assertIn('uses: ./.github/workflows/reusable-musl-release.yml', workflow)
        self.assertIn('validate-release-source: false', workflow)
        self.assertIn('permissions:\n  contents: read', workflow)
        for source in [workflow, (ROOT / '.github/workflows/reusable-musl-release.yml').read_text()]:
            self.assertNotIn('secrets.', source)
            self.assertNotIn('environment: release', source)
            self.assertNotIn('sign-manifest', source)
            self.assertNotIn('create-ci-draft', source)

    def test_all_target_architectures_require_static_elf_and_hardfloat(self):
        for arch in TRIPLES:
            with self.subTest(arch=arch), patch.object(musl.subprocess, 'run') as run:
                run.return_value = SimpleNamespace(stdout=self.headers(arch))
                musl.validate(self.install, arch)
                self.assertEqual(run.call_count, 6)
                for invalid in ('INTERP', '(NEEDED) Shared library: [libc.so]'):
                    run.return_value.stdout = self.headers(arch) + invalid
                    with self.assertRaisesRegex(ValueError, 'Dynamic loader'):
                        musl.validate(self.install, arch)
                run.return_value.stdout = self.headers(arch).replace('hard-float ABI', 'soft-float ABI')
                if arch == 'arm_hf':
                    with self.assertRaisesRegex(ValueError, 'hard-float'):
                        musl.validate(self.install, arch)

    def test_wrong_machine_class_or_nonexecutable_elf_is_rejected(self):
        for headers in (self.headers('arm_64'), self.headers('x86_64').replace('ELF64', 'ELF32'),
                        self.headers('x86_64').replace('EXEC', 'REL')):
            with self.subTest(headers=headers), patch.object(musl.subprocess, 'run', return_value=SimpleNamespace(stdout=headers)):
                with self.assertRaises(ValueError):
                    musl.validate(self.install, 'x86_64')

    @patch.object(musl.subprocess, 'run')
    def test_missing_gui_shared_symlink_or_nonexecutable_payload_is_rejected(self, run):
        run.return_value = SimpleNamespace(stdout=self.headers('x86_64'))
        binary = self.install / 'bin/bitcoin-cli'
        binary.unlink()
        with self.assertRaisesRegex(ValueError, 'six headless'):
            musl.validate(self.install, 'x86_64')
        binary.write_bytes(b'fixture')
        with self.assertRaisesRegex(ValueError, 'regular executable'):
            musl.validate(self.install, 'x86_64')
        binary.chmod(0o755)
        for extra in ('bin/bitcoin-qt', 'lib/libsqlite3.so.0', 'link'):
            with self.subTest(extra=extra):
                path = self.install / extra
                path.parent.mkdir(exist_ok=True)
                if extra == 'link':
                    path.symlink_to(binary)
                else:
                    path.write_bytes(b'unexpected')
                with self.assertRaises(ValueError):
                    musl.validate(self.install, 'x86_64')
                path.unlink()

    @unittest.skipUnless(shutil.which('cc') and shutil.which('readelf'), 'C compiler and readelf required')
    def test_real_static_and_dynamic_elf_files(self):
        if os.uname().machine != 'x86_64':
            self.skipTest('native ELF execution fixture requires x86_64')
        source = self.work / 'main.c'
        source.write_text('int main(void) { return 0; }\n')
        static = self.work / 'static'
        subprocess.run(['cc', '-nostdlib', '-static', '-Wl,-e,main', str(source), '-o', str(static)], check=True)
        for name in musl.BINARIES:
            shutil.copyfile(static, self.install / 'bin' / name)
        musl.validate(self.install, 'x86_64')
        binary = self.install / 'bin/bitcoin-cli'
        subprocess.run(['cc', '-fPIC', '-pie', str(source), '-o', str(binary), '-static-pie'], check=True)
        musl.validate(self.install, 'x86_64')
        subprocess.run([binary], check=True)
        subprocess.run(['cc', '-static', '-fPIC', '-pie', str(source), '-o', str(binary), '-no-pie'], check=True)
        musl.validate(self.install, 'x86_64')
        subprocess.run([binary], check=True)
        subprocess.run(['cc', str(source), '-o', str(binary)], check=True)
        with self.assertRaisesRegex(ValueError, 'Dynamic loader'):
            musl.validate(self.install, 'x86_64')
        binary.write_bytes(b'not ELF')
        with self.assertRaises(subprocess.CalledProcessError):
            musl.validate(self.install, 'x86_64')

    def sdk_environment(self, arch):
        triple = TRIPLES[arch]
        sdk = self.work / arch / 'sdk with spaces'
        compiler = sdk / 'gcc-musl'
        target = sdk / triple
        for name in ('settings.cmake', f'bin/{triple}-gcc', f'bin/{triple}-g++'):
            path = compiler / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f'#!/bin/sh\nprintf "%s\\n" "{triple}"\n')
            path.chmod(0o755)
        for name in ('include/boost/version.hpp', 'lib/libsqlite3.a', 'lib/libzmq.a'):
            path = target / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        tools = sdk / 'host-tools'
        tools.mkdir()
        (tools / 'cmake').write_text(
            '#!/usr/bin/env python3\nimport json,os,sys\n'
            'with open(os.environ["MUSL_TEST_CALLS"], "a") as out: out.write(json.dumps(sys.argv[1:]) + "\\n")\n'
            'sys.exit(17 if os.environ.get("MUSL_TEST_FAIL") else 0)\n')
        (tools / 'readelf').write_text('#!/bin/sh\ncat <<\'EOF\'\n' + self.headers(arch) + 'EOF\n')
        for path in tools.iterdir():
            path.chmod(0o755)
        return {**os.environ, 'PATH': f'{tools}:{os.environ["PATH"]}',
                'CC': str(compiler / f'bin/{triple}-gcc'), 'CXX': str(compiler / f'bin/{triple}-g++'),
                'RELEASE_TAG': 'v32.1-roots.3', 'MUSL_TEST_CALLS': str(sdk / 'calls.jsonl')}

    def test_action_compilers_configure_every_target_and_fail_before_install(self):
        for arch, triple in TRIPLES.items():
            with self.subTest(arch=arch):
                env = self.sdk_environment(arch)
                build = self.work / arch / 'build'
                install = self.work / arch / 'out'
                command = [ROOT / 'ci/release/build-musl.sh', arch, build, install]
                # Stub only the CMake boundary, preserving script failure handling.
                failed = subprocess.run(command, env={**env, 'MUSL_TEST_FAIL': '1'}, capture_output=True, text=True)
                self.assertEqual(failed.returncode, 17, failed.stderr)
                self.assertFalse(install.exists())
                calls = Path(env['MUSL_TEST_CALLS'])
                self.assertEqual(len(calls.read_text().splitlines()), 1)
                arguments = json.loads(calls.read_text())
                for expected in (f'-DARCH={triple}', f'-DCMAKE_C_COMPILER={env["CC"]}',
                                 f'-DCMAKE_CXX_COMPILER={env["CXX"]}', '-DFORCE_STATIC=OFF',
                                 '-DAPPEND_CXXFLAGS=-fPIC',
                                 '-DBUILD_GUI=OFF', '-DENABLE_IPC=OFF', '-DENABLE_WALLET=ON',
                                 '-DWITH_ZMQ=ON', '-DBUILD_WALLET_TOOL=ON', '-DBUILD_TESTS=OFF',
                                 '-DCLIENT_VERSION_TAG=v32.1-roots.3'):
                    self.assertIn(expected, arguments)
                if arch == 'arm_hf':
                    self.assertIn('-DCMAKE_EXE_LINKER_FLAGS=-static -static-libgcc -static-libstdc++ -no-pie', arguments)
                    self.assertIn('-DAPPEND_LDFLAGS=-no-pie', arguments)
                else:
                    self.assertIn('-DCMAKE_EXE_LINKER_FLAGS=-static-pie -static-libgcc -static-libstdc++', arguments)
                    self.assertIn('-DAPPEND_LDFLAGS=-static-pie', arguments)
                for language in ('C', 'CXX'):
                    flags = next(arg.split('=', 1)[1].split() for arg in arguments
                                 if arg.startswith(f'-DCMAKE_{language}_FLAGS='))
                    self.assertIn('-O2', flags)
                    self.assertNotIn('-Os', flags)
                    self.assertNotIn('-ffast-math', flags)
                    self.assertNotIn('-static', flags)
                    self.assertIn('-fPIC', flags)
                calls.unlink()
                shutil.rmtree(build)
                # The fixture simulates cmake --install creating the payload.
                # Create it from the install stub only, after the output guard.
                cmake = Path(env['PATH'].split(os.pathsep)[0]) / 'cmake'
                cmake.write_text(cmake.read_text().replace('sys.exit(17',
                    'if sys.argv[1] == "--install":\n'
                    f' import shutil; shutil.copytree({str(self.install)!r}, {str(install)!r})\n'
                    'sys.exit(17'))
                result = subprocess.run(command, env=env, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                actual = [json.loads(line) for line in calls.read_text().splitlines()]
                self.assertEqual(actual[1], ['--build', str(build), '--parallel', '2'])
                self.assertEqual(actual[2], ['--install', str(build), '--strip'])
                self.assertNotEqual(subprocess.run(command, env=env, capture_output=True).returncode, 0)

    def test_nine_package_assembly_checksums_all_musl_assets(self):
        downloads = self.work / 'downloads'
        downloads.mkdir()
        platforms = ['linux-x86_64', 'linux-aarch64', 'darwin-x86_64', 'darwin-arm64', 'windows-x86_64']
        platforms += [f'musl-{arch}' for arch in TRIPLES]
        for platform in platforms:
            suffix = 'tar.gz' if platform.startswith(('linux', 'musl')) else 'zip'
            packages.PrepareReleaseTest().write_package(downloads / f'bitcoin-roots-{platform}.{suffix}',
                                                       'bitcoin-roots-32.1-roots.3', b'fixture', notices=False)
        (downloads / 'bitcoin-roots-32.1-roots.3.patch').write_text('patch fixture\n')
        output = self.work / 'assets'
        subprocess.run([packages.SCRIPT, downloads, output, packages.PUBLIC_KEY, 'v32.1-roots.3', '9'], check=True)
        entries = [line for line in (output / 'SHA512SUMS').read_text().splitlines() if not line.startswith('#')]
        self.assertEqual(len(entries), 10)
        subprocess.run(['sha512sum', '--check', 'SHA512SUMS'], cwd=output, check=True, capture_output=True)

    @unittest.skipUnless(shutil.which('cc') and shutil.which('readelf'), 'C compiler and readelf required')
    def test_actual_musl_workflow_packages_static_payload_with_copying(self):
        if os.uname().machine != 'x86_64':
            self.skipTest('native ELF fixture requires x86_64')
        source = self.work / 'entry.c'
        source.write_text('int main(void) { return 0; }\n')
        static = self.work / 'static'
        subprocess.run(['cc', '-nostdlib', '-static', '-Wl,-e,main', str(source), '-o', str(static)], check=True)
        for name in musl.BINARIES:
            shutil.copyfile(static, self.install / 'bin' / name)
        self.install.rename(self.work / 'musl-install')
        workflow = (ROOT / '.github/workflows/reusable-musl-release.yml').read_text()
        block = workflow.split('      - name: Package static headless binaries\n', 1)[1]
        block = block.split('        run: |\n', 1)[1].split('\n      - name:', 1)[0]
        script = '\n'.join(line[10:] for line in block.splitlines())
        subprocess.run(['bash', '-c', script], cwd=ROOT, check=True,
                       env={**os.environ, 'RUNNER_TEMP': str(self.work), 'MUSL_ARCH': 'x86_64',
                            'RELEASE_TAG': 'v32.1-roots.3'})
        archive = self.work / 'release-packages/bitcoin-roots-musl-x86_64.tar.gz'
        with tarfile.open(archive) as package:
            self.assertEqual(package.extractfile('bitcoin-roots-32.1-roots.3/COPYING').read(),
                             (ROOT / 'COPYING').read_bytes())
            for name in musl.BINARIES:
                self.assertEqual(package.extractfile(f'bitcoin-roots-32.1-roots.3/bin/{name}').read(), static.read_bytes())


if __name__ == '__main__':
    unittest.main()
