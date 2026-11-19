#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying file COPYING.
"""The standalone GUI's conventions must not disable unrelated lint checks."""

import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


LINT = Path(__file__).resolve().parents[2] / "test/lint"


def load_lint(name):
    spec = importlib.util.spec_from_file_location(name, LINT / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    with mock.patch.object(sys, "path", [str(LINT), *sys.path]):
        spec.loader.exec_module(module)
    return module


class StandaloneLintTest(unittest.TestCase):
    def test_guard_conventions_require_an_actual_guard(self):
        module = load_lint("lint-include-guards")
        cases = (
            ("src/roots-gui/example.h", "#pragma once\n", 0),
            ("src/roots-gui/example.h", "// #pragma once\n", 1),
            ("src/roots-gui/example.h", "#ifndef ROOTS_GUI_EXAMPLE_H\n#define ROOTS_GUI_EXAMPLE_H\n#endif // ROOTS_GUI_EXAMPLE_H\n", 0),
            ("src/core.h", "#pragma once\n", 1),
            ("src/core.h", "#ifndef BITCOIN_CORE_H\n#define BITCOIN_CORE_H\n#endif // BITCOIN_CORE_H\n", 0),
        )
        for name, contents, expected in cases:
            with self.subTest(name=name, contents=contents), tempfile.TemporaryDirectory() as directory:
                header = Path(directory) / "fixture.h"
                header.write_text(contents, encoding="utf-8")
                with mock.patch.object(module, "_get_header_file_lst", return_value=[name]), \
                        mock.patch("builtins.open", return_value=header.open()), \
                        contextlib.redirect_stdout(io.StringIO()), \
                        self.assertRaises(SystemExit) as result:
                    module.main()
                self.assertEqual(result.exception.code, expected)

    def test_gui_locale_exception_does_not_allow_floating_parsers(self):
        module = load_lint("lint-locale-dependence")
        for line, expected in (
            ('src/roots-gui/main.cpp: auto n = std::stod("1.2");', 1),
            ('src/roots-gui/tests/probe.cpp: auto n = std::stof("1.2");', 1),
            ('src/roots-gui/ui/view.cpp: auto n = std::to_string(count);', 0),
            ('src/validation.cpp: auto n = std::to_string(count);', 1),
        ):
            with self.subTest(line=line), \
                    mock.patch.object(module, "find_locale_dependent_function_uses", return_value=[line]), \
                    contextlib.redirect_stdout(io.StringIO()), self.assertRaises(SystemExit) as result:
                module.main()
            self.assertEqual(result.exception.code, expected)


if __name__ == "__main__":
    unittest.main()
