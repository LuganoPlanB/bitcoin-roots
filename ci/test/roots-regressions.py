#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Select affected Roots regressions and enforce fast/nightly execution budgets.

This selects commands for the native runners; it is not a release-state store.
Incomplete or unknown paths deliberately select every Roots regression group.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import sys

GROUPS = {
    "policy": ("feature_policy_args.py", "mempool_datacarrier.py", "mempool_subdust_fee_penalty.py", "mempool_dynamic_dust.py", "mempool_roots_local_policy.py", "mempool_graph_policy.py", "mempool_rbf_policy.py"),
    "mining": ("mining_roots_template.py", "mempool_truc.py", "interface_ipc_mining.py"),
    "wallet": ("wallet_send.py", "wallet_sweepprivkeys.py", "wallet_signer.py", "wallet_exportaddresskey.py", "wallet_dangerous_rpc.py"),
    "net": ("p2p_filter.py", "p2p_invalid_messages.py", "rpc_net.py", "p2p_permissions.py", "p2p_addr_relay.py", "p2p_getaddr_caching.py", "p2p_roots_controls.py", "rpc_getblockfrompeer.py", "p2p_compactblocks_extratxs.py"),
    "runtime": ("feature_config_args.py", "interface_http.py", "rpc_effective_policy.py", "rpc_uptime.py"),
}
FUZZ = {
    "policy": ("policy_args", "subdust_fee_penalty", "dynamic_dust_fee"),
    "mining": ("tx_pool", "txgraph"),
    "wallet": ("script", "psbt"),
    "net": ("process_message",),
    "runtime": ("policy_args", "string"),
}
BUDGETS = {"fast": {"functional": 900, "fuzz": 5}, "nightly": {"functional": 7200, "fuzz": 60}}


def affected(files):
    groups = set()
    if not isinstance(files, list) or not files:
        return sorted(GROUPS)
    for path in files:
        if not isinstance(path, str) or any(part in ("", ".", "..") for part in path.split('/')) or '\\' in path:
            return sorted(GROUPS)
        owners = {group for group, tests in GROUPS.items() if path in {'test/functional/' + test for test in tests}}
        if owners:
            groups.update(owners)
            continue
        if path.startswith(('doc/man/', 'share/examples/')):
            return sorted(GROUPS)
        if path.startswith(('doc/', 'share/', 'src/qt/res/')) or path == 'README.md':
            continue
        if path.startswith(('src/wallet/', 'src/qt/')) or path.startswith('test/functional/wallet_'):
            groups.add('wallet')
        elif path.startswith(('src/net', 'src/protocol', 'test/functional/p2p_')) or path in ('test/functional/rpc_net.py', 'test/functional/rpc_getblockfrompeer.py'):
            groups.add('net')
        elif path in ('src/rpc/net.cpp', 'src/rpc/blockchain.cpp'):
            groups.update(('net', 'runtime', 'policy'))
        elif path.startswith(('src/node/miner', 'src/rpc/mining', 'src/ipc/', 'src/interfaces/mining')) or 'mining_' in path:
            groups.update(('mining', 'policy'))
        elif path.startswith('src/node/mempool'):
            groups.update(('policy', 'mining', 'runtime'))
        elif path.startswith(('src/policy/', 'src/txmempool', 'src/txgraph', 'src/validation')) or path.startswith('test/functional/mempool_'):
            groups.update(('policy', 'mining'))
        elif path.startswith(('src/common/', 'src/util/', 'src/rpc/')) or path.endswith(('feature_config_args.py', 'rpc_effective_policy.py', 'rpc_uptime.py')):
            groups.update(('runtime', 'policy'))
        else:
            return sorted(GROUPS)
    return sorted(groups)


def selection(files, cadence):
    groups = affected(files)
    return {"groups": groups, "functional": sorted({test for group in groups for test in GROUPS[group]}),
            "fuzz": sorted({target for group in groups for target in FUZZ[group]}), "budgets": BUDGETS[cadence]}


def run_bounded(command, timeout, log, env=None):
    """Kill only this owned process group on timeout and retain failure output."""
    with log.open('w', encoding='utf-8') as output:
        try:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT, env=env,
                                       start_new_session=os.name != 'nt')
        except OSError as error:
            output.write(str(error) + '\n')
            return 127
        try:
            return process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            if os.name == 'nt':
                subprocess.run(['taskkill', '/F', '/T', '/PID', str(process.pid)], check=False, stdout=output, stderr=output)
            else:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
                # The leader may exit on TERM while a child ignores it. Reap
                # the entire group we created, even after its leader settles.
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            process.wait()
            return 124


def report_failure(name, result, log, command):
    """Keep console diagnostics bounded; the artifact retains the full output."""
    print(f'{name} failed (exit {result}); full log: {log}', file=sys.stderr)
    print('Command: ' + shlex.join(command), file=sys.stderr)
    with log.open('rb') as output:
        output.seek(0, os.SEEK_END)
        output.seek(max(0, output.tell() - 4096))
        tail = output.read(4096).decode('utf-8', errors='replace')
    for line in tail.splitlines()[-12:]:
        print(line[:240], file=sys.stderr)


