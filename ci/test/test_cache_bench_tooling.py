#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Include the standalone offline cache-tool regressions in maintenance CI."""
import importlib.util
from pathlib import Path
import sys


def load_tests(loader, _tests, _pattern):
    directory = Path(__file__).resolve().parents[2] / 'contrib/roots'
    sys.path.insert(0, str(directory))
    try:
        spec = importlib.util.spec_from_file_location('roots_cache_bench_tests', directory / 'test_cache_bench.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return loader.loadTestsFromModule(module)
    finally:
        sys.path.remove(str(directory))
