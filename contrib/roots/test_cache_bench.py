#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Contract fixtures for benchmark extraction, pairing and failure retention."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import cache_bench as bench
from cache_bench import execute, instrument, parse_metric, summarize, write_json
if sys.platform.startswith('linux'):
    from cache_bench_workload import CacheWorkload, MemorySampler, check_reconstruction, process_stats


def metric_line(**overrides):
    values = {"block": "a" * 64, "insert_calls": 4, "insert_cpu_ns": 12, "insert_wall_ns": 16,
              "init_cpu_ns": 20, "init_wall_ns": 24, "slots": 8, "slot_bytes": 384,
              "occupied": 4, "charged_dynamic_bytes": 512, "unique_dynamic_bytes": 384,
              "distinct": 3, "external_refs": 2, "shared_objects": 1, "extra_hits": 3, "available": 4,
              "stored_calls": 3, "stored_cpu_ns": 9, "stored_wall_ns": 12}
    values.update(overrides)
    return "timestamp [info] ISSUE56 " + " ".join(f"{key}={value}" for key, value in values.items())


class CacheBenchmarkTests(unittest.TestCase):
    def test_exact_metric_units_and_shared_references(self):
        metric = parse_metric(metric_line())
        self.assertEqual(metric["insert_cpu_ns"], 12)
        self.assertEqual(metric["charged_dynamic_bytes"], 512)
        self.assertEqual(metric["unique_dynamic_bytes"], 384)
        self.assertEqual(metric["external_refs"], 2)
        self.assertEqual(metric["extra_hits"], 3)
        self.assertEqual(metric["stored_calls"], 3)
        self.assertIsNone(parse_metric("ordinary debug message"))

    def test_reject_incomplete_duplicate_and_negative_metrics(self):
        for line in (metric_line().replace(" slots=8", ""), metric_line() + " slots=8",
                     metric_line(insert_cpu_ns=-1), metric_line(init_wall_ns="unknown"),
                     metric_line(occupied=9), metric_line(stored_calls=5), metric_line(block="z" * 64),
                     metric_line(insert_cpu_ns=2**64), metric_line(stored_wall_ns=17),
                     metric_line(extra_hits=5), metric_line(init_cpu_ns="+20")):
            with self.subTest(line=line):
                with self.assertRaises(ValueError):
                    parse_metric(line)

    def test_capture_nonzero_and_timeout(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "failure.log"
            record = execute([sys.executable, "-c", "print('known failure'); raise SystemExit(7)"],
                             cwd=directory, log=log, timeout=10)
            self.assertEqual(record["exit"], 7)
            self.assertIn("known failure", log.read_text())
            self.assertFalse(record["timed_out"])
            record = execute([sys.executable, "-c", "import time; time.sleep(30)"],
                             cwd=directory, log=log, timeout=.05)
            self.assertTrue(record["timed_out"])
            self.assertNotEqual(record["exit"], 0)

    def test_summary_preserves_failure_and_detects_unpaired_fixtures(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runs = []
            for name, cpu, fixture in (("roots-default", 10, "one"), ("core-default", 20, "two")):
                result = root / (name + ".json")
                write_json(result, [{"case": "reject", "fixture_sha256": fixture, "init_cpu_ns": cpu}])
                runs.append({"variant": name, "repetition": 0, "warmup": False,
                             "exit": 0, "result": str(result), "log": "unused"})
            runs.append({"variant": "roots-disabled", "repetition": 0, "exit": 9,
                         "result": str(root / "missing.json"), "log": "failure.log"})
            write_json(root / "runs.json", runs)
            summarize(root)
            report = json.loads((root / "summary.json").read_text())
            self.assertEqual(len(report["failures"]), 1)
            self.assertEqual(len(report["fixture_mismatches"]), 1)
            stats = report["statistics"]["roots-default/reject"]["init_cpu_ns"]
            self.assertEqual(stats["n"], 1)
            self.assertEqual(stats["median"], 10)
            self.assertIsNone(stats["p95"])
            self.assertIsNone(stats["sample_stdev"])

    @unittest.skipUnless(os.name == 'posix', 'Owned process groups require POSIX')
    def test_cooperative_zero_exit_timeout_cannot_contribute_complete_sample(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sample = root / 'row.json'
            row = {'case': 'probe', 'fixture_sha256': 'a' * 64, 'block_accepted': True, 'init_cpu_ns': 20}
            script = ("import json,signal,time;from pathlib import Path;"
                      "signal.signal(signal.SIGTERM,lambda *_:exit(0));"
                      f"Path({str(sample)!r}).write_text({json.dumps([row])!r});"
                      "print('ready',flush=True);time.sleep(30)")
            record = execute([sys.executable, '-c', script], cwd=root, log=root / 'timeout.log', timeout=.3)
            self.assertEqual(record['exit'], 0)
            self.assertTrue(record['timed_out'])
            self.assertIn('ready', (root / 'timeout.log').read_text())
            self.assertEqual(json.loads(sample.read_text()), [row])
            record.update(variant='roots-default', repetition=0, warmup=False, result=str(sample))
            write_json(root / 'runs.json', [record])
            write_json(root / 'design.json', {'warmup': 0, 'repetitions': 1,
                'variants': {'roots-default': []}, 'cases': ['probe']})
            before = (root / 'runs.json').read_bytes()
            summary = summarize(root)
            self.assertEqual(summary['statistics'], {})
            self.assertEqual(summary['failures'][0]['reason'], 'Timed out')
            self.assertEqual((root / 'runs.json').read_bytes(), before)

    def test_prepare_rejects_zero_exit_timeout_before_next_build_step(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = self.source_fixture(directory, 'v32.1', 'core')
            source.rename(root / 'core-source')
            archive = root / 'core.tar'
            with tarfile.open(archive, 'w') as bundle:
                for name in ['CMakeLists.txt', *bench.INSTRUMENTED[:2]]:
                    bundle.add(root / 'core-source' / name, arcname=name)
            core = bench.PROFILES['v32.1']['core']
            write_json(root / 'builds.json', {'roots_commit': 'fixture', 'core_commit': core, 'profile': 'v32.1',
                'builds': {'core': {'archive_sha256': bench.digest(archive)}}})
            record = {'exit': 0, 'timed_out': True, 'log': 'cooperative-timeout.log'}
            args = SimpleNamespace(work=root, roots_ref='fixture', profile='v32.1', resume=True, timeout=.3)
            with patch.object(bench, 'check_environment'), patch.object(bench, 'output', return_value='fixture compiler'), \
                    patch.object(bench, 'checked_sources', return_value=('v32.1', core, 'fixture')), \
                    patch.object(bench, 'execute', return_value=record) as invoked:
                with self.assertRaisesRegex(RuntimeError, 'Build failed'):
                    bench.prepare(args)
            self.assertEqual(invoked.call_count, 1)
            retained = json.loads((root / 'builds.json').read_text())
            self.assertEqual(retained['builds']['core']['commands'], [record])
            self.assertNotIn('binary', retained['builds']['core'])
            self.assertNotIn('roots', retained['builds'])

    def test_run_rejects_zero_exit_timeout_even_if_summary_is_empty(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            builds = {'builds': {'roots': {'binary': '/fixture/bitcoind', 'binary_sha256': 'fixture'}}}
            write_json(root / 'builds.json', builds)
            args = SimpleNamespace(work=root, configfile=root / 'config.ini', variants='roots-default', smoke=True,
                repetitions=1, warmup=0, count=4, timeout=.3, name='probe', seed=1)
            def execute_fixture(command, *, log, **_kwargs):
                log.write_text('Bitcoin Roots version v32.1\n')
                return {'exit': 0, 'timed_out': log.name != 'roots-version.log', 'log': str(log)}
            with patch.object(bench, 'check_environment'), patch.object(bench, 'validate_builds', return_value='v32.1'), \
                    patch.object(bench, 'framework_inputs', return_value=(root, {})), \
                    patch.object(bench, 'execute', side_effect=execute_fixture), \
                    patch.object(bench, 'summarize', return_value={'failures': [], 'fixture_mismatches': []}):
                with self.assertRaisesRegex(RuntimeError, 'experiment incomplete'):
                    bench.run(args)
            records = json.loads((root / 'probe/runs.json').read_text())
            self.assertEqual(len(records), 1)
            self.assertTrue(records[0]['timed_out'])
            self.assertEqual(records[0]['exit'], 0)

    def test_supported_exact_source_profiles(self):
        for name, profile in bench.PROFILES.items():
            metadata = "\n".join(f"set(CLIENT_VERSION_{key} {value})" for key, value in
                zip(("MAJOR", "MINOR", "BUILD", "RC"), profile["version"]))
            self.assertEqual(bench.select_profile(metadata), name)
            with self.assertRaises(ValueError):
                bench.select_profile(metadata.replace("BUILD 0", "BUILD 1"))
            with self.assertRaises(ValueError):
                bench.select_profile(metadata.replace("RC 0", "RC 1"))
        with self.assertRaises(ValueError):
            bench.select_profile((bench.REPO / 'CMakeLists.txt').read_text(), "v99.1")

    def source_fixture(self, directory, profile, kind):
        references = {"v31.1": {"core": bench.CORE, "roots": "e93a8cca047c12ce9cab97181dd30705ed0e7e83"},
                      "v32.1": {"core": bench.PROFILES["v32.1"]["core"], "roots": "HEAD" if bench.select_profile(
                          (bench.REPO / 'CMakeLists.txt').read_text()) == 'v32.1' else 'e9790ef6a9ecc01c71e022e124edb6479c338551'}}
        source = Path(directory) / (profile + '-' + kind)
        (source / 'src').mkdir(parents=True)
        for name in ['CMakeLists.txt', *bench.INSTRUMENTED[:2]]:
            result = subprocess.run(['git', 'show', references[profile][kind] + ':' + name], cwd=bench.REPO, capture_output=True)
            if result.returncode:
                self.skipTest('Pinned ' + profile + ' source unavailable; no retrieval/installation')
            (source / name).write_bytes(result.stdout)
        return source

    def check_profile_patch(self, profile):
        with tempfile.TemporaryDirectory() as directory:
            for kind in ['core', 'roots']:
                source = self.source_fixture(directory, profile, kind)
                instrument(source, Path(directory) / (kind + '.patch'), profile, kind)
                self.assertIn('issue56::Insertion', (source / bench.INSTRUMENTED[0]).read_text())
                self.assertIn('issue56::Reconstruction', (source / bench.INSTRUMENTED[1]).read_text())
                self.assertEqual((source / bench.INSTRUMENTED[2]).read_bytes(), (bench.HERE / 'cache_bench_metrics.h').read_bytes())

    def test_v31_instrumentation_pair(self):
        self.check_profile_patch('v31.1')

    def test_v32_instrumentation_pair(self):
        self.check_profile_patch('v32.1')

    def test_changed_backend_is_rejected_without_partial_patch(self):
        with tempfile.TemporaryDirectory() as directory:
            source = self.source_fixture(directory, 'v32.1', 'roots')
            path = source / bench.INSTRUMENTED[1]
            path.write_text(path.read_text().replace('    LogDebug(BCLog::CMPCTBLOCK, "Initializing',
                                                    '    /* unreviewed backend */ LogDebug(BCLog::CMPCTBLOCK, "Initializing'))
            before = {name: (source / name).read_bytes() for name in bench.INSTRUMENTED[:2]}
            with self.assertRaisesRegex(ValueError, 'function body'):
                instrument(source, Path(directory) / 'patch', 'v32.1', 'roots')
            self.assertEqual(before, {name: (source / name).read_bytes() for name in bench.INSTRUMENTED[:2]})
            self.assertFalse((source / bench.INSTRUMENTED[2]).exists())
        with self.assertRaisesRegex(ValueError, 'working trees'):
            instrument(bench.REPO, Path('unused.patch'))

    def test_export_and_config_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            source.mkdir()
            (source / 'file').write_text('known source')
            archive = root / 'source.tar'
            with tarfile.open(archive, 'w') as bundle:
                bundle.add(source / 'file', arcname='file')
            bench.check_export(source, archive)
            (source / 'file').write_text('changed')
            with self.assertRaisesRegex(ValueError, 'source changed'):
                bench.check_export(source, archive)
            cache = root / 'CMakeCache.txt'
            cache.write_text('\n' + '\n'.join(flag.removeprefix('-D').replace('=', ':STRING=', 1) for flag in bench.BUILD_FLAGS))
            controls = bench.build_controls(cache)
            self.assertEqual(controls['CMAKE_BUILD_TYPE'], 'RelWithDebInfo')
            cache.write_text(cache.read_text().replace('ENABLE_IPC:STRING=OFF', 'ENABLE_IPC:STRING=ON'))
            with self.assertRaisesRegex(ValueError, 'profile mismatch'):
                bench.build_controls(cache)

    def test_harness_change_and_partial_build_cannot_run(self):
        profile = bench.PROFILES['v32.1']
        base = {'roots_commit': 'fixture', 'core_commit': profile['core'], 'profile': 'v32.1',
                'builds': {'core': {}, 'roots': {}}, 'harness': {}}
        with patch.object(bench, 'checked_sources', return_value=('v32.1', profile['core'], 'fixture')):
            with self.assertRaisesRegex(ValueError, 'Harness'):
                bench.validate_builds(base)
            base['harness'] = bench.harness_hashes()
            for kind in base['builds']:
                base['builds'][kind] = {'commit': profile['core'] if kind == 'core' else 'fixture',
                    'flags': bench.BUILD_FLAGS, 'commands': [{'exit': 7, 'timed_out': False}]}
            with self.assertRaisesRegex(ValueError, 'Incomplete controlled build'):
                bench.validate_builds(base)
            for entry in base['builds'].values():
                entry['commands'] = [{'exit': 0, 'timed_out': False}, {'exit': 0, 'timed_out': True}]
            with self.assertRaisesRegex(ValueError, 'Incomplete controlled build'):
                bench.validate_builds(base)

    def test_framework_profile_must_match(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'CMakeLists.txt').write_text((bench.REPO / 'CMakeLists.txt').read_text())
            framework = root / 'test/functional/test_framework'
            framework.mkdir(parents=True)
            (framework / 'test_framework.py').write_text('# fixture\n')
            config = root / 'config.ini'
            config.write_text('[environment]\nSRCDIR=' + str(root) + '\n')
            current = bench.select_profile((root / 'CMakeLists.txt').read_text())
            other = 'v31.1' if current == 'v32.1' else 'v32.1'
            self.assertEqual(bench.framework_inputs(config, current)[0], root)
            with self.assertRaises(ValueError):
                bench.framework_inputs(config, other)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Experiment environment is Linux-only')
    def test_allocator_and_platform_fail_before_experiment(self):
        with patch.dict(os.environ, {'LD_PRELOAD': 'fixture-not-a-library'}):
            with self.assertRaisesRegex(ValueError, 'LD_PRELOAD'):
                bench.check_environment()
        with patch.object(bench.platform, 'system', return_value='Darwin'):
            with self.assertRaisesRegex(ValueError, 'Linux'):
                bench.check_environment()

    @unittest.skipUnless(os.name == 'posix', 'Owned process groups require POSIX')
    def test_timeout_finishes_child_even_when_leader_settles(self):
        with tempfile.TemporaryDirectory() as directory:
            ready, escaped = Path(directory) / 'ready', Path(directory) / 'escaped'
            child = ("import signal,time;from pathlib import Path;signal.signal(signal.SIGTERM,signal.SIG_IGN);"
                     f"Path({str(ready)!r}).write_text('ready');time.sleep(.7);Path({str(escaped)!r}).write_text('escaped')")
            parent = ("import subprocess,sys,time;from pathlib import Path;"
                      f"subprocess.Popen([sys.executable,'-c',{child!r}]);"
                      f"\nwhile not Path({str(ready)!r}).exists(): time.sleep(.001)\ntime.sleep(30)")
            with patch.object(bench, 'STOP_GRACE_SECONDS', .05):
                record = execute([sys.executable, '-c', parent], cwd=directory, log=Path(directory) / 'timeout.log', timeout=.2)
            self.assertTrue(ready.exists())
            self.assertTrue(record['timed_out'])
            time.sleep(.8)
            self.assertFalse(escaped.exists())

    def test_aggregate_rejects_duplicate_partial_and_unaccepted_samples(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sample = root / 'row.json'
            row = {'case': 'probe', 'fixture_sha256': 'a' * 64, 'init_cpu_ns': 20, 'block_accepted': True}
            record = {'variant': 'roots-default', 'repetition': 0, 'warmup': False,
                      'exit': 0, 'result': str(sample), 'log': 'fixture.log'}
            write_json(root / 'design.json', {'warmup': 0, 'repetitions': 1, 'variants': {'roots-default': []}, 'cases': ['probe']})
            write_json(sample, [row])
            write_json(root / 'runs.json', [record, record])
            result = summarize(root)
            self.assertEqual(result['failures'][0]['reason'], 'Duplicate run')
            self.assertEqual(result['statistics']['roots-default/probe']['init_cpu_ns']['n'], 1)
            for data in ['[{', json.dumps([{**row, 'block_accepted': False}]), json.dumps([row, row]),
                         json.dumps([{**row, 'init_cpu_ns': -1}]), json.dumps([{**row, 'init_cpu_ns': float('nan')}])]:
                with self.subTest(data=data):
                    sample.write_text(data)
                    write_json(root / 'runs.json', [record])
                    before = (root / 'runs.json').read_bytes()
                    result = summarize(root)
                    self.assertTrue(result['failures'])
                    self.assertEqual(result['statistics'], {})
                    self.assertEqual((root / 'runs.json').read_bytes(), before)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Workload framework profile is Linux-only')
    def test_capacity_controls_do_not_assume_historical_default(self):
        check_reconstruction({'available': 100, 'occupied': 100}, 128, list(range(28)),
            root_reject=True, overlap=1, flags=[], offered_count=128)
        check_reconstruction({'available': 0, 'occupied': 0}, 4, list(range(4)),
            root_reject=True, overlap=1, flags=['-blockreconstructionextratxn=0'], offered_count=4)
        with self.assertRaises(AssertionError):
            check_reconstruction({'available': 3, 'occupied': 4}, 4, [0],
                root_reject=True, overlap=1, flags=[], offered_count=4)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Workload framework profile is Linux-only')
    def test_workload_loopback_composes_with_framework_and_rejects_extra_flags(self):
        workload = object.__new__(CacheWorkload)
        environment = {'ROOTS_BENCH_KIND': 'roots', 'ROOTS_BENCH_FLAGS': '[]', 'ROOTS_BENCH_COUNT': '4',
            'ROOTS_BENCH_PROFILE': bench.select_profile((bench.REPO / 'CMakeLists.txt').read_text())}
        with patch.dict(os.environ, environment):
            workload.set_test_params()
            self.assertTrue(workload.bind_to_localhost_only)
            self.assertFalse(any(flag.startswith('-bind=') for flag in workload.extra_args[0]))
            self.assertIn('-dnsseed=0', workload.extra_args[0])
            with patch.dict(os.environ, {'ROOTS_BENCH_FLAGS': '["-connect=example.invalid"]'}):
                with self.assertRaisesRegex(ValueError, 'declared bounded'):
                    workload.set_test_params()

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Actual /proc sampling is Linux-only')
    def test_process_sampler_units_and_cleanup(self):
        stats = process_stats(os.getpid())
        self.assertGreater(stats['rss_bytes'], 0)
        self.assertGreaterEqual(stats['lifetime_hwm_bytes'], stats['rss_bytes'])
        with MemorySampler(os.getpid()) as sampler:
            time.sleep(.025)
        self.assertFalse(sampler.thread.is_alive())
        self.assertTrue(sampler.samples)
        self.assertEqual(sampler.errors, [])

    def test_patch_fails_closed_on_unknown_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            (root / "src/net_processing.cpp").write_text("// unsupported source\n")
            with self.assertRaisesRegex(ValueError, "anchor mismatch"):
                instrument(root, root / "patch")

    def test_missing_runs_cannot_disappear_from_summary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_json(root / "runs.json", [])
            write_json(root / "design.json", {"warmup": 1, "repetitions": 5,
                                               "variants": {"roots-default": []}, "cases": ["rejected"]})
            result = summarize(root)
            self.assertEqual(len(result["failures"]), 6)
            self.assertTrue(all(failure["reason"] == "Run not executed" for failure in result["failures"]))

    def test_dispersion_uses_every_measured_sample_and_excludes_warmup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runs = []
            for repetition, value in enumerate((999, 1, 2, 3, 4, 100)):
                result = root / f"sample-{repetition}.json"
                write_json(result, [{"case": "probe", "fixture_sha256": "same", "init_cpu_ns": value}])
                runs.append({"variant": "roots-default", "repetition": repetition - 1,
                             "warmup": repetition == 0, "exit": 0, "result": str(result), "log": "unused"})
            write_json(root / "runs.json", runs)
            summary = summarize(root)
            stats = summary["statistics"]["roots-default/probe"]["init_cpu_ns"]
            self.assertEqual((stats["n"], stats["median"], stats["min"], stats["max"]), (5, 3, 1, 100))
            self.assertGreater(stats["sample_stdev"], 40)
            self.assertIsNone(stats["p95"])


if __name__ == "__main__":
    unittest.main()
