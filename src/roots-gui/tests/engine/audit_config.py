# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Check actual compiler/link commands and symbols for test/production isolation."""

import argparse
import json
from pathlib import Path
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--build", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
b = a.build.resolve()
checks = []


def check(ok, label):
    checks.append(dict(id=label, passed=bool(ok)))


commands = json.loads((b / "compile_commands.json").read_text())
tested = production = 0
for c in commands:
    argv = shlex.split(c["command"])
    obj = argv[argv.index("-o") + 1]
    if (
        "tests/engine/" in obj
        and "/roots_gui_models.dir/" not in obj
        and "/roots_gui_preference_models.dir/" not in obj
    ):
        tested += 1
        defs = [x for x in argv if x.startswith("-DIMGUI_USER_CONFIG=")]
        check(
            len(defs) == 1
            and Path(defs[0].split("=", 1)[1].strip('"')).resolve()
            == Path(__file__).with_name("config.h").resolve(),
            obj + " shared test configuration",
        )
        check("-UNDEBUG" in argv, obj + " assertions enabled")
    elif (
        "/roots_gui_" in obj
        or obj.startswith("CMakeFiles/roots_gui_")
        or "/roots-gui.dir/" in obj
        or obj.startswith("CMakeFiles/roots-gui.dir/")
    ):
        production += 1
        check(
            not any(
                "IMGUI_USER_CONFIG" in x or "IMGUI_ENABLE_TEST_ENGINE" in x
                for x in argv
            ),
            obj + " no test config",
        )
check(tested >= 40, "test compile coverage")
check(production >= 25, "production compile coverage")
raw = subprocess.check_output(
    ["ninja", "-C", str(b), "-t", "commands", "roots-gui"], text=True
)
check(
    "roots_gui_test_" not in raw
    and "/third_party/test_inputs/" not in raw
    and "/inputs/test-" not in raw,
    "production command closure excludes test inputs",
)
symbols = subprocess.check_output(["nm", "--demangle", str(b / "roots-gui")], text=True)
check("ImGuiTestEngine" not in symbols, "production has no engine or core hook symbols")
markers = (b"native-request", b"copy-sensitive")
check(
    all(marker not in (b / "roots-gui").read_bytes() for marker in markers),
    "production excludes native-input fixture request controls",
)
check(
    all(marker in (b / "roots_gui_a11y_fixture").read_bytes() for marker in markers),
    "native-input isolation positive control identifies actual fixture binary",
)
symbols = subprocess.check_output(
    ["nm", "--demangle", str(b / "tests/engine/roots_gui_engine_tests")], text=True
)
check(
    "ImGuiTestEngineHook_ItemInfo" in symbols, "test binary contains linked core hooks"
)
check(
    "roots::gui::ui::DrawPreferences" in symbols
    and "roots::gui::ui::DrawBootstrap" in symbols,
    "test binary uses production Draw functions",
)
failed = sum(not x["passed"] for x in checks)
result = dict(
    executed=len(checks),
    passed=len(checks) - failed,
    failed=failed,
    skipped=0,
    testCompileUnits=tested,
    productionCompileUnits=production,
    checks=checks,
)
a.output.write_text(json.dumps(result, indent=2) + "\n")
print(
    json.dumps(
        {
            k: result[k]
            for k in [
                "executed",
                "passed",
                "failed",
                "skipped",
                "testCompileUnits",
                "productionCompileUnits",
            ]
        }
    )
)
raise SystemExit(bool(failed))
