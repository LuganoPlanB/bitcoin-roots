#!/usr/bin/env python3
"""Regression tests for strict, retried APT index refreshes in Linux CI."""

import os
import pathlib
import re
import subprocess
import tempfile
import unittest


CI_TEST_ROOT = pathlib.Path(__file__).resolve().parent
CI_ROOT = CI_TEST_ROOT.parent
APT_HELPER = CI_TEST_ROOT / "apt.sh"
CI_IMAGEFILE = CI_TEST_ROOT.parent / "test_imagefile"
LINT_IMAGEFILE = CI_ROOT / "lint_imagefile"
LINT_INSTALL = CI_ROOT / "lint" / "01_install.sh"
REFRESH_CALLERS = [
    CI_TEST_ROOT / "01_base_install.sh",
    CI_TEST_ROOT / "03_test_script.sh",
]


class AptRefreshTest(unittest.TestCase):
    def test_refresh_callers_use_the_shared_strict_helper(self):
        helper = APT_HELPER.read_text(encoding="utf-8")
        self.assertIn('APT::Update::Error-Mode=any update', helper)
        self.assertIn('"${retry_command[@]}" apt-get', helper)
        self.assertIn('./ci/test/apt.sh', CI_IMAGEFILE.read_text(encoding="utf-8"))
        self.assertIn('./ci/test/apt.sh', LINT_IMAGEFILE.read_text(encoding="utf-8"))

        for caller in REFRESH_CALLERS:
            with self.subTest(caller=caller):
                contents = caller.read_text(encoding="utf-8")
                self.assertIn('source "$(dirname "${BASH_SOURCE[0]}")/apt.sh"', contents)
                self.assertIn('ci_retry_apt_update', contents)
                self.assertNotIn('apt-get update', contents)
                self.assertNotIn('APT::Update::Error-Mode=any update', contents)

        lint_install = LINT_INSTALL.read_text(encoding="utf-8")
        self.assertIn('source /ci/test/apt.sh', lint_install)
        self.assertIn('ci_retry_apt_update', lint_install)

        refresh_pattern = re.compile(r"\bapt-get\b.*\bupdate\b")
        for script in CI_ROOT.rglob("*.sh"):
            matches = refresh_pattern.findall(script.read_text(encoding="utf-8"))
            with self.subTest(script=script):
                if script == APT_HELPER:
                    self.assertEqual(matches, ['apt-get -o APT::Update::Error-Mode=any update'])
                else:
                    self.assertEqual(matches, [])

    def test_invalid_repository_failure_is_retried_and_propagated(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_path = pathlib.Path(temporary_directory)
            attempts = temporary_path / "attempts"
            retry = temporary_path / "retry"
            apt_get = temporary_path / "apt-get"
            retry.write_text(
                "#!/usr/bin/env bash\n"
                "set -u\n"
                "test \"$1\" = --\n"
                "shift\n"
                "for attempt in 1 2 3; do\n"
                "  \"$@\" || status=$?\n"
                "  test \"${status:-0}\" -eq 0 && exit 0\n"
                "done\n"
                "exit \"$status\"\n",
                encoding="utf-8",
            )
            apt_get.write_text(
                "#!/usr/bin/env bash\n"
                "set -u\n"
                "printf '%s\\n' \"$*\" >> \"$APT_ATTEMPTS\"\n"
                "exit 100\n",
                encoding="utf-8",
            )
            retry.chmod(0o755)
            apt_get.chmod(0o755)

            environment = os.environ | {
                "APT_ATTEMPTS": str(attempts),
                "CI_RETRY_EXE": f"{retry} --",
                "PATH": f"{temporary_path}{os.pathsep}{os.environ['PATH']}",
            }
            result = subprocess.run(
                ["bash", "-c", f'source "{APT_HELPER}"; ci_retry_apt_update'],
                check=False,
                env=environment,
            )

            self.assertEqual(result.returncode, 100)
            self.assertEqual(
                attempts.read_text(encoding="utf-8").splitlines(),
                ['-o APT::Update::Error-Mode=any update'] * 3,
            )


if __name__ == "__main__":
    unittest.main()
