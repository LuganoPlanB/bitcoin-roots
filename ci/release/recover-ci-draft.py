#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Read-only preflight for one isolated recovery; never merge into product branches."""

import json
import os
import subprocess

REPOSITORY = 'LuganoPlanB/bitcoin-roots'
RUN = 37849202986
TAG = 'v30.3-roots.1'
TAG_OBJECT = 'd04b1e37b4da40dade3be1c5f29349e4c2235d3c'
SOURCE = '17e1d484aba2a881be03b8928fba3c0125e027a2'
MAIN = '6ff7e060e56a01883de68e2f95d50f4a048e1358'
BRANCH = 'refs/heads/recovery/30.3-ci-draft'
WORKFLOW = 354483895
PRODUCERS = {
    113557621044: 'Release metadata and patch series',
    113557930514: 'Windows x86_64 release',
    113557930661: 'macOS x86_64 release',
    113557930724: 'macOS arm64 release',
    113557930739: 'Linux aarch64 release',
    113557930759: 'Linux x86_64 release',
}
ARTIFACTS = {
    11584717786: 'bitcoin-roots-windows-x86_64-v30.3-roots.1',
    11583260428: 'bitcoin-roots-linux-x86_64-v30.3-roots.1',
    11582653048: 'bitcoin-roots-darwin-x86_64-v30.3-roots.1',
    11582291666: 'bitcoin-roots-linux-aarch64-v30.3-roots.1',
    11581911271: 'bitcoin-roots-patch-v30.3-roots.1',
    11580919490: 'bitcoin-roots-darwin-arm64-v30.3-roots.1',
}


def run(command):
    return subprocess.check_output(command, text=True, encoding='utf8').strip()


def api(path, pages=False):
    command = ['gh', 'api']
    if pages:
        command += ['--paginate', '--slurp']
    return json.loads(run(command + [f'repos/{REPOSITORY}/{path}']))


def inventory(path, key):
    pages = api(path, pages=True)
    if not pages or not all(isinstance(p, dict) and isinstance(p.get(key), list) for p in pages):
        raise ValueError('Invalid paginated inventory')
    values = [value for page in pages for value in page[key]]
    if any(page.get('total_count') != len(values) for page in pages):
        raise ValueError('Incomplete or changing inventory')
    return values


def verify():
    if (os.environ.get('GITHUB_REPOSITORY') != REPOSITORY
            or os.environ.get('GITHUB_EVENT_NAME') != 'workflow_dispatch'
            or os.environ.get('GITHUB_REF') != BRANCH
            or os.environ.get('RECOVERY_RUN') != str(RUN)
            or os.environ.get('RELEASE_TAG') != TAG
            or os.environ.get('RELEASE_COMMIT') != SOURCE):
        raise ValueError('Recovery requires its exact isolated branch and original run')
    original = api(f'actions/runs/{RUN}')
    if (original.get('repository', {}).get('full_name') != REPOSITORY
            or original.get('head_repository', {}).get('full_name') != REPOSITORY
            or original.get('workflow_id') != WORKFLOW
            or original.get('path') != '.github/workflows/release.yml'
            or original.get('event') != 'push' or original.get('head_branch') != TAG
            or original.get('head_sha') != SOURCE or original.get('status') != 'completed'
            or original.get('conclusion') != 'failure'):
        raise ValueError('Original build run identity differs')
    jobs = inventory(f'actions/runs/{RUN}/jobs?per_page=100', 'jobs')
    producers = [job for job in jobs if job.get('name') in PRODUCERS.values()]
    if len(producers) != 6 or {job['id']: job['name'] for job in producers} != PRODUCERS:
        raise ValueError('Producer identity differs or is ambiguous')
    if any(job.get('status') != 'completed' or job.get('conclusion') != 'success' for job in producers):
        raise ValueError('Every original producer must have succeeded')
    assets = inventory(f'actions/runs/{RUN}/artifacts?per_page=100', 'artifacts')
    if len(assets) != 6 or {asset['id']: asset['name'] for asset in assets} != ARTIFACTS:
        raise ValueError('Original artifact inventory differs')
    if any(asset.get('expired') is not False or asset.get('size_in_bytes', 0) <= 0
           or asset.get('workflow_run', {}).get('id') != RUN
           or asset.get('workflow_run', {}).get('head_sha') != SOURCE for asset in assets):
        raise ValueError('Original artifact is expired, empty or from another run')
    for identity, head in [(37835146685, SOURCE), (37847473824, '673c29ac0e8ad616a6054c2325b9bc50e84b7ac0')]:
        proof = api(f'actions/runs/{identity}')
        if (proof.get('repository', {}).get('full_name') != REPOSITORY
                or proof.get('workflow_id') != 352575464
                or proof.get('path') != '.github/workflows/ci.yml'
                or proof.get('event') != 'pull_request' or proof.get('head_sha') != head
                or proof.get('status') != 'completed' or proof.get('conclusion') != 'success'):
            raise ValueError('Required source/promotion CI proof differs')
    refs = dict(line.split('\t')[::-1] for line in run([
        'git', 'ls-remote', f'https://github.com/{REPOSITORY}.git',
        'refs/heads/roots/30.3', 'refs/heads/main', f'refs/tags/{TAG}', f'refs/tags/{TAG}^{{}}',
    ]).splitlines())
    if refs != {'refs/heads/roots/30.3': SOURCE, 'refs/heads/main': MAIN,
                f'refs/tags/{TAG}': TAG_OBJECT, f'refs/tags/{TAG}^{{}}': SOURCE}:
        raise ValueError('Live source/main/tag identities differ')
    releases = api('releases?per_page=100', pages=True)
    if not isinstance(releases, list) or any(not isinstance(page, list) for page in releases):
        raise ValueError('Invalid release inventory')
    if any(not isinstance(release, dict) or 'tag_name' not in release
           or release['tag_name'] == TAG for page in releases for release in page):
        raise ValueError('Release exists or inventory is ambiguous')
    return {'original_run': RUN, 'source': SOURCE, 'tag_object': TAG_OBJECT,
            'producer_jobs': PRODUCERS, 'artifacts': ARTIFACTS}


if __name__ == '__main__':
    print(json.dumps(verify(), sort_keys=True))