def execute(plan, kind, build, artifacts, runner_args):
    artifacts.mkdir(parents=True, exist_ok=True)
    records = []
    if kind == 'functional' and plan['functional']:
        command = [sys.executable, str(build / 'test/functional/test_runner.py'), *plan['functional'], *runner_args]
        timeout = plan['budgets']['functional']
        log = artifacts / 'functional.log'
        result = run_bounded(command, timeout, log)
        records.append({'command': command, 'timeout': timeout, 'exit': result, 'log': log.name})
        if result != 0:
            report_failure('Roots regression functional', result, log, command)
    elif kind == 'fuzz':
        for target in plan['fuzz']:
            # Native libFuzzer flags bound generation by time, not input counts.
            budget = plan['budgets']['fuzz']
            command = [str(build / 'bin/fuzz'), f'-max_total_time={budget}', '-timeout=5', '-seed=1',
                       '-max_len=4096', '-rss_limit_mb=2048', '-artifact_prefix=' + str(artifacts / (target + '-'))]
            log = artifacts / (target + '.log')
            result = run_bounded(command, budget + 10, log, os.environ | {'FUZZ': target})
            records.append({'target': target, 'command': command, 'timeout': budget + 10, 'exit': result, 'log': log.name})
            if result != 0:
                report_failure(f'Roots regression {target}', result, log, command)
    (artifacts / (kind + '-results.json')).write_text(json.dumps({'selection': plan, 'results': records}, indent=2) + '\n')
    return 1 if any(record['exit'] != 0 for record in records) else 0


def collect(build, scratch, artifacts, source=None):
    """Copy diagnostic text only; never upload wallet/config/database files."""
    artifacts.mkdir(parents=True, exist_ok=True)
    records = []
    for label, root in [('build', build), ('scratch', scratch), ('source', source)]:
        if root is None or not root.is_dir():
            continue
        paths = root.glob('*') if label == 'source' else root.rglob('*')
        for path in sorted(paths):
            if path.is_symlink() or not path.is_file() or any(parent.is_symlink() for parent in path.parents):
                continue
            relative = path.relative_to(root)
            if artifacts.resolve() in path.resolve().parents or (path.suffix == '.log' and path.stem.isdecimal()):
                continue
            # CTest/CMake logs and test-framework/node logs are diagnostic
            # outputs. Numeric LevelDB .log journals are deliberately excluded.
            allowed = (path.name in {'debug.log', 'test_framework.log', 'stdout', 'stderr', 'test_results.xml'} or
                       (relative.parts[0] == 'Testing' and path.suffix in {'.log', '.xml'}) or
                       (relative.parts[0] == 'CMakeFiles' and path.name in {'CMakeConfigureLog.yaml', 'CMakeError.log', 'CMakeOutput.log'}) or
                       path.name.startswith(('crash-', 'timeout-', 'oom-', 'leak-')))
            if label == 'source':
                allowed = path.name.startswith(('crash-', 'timeout-', 'oom-', 'leak-'))
            if not allowed:
                continue
            destination = artifacts / label / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, destination)
            records.append(str(destination.relative_to(artifacts)))
    (artifacts / 'diagnostics.json').write_text(json.dumps({'files': records}, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=['functional', 'fuzz', 'plan', 'command', 'collect'])
    parser.add_argument('--cadence', choices=BUDGETS, default='fast')
    parser.add_argument('--files-json', default=os.getenv('ROOTS_CHANGED_FILES_JSON', 'null'))
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--artifacts', type=Path)
    parser.add_argument('--scratch-dir', type=Path)
    parser.add_argument('--source-dir', type=Path)
    parser.add_argument('--timeout', type=int, default=19800)
    args, runner_args = parser.parse_known_args()
    if runner_args and runner_args[0] == '--':
        runner_args = runner_args[1:]
    if args.kind in ('command', 'collect'):
        if not args.artifacts:
            parser.error('diagnostics require --artifacts')
        args.artifacts.mkdir(parents=True, exist_ok=True)
        if args.kind == 'collect':
            if not args.build_dir or not args.scratch_dir:
                parser.error('collection requires --build-dir and --scratch-dir')
            collect(args.build_dir, args.scratch_dir, args.artifacts, args.source_dir)
            return 0
        if not runner_args or args.timeout <= 0:
            parser.error('command requires positive --timeout and argv after --')
        log = args.artifacts / 'ci.log'
        result = run_bounded(runner_args, args.timeout, log)
        (args.artifacts / 'ci-results.json').write_text(json.dumps({
            'command': runner_args, 'timeout': args.timeout, 'exit': result, 'log': log.name}, indent=2) + '\n')
        if result:
            report_failure('CI command', result, log, runner_args)
        else:
            print(f'CI script passed; full log: {log}')
        return result if result >= 0 else 1
    try:
        files = json.loads(args.files_json)
    except (ValueError, TypeError):
        files = None
    plan = selection(files, args.cadence)
    if args.kind == 'plan':
        print(json.dumps(plan, sort_keys=True))
        return 0
    if not args.build_dir or not args.artifacts:
        parser.error('execution requires --build-dir and --artifacts')
    return execute(plan, args.kind, args.build_dir.resolve(), args.artifacts.resolve(), runner_args)


if __name__ == '__main__':
    sys.exit(main())
