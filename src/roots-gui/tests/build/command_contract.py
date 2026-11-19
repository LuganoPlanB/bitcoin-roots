# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Linked executable metadata checks; this command does not initialize the native host."""

import json
import subprocess
import sys

binary = sys.argv[1]
executed = 0
version = subprocess.run(
    [binary, "--version"], capture_output=True, text=True, check=True
)
assert version.stdout.strip() == "roots-gui 32.1-roots.1"
executed += 1
result = subprocess.run(
    [binary, "--build-contract"], capture_output=True, text=True, check=True
)
info = json.loads(result.stdout)
for key, expected in {
    "stage": "native-bootstrap",
    "wally": 0x10601,
    "curl": 0x081700,
    "sqlite": 3054001,
    "imgui": 19297,
    "imkit": "4.1.0",
    "native_initialized": False,
    "backend_symbols_linked": True,
}.items():
    assert info[key] == expected, (key, info)
    executed += 1
assert "3.5.1" in info["glfw"]
executed += 1
invalid = subprocess.run([binary, "--unknown"], capture_output=True, text=True)
assert invalid.returncode == 2 and "usage:" in invalid.stderr
executed += 1
print(
    f"executed={executed} passed={executed} failed=0 skipped=0; linked native-bootstrap metadata only"
)
