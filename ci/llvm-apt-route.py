#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Select a fresh LLVM APT route without changing its HTTPS identity or trust."""

import argparse
from datetime import datetime, timezone
import ipaddress
from pathlib import Path
import subprocess
import sys


HOST = 'apt.llvm.org'
# Reviewed LLVM CNAME target. Revisit this if LLVM changes CDN providers.
FALLBACK = 'dualstack.l2.shared.us-eu.fastly.net'


def addresses(host):
    try:
        result = subprocess.run(['getent', 'ahostsv4', host], capture_output=True,
                                text=True, timeout=8, check=False)
    except subprocess.TimeoutExpired:
        return []
    if result.returncode:
        return []
    found = []
    for line in result.stdout.splitlines():
        try:
            address = ipaddress.IPv4Address(line.split()[0])
        except (ValueError, IndexError):
            continue
        if address.is_global and str(address) not in found:
            found.append(str(address))
    return found[:2]


def select_route():
    candidates = addresses(HOST)
    route = HOST
    if not candidates:
        route = FALLBACK
        candidates = addresses(route)
    for address in candidates:
        # --resolve changes the destination only: Host, SNI and certificate
        # verification still use apt.llvm.org. Never use --insecure.
        try:
            result = subprocess.run([
                'curl', '--fail', '--silent', '--show-error', '--ipv4',
                '--noproxy', HOST, '--proto', '=https', '--connect-timeout', '5',
                '--max-time', '10', '--resolve', f'{HOST}:443:{address}',
                '--output', '/dev/null', f'https://{HOST}/llvm-snapshot.gpg.key',
            ], capture_output=True, text=True, timeout=12, check=False)
        except subprocess.TimeoutExpired:
            continue
        if result.returncode == 0:
            return address, route
    raise RuntimeError('No verified LLVM APT route; stopping before the expensive build')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--github-env', type=Path, required=True)
    args = parser.parse_args()
    try:
        address, route = select_route()
    except (OSError, RuntimeError) as error:
        parser.exit(1, f'{error}\n')
    with args.github_env.open('a', encoding='utf-8') as output:
        output.write(f'ROOTS_APT_LLVM_IPV4={address}\n')
    print(f'{datetime.now(timezone.utc).isoformat()} LLVM APT route: {route} -> {address}; HTTPS verified')
    return 0


if __name__ == '__main__':
    sys.exit(main())
