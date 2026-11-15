#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[2]


def lint_run_body():
    workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
    job = re.search(r"(?ms)^  lint:\n(.*?)(?=^  [a-z0-9_-]+:\n|\Z)", workflow)
    if job is None:
        raise AssertionError("Missing lint job")
    step = re.search(r"- name: Run lint\n        run: \|\n((?:          [^\n]*\n)+)", job.group(1))
    if step is None:
        raise AssertionError("Missing Run lint body")
    return textwrap.dedent(step.group(1))


class LintWorkflowTest(unittest.TestCase):
    def run_lint(self, *, cache_args="", runner_exit=0):
        with tempfile.TemporaryDirectory(prefix="lint-workflow-") as directory:
            scratch = Path(directory)
            docker = scratch / "docker"
            docker.write_text(
                f"#!{sys.executable}\n"
                "import json, os, sys\n"
                "with open(os.environ['LINT_TEST_COMMANDS'], 'a') as output:\n"
                "    output.write(json.dumps(sys.argv[1:]) + '\\n')\n"
                "if sys.argv[1:3] == ['buildx', 'build']:\n"
                "    sys.exit(0)\n"
                "if sys.argv[1:2] == ['run']:\n"
                "    sys.exit(int(os.environ['LINT_TEST_RUNNER_EXIT']) if './ci/lint/06_script.sh' in sys.argv else 0)\n"
                "sys.exit('Unexpected Docker invocation')\n",
                encoding="utf-8",
            )
            docker.chmod(0o755)
            commands = scratch / "commands.jsonl"
            env = dict(os.environ, PATH=f"{scratch}{os.pathsep}{os.environ['PATH']}",
                       GITHUB_ACTIONS="true", GITHUB_EVENT_NAME="pull_request",
                       GITHUB_REPOSITORY="LuganoPlanB/bitcoin-roots", CONTAINER_NAME="bitcoin-linter",
                       DOCKER_BUILD_CACHE_ARG=cache_args, LINT_TEST_COMMANDS=str(commands),
                       LINT_TEST_RUNNER_EXIT=str(runner_exit))
            result = subprocess.run(["bash", "-c", lint_run_body()], cwd=ROOT, env=env,
                                    capture_output=True, text=True, timeout=30)
            calls = [json.loads(line) for line in commands.read_text(encoding="utf-8").splitlines()]
            return result, calls

    def assert_runner_invocation(self, calls):
        self.assertEqual(len(calls), 2)
        build, run = calls
        self.assertEqual(build[:2], ["buildx", "build"])
        self.assertIn("--tag=bitcoin-linter", build)
        self.assertIn(f"--file={ROOT}/ci/lint_imagefile", build)
        self.assertEqual(run[0], "run")
        self.assertIn("--rm", run)
        self.assertIn("--env", run)
        self.assertEqual(run[run.index("--env") + 1], "LINT_CI_IS_PR=1")
        self.assertIn(f"--volume={ROOT}:/bitcoin", run)
        self.assertNotIn("-it", run)
        self.assertEqual(run[-2:], ["bitcoin-linter", "./ci/lint/06_script.sh"])

    def test_workflow_runs_lints_with_pr_flag_and_build_cache(self):
        result, calls = self.run_lint(cache_args="--cache-from type=gha,scope=bitcoin-linter --load")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_runner_invocation(calls)
        build = calls[0]
        self.assertEqual(build[build.index("--cache-from") + 1], "type=gha,scope=bitcoin-linter")
        self.assertIn("--load", build)

    def test_workflow_runs_lints_without_build_cache(self):
        result, calls = self.run_lint()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_runner_invocation(calls)
        self.assertNotIn("--cache-from", calls[0])

    def test_workflow_propagates_lint_runner_failure(self):
        result, calls = self.run_lint(runner_exit=42)
        self.assert_runner_invocation(calls)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("42", result.stderr)


if __name__ == "__main__":
    unittest.main()
