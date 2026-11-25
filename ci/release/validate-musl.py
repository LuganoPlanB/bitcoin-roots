#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Reject incomplete, dynamic, GUI or wrong-architecture musl installations."""

import argparse
import os
from pathlib import Path
import re
import subprocess

BINARIES = {'bitcoin', 'bitcoind', 'bitcoin-cli', 'bitcoin-tx', 'bitcoin-util', 'bitcoin-wallet'}
TARGETS = {
    'x86_64': ('ELF64', 'Advanced Micro Devices X86-64'),
    'arm_64': ('ELF64', 'AArch64'),
    'arm_hf': ('ELF32', 'ARM'),
    'riscv_64': ('ELF64', 'RISC-V'),
}


def validate(directory, arch):
    bin_dir = directory / 'bin'
    if bin_dir.is_symlink() or not bin_dir.is_dir() or {p.name for p in bin_dir.iterdir()} != BINARIES:
        raise ValueError('Expected exactly the six headless release executables in bin/')
    for path in directory.rglob('*'):
        if path.is_symlink() or '.so' in path.suffixes:
            raise ValueError(f'Unexpected symlink or shared library: {path}')
    elf_class, machine = TARGETS[arch]
    for name in sorted(BINARIES):
        binary = bin_dir / name
        if not binary.is_file() or not os.access(binary, os.X_OK):
            raise ValueError(f'Not a regular executable: {binary}')
        headers = subprocess.run(['readelf', '-W', '-h', '-l', '-d', str(binary)], check=True,
                                 capture_output=True, text=True, env={**os.environ, 'LC_ALL': 'C'}).stdout
        if not re.search(rf'^\s*Class:\s+{elf_class}\s*$', headers, re.MULTILINE) or not re.search(
                rf'^\s*Machine:\s+{re.escape(machine)}\s*$', headers, re.MULTILINE):
            raise ValueError(f'Wrong ELF architecture for {arch}: {binary}')
        if not re.search(r'^\s*Type:\s+(EXEC|DYN)\b', headers, re.MULTILINE):
            raise ValueError(f'Not an ELF executable: {binary}')
        if 'INTERP' in headers or '(NEEDED)' in headers:
            raise ValueError(f'Dynamic loader or shared-library dependency: {binary}')
        if arch == 'arm_hf' and 'hard-float ABI' not in headers:
            raise ValueError(f'Expected ARM hard-float ABI: {binary}')
    print(f'{arch}: six headless static ELF executables validated')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('arch', choices=TARGETS)
    args = parser.parse_args()
    try:
        validate(args.directory, args.arch)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
