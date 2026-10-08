#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]
BUILD_CONFIG = ROOT / "CMakeLists.txt"
ARCHIVE_TOOL = ROOT / "ci/release/archive.py"
RELEASE_WORKFLOW = ROOT / ".github/workflows/release.yml"


class ReleaseVersionTest(unittest.TestCase):
    def configure_release_version(self, tag=None):
        build_config = BUILD_CONFIG.read_text(encoding="utf-8")
        version_start = re.search(r'^set\(CLIENT_VERSION_STRING ', build_config, re.MULTILINE)
        self.assertIsNotNone(version_start, "Release version configuration must be executable CMake")
        version_block = build_config[version_start.start():].split('#=============================', 1)[0]
        with tempfile.TemporaryDirectory(prefix="roots-release-version-") as directory:
            source = Path(directory)
            metadata = "\n".join(
                re.search(rf"set\({name} [^)]+\)", build_config).group(0)
                for name in ("CLIENT_VERSION_MAJOR", "CLIENT_VERSION_MINOR", "CLIENT_VERSION_RC")
            )
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.22)\n"
                "project(ReleaseVersionTest LANGUAGES NONE)\n"
                + metadata + "\n" + version_block
                + f'configure_file("{ROOT / "cmake/bitcoin-build-config.h.in"}" '
                '"${CMAKE_BINARY_DIR}/bitcoin-build-config.h" @ONLY)\n',
                encoding="utf-8",
            )
            command = ["cmake", "-S", str(source), "-B", str(source / "build")]
            if tag is not None:
                command.append(f"-DCLIENT_VERSION_TAG={tag}")
            result = subprocess.run(command, capture_output=True, text=True)
            header = source / "build/bitcoin-build-config.h"
            return result, header.read_text(encoding="utf-8") if header.exists() else ""

    def test_release_tag_sets_compiled_version_without_git_metadata(self):
        for tag in ("v30.3-roots.2", "v30.3-roots.4"):
            with self.subTest(tag=tag):
                result, header = self.configure_release_version(tag)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f'#define CLIENT_VERSION_STRING "{tag[1:]}"', header)
                self.assertNotIn("Manually-specified variables were not used", result.stderr)

    def test_release_tag_rejects_wrong_base_and_invalid_roots_version(self):
        for tag in ("v30.0-roots.4", "v30x3-roots.4", "v30.3-roots.0", "v30.3-knots.4", ""):
            with self.subTest(tag=tag):
                result, _ = self.configure_release_version(tag)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("CLIENT_VERSION_TAG must be a Roots release tag", result.stderr)

    def test_untagged_build_keeps_default_roots_version(self):
        result, header = self.configure_release_version()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('#define CLIENT_VERSION_STRING "30.3-roots.1"', header)

    def test_version_and_release_tag_name_agree(self):
        build_config = BUILD_CONFIG.read_text(encoding="utf-8")
        self.assertIn("set(CLIENT_VERSION_MAJOR 30)", build_config)
        self.assertIn("set(CLIENT_VERSION_MINOR 3)", build_config)
        self.assertIn('string(APPEND CLIENT_VERSION_STRING "-roots.1")', build_config)
        result = subprocess.run(
            ["python3", ARCHIVE_TOOL, "root-name", "--tag", "v30.3-roots.1"],
            capture_output=True, text=True, check=True,
        )
        self.assertEqual(result.stdout.strip(), "bitcoin-roots-30.3-roots.1")

    def test_macos_deployment_verifies_the_branded_archive(self):
        script = (ROOT / "ci/test/03_test_script.sh").read_text(encoding="utf-8")
        start = script.index('if [[ "$CI_OS_NAME" == "macos"')
        block = script[start:script.index('if [ "$RUN_UNIT_TESTS"', start)]
        client_name = re.search(r'set\(CLIENT_NAME "([^"]+)"\)', BUILD_CONFIG.read_text()).group(1)
        with tempfile.TemporaryDirectory(prefix="roots-macos-deployment-") as directory:
            build = Path(directory)
            with zipfile.ZipFile(build / f"{client_name.replace(' ', '-')}.zip", "w") as archive:
                archive.writestr("Bitcoin-Qt.app/Contents/MacOS/Bitcoin-Qt", "fixture")
            tools = build / "tools"
            tools.mkdir()
            codesign = tools / "codesign"
            codesign.write_text(
                '#!/bin/sh\nprintf "%s\\n" "$*" > "$CODESIGN_LOG"\nexit "$CODESIGN_STATUS"\n',
                encoding="utf-8",
            )
            codesign.chmod(0o755)
            log = build / "codesign.log"
            for status in (0, 1):
                with self.subTest(codesign_status=status):
                    shutil.rmtree(build / "deploy", ignore_errors=True)
                    result = subprocess.run(
                        ["bash", "-ec", block],
                        env={**os.environ, "PATH": f"{tools}:{os.environ['PATH']}",
                             "BASE_BUILD_DIR": str(build), "CI_OS_NAME": "macos", "GOAL": "install deploy",
                             "CODESIGN_LOG": str(log), "CODESIGN_STATUS": str(status)},
                        capture_output=True, text=True,
                    )
                    self.assertEqual(result.returncode, status, result.stderr)
                    self.assertEqual(log.read_text().strip(), f"--verify {build}/deploy/Bitcoin-Qt.app")

    def test_workflow_tags_build_signed_drafts_and_dispatch_only_rehearses(self):
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("workflow_dispatch:", workflow)
        self.assertIn("  push:", workflow)
        self.assertIn("RELEASE_TAG: ${{ github.event_name == 'push' && github.ref_name || inputs.release_tag }}", workflow)
        self.assertIn("ref: ${{ env.RELEASE_SOURCE_REF }}", workflow)
        self.assertEqual(workflow.count("ci/release/prepare-release-source.sh"), 5)
        publish = workflow.split("  publish-release:", 1)[1]
        self.assertIn("if: github.event_name == 'push'", publish)
        self.assertIn("contents: write", publish)
        self.assertIn("REQUIRE_RELEASE_SIGNATURE: '1'", publish)
        self.assertIn("test -s", publish)
        self.assertNotIn("secrets.", workflow.split("  publish-release:", 1)[0])
        self.assertNotIn("--clobber", workflow)
        for metadata_workflow in (RELEASE_WORKFLOW, ROOT / ".github/workflows/ci.yml"):
            metadata = metadata_workflow.read_text()
            for test in ("test_release_notices.py", "test_create_release_draft.py",
                         "test_runner_release_notices.py", "test_create_ci_release_draft.py"):
                self.assertIn("python3 ci/test/" + test, metadata)

    def test_workflow_builds_complete_artifact_matrix_and_patch(self):
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        for artifact in [
            "bitcoin-roots-linux-x86_64",
            "bitcoin-roots-linux-aarch64",
            "bitcoin-roots-darwin-x86_64",
            "bitcoin-roots-darwin-arm64",
            "bitcoin-roots-windows-x86_64",
        ]:
            self.assertIn(artifact, workflow)
        self.assertIn("ci/release/create-patch-series.sh", workflow)
        self.assertIn("ci/release/create-ci-draft.py", workflow)

    def test_workflow_references_only_present_local_release_files(self):
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        local_paths = re.findall(r"(?:ci|contrib)/[A-Za-z0-9_./-]+\.(?:asc|py|sh)", workflow)
        for local_path in local_paths:
            with self.subTest(local_path=local_path):
                self.assertTrue((ROOT / local_path).is_file())

    def test_workflow_actions_are_intentionally_bounded(self):
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        actions = set(re.findall(r"uses: (\S+)", workflow))
        self.assertEqual(
            actions,
            {
                "actions/checkout@v6",
                "actions/upload-artifact@v4",
                "actions/download-artifact@v5",
                "./.github/actions/configure-docker",
                "./.github/actions/configure-environment",
            },
        )


if __name__ == "__main__":
    unittest.main()
