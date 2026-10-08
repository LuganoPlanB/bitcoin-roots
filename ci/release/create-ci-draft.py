#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Create one signed CI draft from this run's validated five-platform outputs."""

import argparse
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

spec = importlib.util.spec_from_file_location('draft', Path(__file__).with_name('create-draft.py'))
draft = importlib.util.module_from_spec(spec)
spec.loader.exec_module(draft)


def verify_signature(manifest, signature, public_key, fingerprint, home):
    if not re.fullmatch(r'[0-9A-F]{40}', fingerprint):
        raise ValueError('Invalid expected signing fingerprint')
    environment = {**os.environ, 'GNUPGHOME': str(home)}
    home.mkdir(mode=0o700)
    imported = draft.run(['gpg', '--batch', '--with-colons', '--import-options', 'show-only',
                          '--import', str(public_key)], env=environment)
    fingerprints = [line.split(':')[9] for line in imported.splitlines() if line.startswith('fpr:')]
    if not fingerprints or fingerprints[0] != fingerprint:
        raise ValueError('Release public key differs from expected fingerprint')
    draft.run(['gpg', '--batch', '--import', str(public_key)], env=environment)
    status = draft.run(['gpg', '--batch', '--status-fd=1', '--verify', str(signature), str(manifest)], env=environment)
    valid = [line.split() for line in status.splitlines() if line.startswith('[GNUPG:] VALIDSIG ')]
    if len(valid) != 1 or fingerprint not in (valid[0][2], valid[0][-1]):
        raise ValueError('Manifest signature does not match expected release key')


def create(args):
    expected = {*draft.PACKAGE_NAMES, f'bitcoin-roots-{args.tag[1:]}.patch', 'SHA512SUMS', 'SHA512SUMS.asc'}
    if not args.assets.is_dir() or args.assets.is_symlink() or set(p.name for p in args.assets.iterdir()) != expected:
        raise ValueError('Signed CI draft requires exactly eight release assets')
    for path in args.assets.iterdir():
        if path.is_symlink() or not path.is_file() or not path.stat().st_size:
            raise ValueError('Release asset must be a nonempty regular file')
    if not re.fullmatch(r'[1-9][0-9]*', args.run_id):
        raise ValueError('CI run identity must be a positive integer')
    args.commit = draft.run(['git', 'rev-parse', 'HEAD^{commit}'])
    with tempfile.TemporaryDirectory(prefix='roots-signed-ci-draft-') as temporary_dir:
        temporary = Path(temporary_dir)
        unsigned = temporary / 'unsigned'
        unsigned.mkdir()
        for name in expected - {'SHA512SUMS.asc'}:
            shutil.copyfile(args.assets / name, unsigned / name)
        template = args.notes.read_text()
        notes = temporary / 'notes.md'
        notes.write_text(template + '\n## CI build and signing provenance\n\n'
                         f'All five platform packages were built from `{args.commit}` in '
                         f'[GitHub Actions run {args.run_id}](https://github.com/{args.repository}/actions/runs/{args.run_id}). '
                         'Dependency notices were collected from each build runner.\n\n'
                         f'`SHA512SUMS.asc` signs the SHA512 manifest with release key `{args.fingerprint}`. '
                         'This checksum signature is separate from Git-tag and platform-code signing. '
                         'The annotated Git tag is unsigned. macOS applications retain ad-hoc signatures, '
                         'without Developer ID signing or notarization; Windows executables have no Authenticode signature.\n')
        args.assets = unsigned
        args.notes = notes
        args.manifest_sha512 = draft.digest(unsigned / 'SHA512SUMS', 'sha512')
        args.notes_sha256 = draft.digest(notes, 'sha256')
        snapshot = temporary / 'snapshot'
        snapshot.mkdir()
        names, patch, snapshot_notes = draft.snapshot_packet(args, snapshot)
        # snapshot_packet validates every checksum, COPYING and platform notice index.
        signature = snapshot / 'SHA512SUMS.asc'
        shutil.copyfile(Path(args.original_assets) / signature.name, signature)
        verify_signature(snapshot / 'SHA512SUMS', signature, args.public_key, args.fingerprint, temporary / 'verify-home')
        tag_object, core_version = draft.validate_source(args, snapshot / patch, temporary)
        # Refresh identities/inventory only after all slow local checks. Existing
        # drafts/public releases fail; an interrupted create is inspected by an operator.
        draft.validate_remote(args, tag_object, core_version)
        command = ['gh', 'release', 'create', args.tag, '--repo', args.repository, '--draft', '--verify-tag',
                   '--title', f'Bitcoin Roots {args.tag}', '--notes-file', str(snapshot_notes)]
        if re.search(r'rc[0-9]+-roots\.', args.tag):
            command.append('--prerelease')
        command.extend(str(snapshot / name) for name in sorted([*names, signature.name]))
        print(draft.run(command))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('tag', 'repository', 'fingerprint', 'run-id'):
        parser.add_argument('--' + name, required=True)
    for name in ('assets', 'notes', 'public-key'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    args.original_assets = args.assets
    try:
        create(args)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
