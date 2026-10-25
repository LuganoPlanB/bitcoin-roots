#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Contract fixtures for benchmark extraction, pairing and failure retention."""

import json
from pathlib import Path
import sys
import tempfile
import unittest

from cache_bench import execute, instrument, parse_metric, summarize, write_json


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
                     metric_line(occupied=9), metric_line(stored_calls=5)):
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
