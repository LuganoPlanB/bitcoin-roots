#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Reuse successful canonical CI only for a verified pure main promotion."""

import json
import os
from pathlib import Path
import re
import subprocess


# These are the reviewed main-only exceptions. CI and release workflows,
# build inputs and feature tests are deliberately not allowed exceptions.
EXCEPTIONS = {'AGENTS.md', 'CONTRIBUTING.md', 'contrib/roots/README.md',
              '.github/workflows/deploy-docs.yml', 'test/lint/lint-files.py',
              'test/lint/lint_ignore_dirs.py', 'test/lint/test_runner/src/main.rs'}


def run(*command):
    return subprocess.check_output(command, text=True, encoding='utf8').strip()


def api(path, paginated=False):
    arguments = ['gh', 'api']
    if paginated:
        arguments += ['--paginate', '--slurp']
    return json.loads(run(*arguments, path))


def verify(event, git=run, request=api):
    pr = event['pull_request']
    if not pr['head']['ref'].startswith('promote/'):
        return None
    repository = event['repository']['full_name']
    if pr['base']['ref'] != 'main' or pr['head']['repo']['full_name'] != repository:
        raise ValueError('Promotion must target main from the same repository')
    match = re.fullmatch(r'promote/([0-9]+\.[0-9]+)(?:[/\-].+)?', pr['head']['ref'])
    if not match:
        raise ValueError('Promotion branch must identify its canonical Core line')
    canonical = 'roots/' + match[1]
    head = pr['head']['sha']
    base = pr['base']['sha']
    if git('git', 'rev-parse', 'HEAD^{commit}') != head:
        raise ValueError('Promotion checkout differs from PR head')
    parents = git('git', 'show', '-s', '--format=%P', head).split()
    if len(parents) != 2 or parents[0] != base:
        raise ValueError('Promotion must have exact current main and canonical parents')
    candidate = parents[1]
    remote = git('git', 'ls-remote', f'https://github.com/{repository}.git', f'refs/heads/{canonical}')
    if remote.split() != [candidate, f'refs/heads/{canonical}']:
        raise ValueError('Promotion candidate differs from current canonical tip')
    # Git diff includes content, mode and symlink-target changes together.
    differences = git('git', 'diff', '--no-renames', '--name-only', candidate, head).splitlines()
    if any(path not in EXCEPTIONS and not path.startswith('doc/website/') for path in differences):
        raise ValueError('Promotion product/build/workflow differs from canonical')
    workflow = request(f'repos/{repository}/actions/workflows/ci.yml')
    # GitHub can clear run.pull_requests after merge. Associated PRs retain
    # the exact source head/ref and canonical target needed for provenance.
    pr_pages = request(f'repos/{repository}/commits/{candidate}/pulls?per_page=100', True)
    source_prs = [source for page in pr_pages for source in page
                  if source['base']['ref'] == canonical and source['head']['sha'] == candidate
                  and source['head']['repo']['full_name'] == repository
                  and source['base']['repo']['full_name'] == repository]
    if len(source_prs) != 1:
        raise ValueError('No unique exact canonical-target candidate PR')
    source_branch = source_prs[0]['head']['ref']
    pages = request(f'repos/{repository}/actions/workflows/ci.yml/runs?head_sha={candidate}&event=pull_request&per_page=100', True)
    if not pages or sum(len(page['workflow_runs']) for page in pages) != pages[0]['total_count']:
        raise ValueError('Candidate CI run inventory is incomplete')
    runs = [item for page in pages for item in page['workflow_runs']]
    matching = []
    for item in runs:
        if (item['head_sha'] != candidate or item['event'] != 'pull_request' or
                item['workflow_id'] != workflow['id'] or item['path'] != '.github/workflows/ci.yml' or
                item['repository']['full_name'] != repository or
                item['head_repository']['full_name'] != repository):
            raise ValueError('Candidate CI run identity mismatch')
        if item['head_branch'] == source_branch:
            matching.append(item)
    if not matching:
        raise ValueError('No exact canonical-target candidate CI evidence')
    latest = max(matching, key=lambda item: (item['created_at'], item['id']))
    if latest['status'] != 'completed' or latest['conclusion'] != 'success':
        raise ValueError('Latest exact candidate CI is not successful')
    job_pages = request(f'repos/{repository}/actions/runs/{latest["id"]}/jobs?per_page=100', True)
    jobs = [job for page in job_pages for job in page['jobs']]
    if not job_pages or len(jobs) != job_pages[0]['total_count']:
        raise ValueError('Candidate job inventory is incomplete')
    for name in ('classify changes', 'lint', 'required result'):
        matches = [job for job in jobs if job['name'] == name]
        if len(matches) != 1 or matches[0]['status'] != 'completed' or matches[0]['conclusion'] != 'success':
            raise ValueError(f'Candidate required evidence failed: {name}')
    return {'candidate': candidate, 'run_id': latest['id'], 'canonical': canonical,
            'exceptions': differences}


def main():
    event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
    # Explicit CI labels request fresh coverage, including reproducibility.
    labels = [label['name'] for label in event['pull_request']['labels']]
    requested = any(label.startswith('ci:') for label in labels)
    proof = None if requested else verify(event)
    reuse = proof is not None
    with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf8') as output:
        output.write(f"reuse={'true' if reuse else 'false'}\n")
    print(json.dumps({'reuse': reuse, 'proof': proof}, sort_keys=True))


if __name__ == '__main__':
    main()
