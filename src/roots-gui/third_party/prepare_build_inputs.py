# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Prepare/verify local GUI inputs; generation identities live only in the build."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot(directory):
    return {p.relative_to(directory).as_posix(): {'link': str(p.readlink())} if p.is_symlink()
            else {'sha256': digest(p)} for p in sorted(directory.rglob('*')) if p.is_file() or p.is_symlink()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path, required=True)
    destination = parser.parse_args().destination.resolve()
    source = Path(__file__).resolve().parent
    repository = source.parents[2]
    if destination == repository or repository in destination.parents:
        raise ValueError('build inputs must be outside the source repository')
    destination.mkdir(parents=True, exist_ok=True)
    ui = json.loads((source/'ui-lock.json').read_text())
    ui_destination = destination/('ui-' + ui['sha256'][:16])
    subprocess.run([sys.executable, str(source/'prepare_ui.py'), '--destination', str(ui_destination)],
                   stdout=sys.stderr, check=True)
    provider = json.loads((source/'provider-lock.json').read_text())
    inputs = ['provider-lock.json'] + [provider[k]['archive'] for k in ['libwally', 'bips_vectors']]
    inputs += [p['path'] for p in provider['libwally']['patches']]
    inputs += [p['path'] for p in provider.get('license_texts', [])]
    identity = hashlib.sha256(json.dumps({p: digest(source/p) for p in inputs}, sort_keys=True).encode()).hexdigest()
    provider_destination = destination/('provider-' + identity[:16])
    stamp = destination/('provider-' + identity[:16] + '.json')
    if not stamp.exists():
        subprocess.run([sys.executable, str(source/'prepare_provider.py'), '--destination', str(provider_destination)],
                       stdout=sys.stderr, check=True)
        stamp.write_text(json.dumps(snapshot(provider_destination), sort_keys=True)+'\n')
    if json.loads(stamp.read_text()) != snapshot(provider_destination):
        raise ValueError('prepared provider source changed; use a fresh build input directory')
    print(json.dumps({'uiSource': str(ui_destination), 'providerSource': str(provider_destination/'libwally-core-1.6.1'),
                      'providerIdentity': identity, 'uiIdentity': ui['sha256']}))


if __name__ == '__main__':
    main()
