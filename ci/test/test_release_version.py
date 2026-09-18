#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
BUILD_CONFIG = ROOT / "CMakeLists.txt"
MODULE = ROOT / "cmake/module/ClientVersion.cmake"
RELEASE_WORKFLOW = ROOT / ".github/workflows/release.yml"
CI_SCRIPT = ROOT / "ci/test/03_test_script.sh"
CONFIG_HEADER = ROOT / "cmake/bitcoin-build-config.h.in"
CLIENT_VERSION_SOURCE = ROOT / "src/clientversion.cpp"
INFO_PLIST = ROOT / "share/qt/Info.plist.in"
WINDOWS_INSTALLER = ROOT / "share/setup.nsi.in"
QT_RESOURCES = ROOT / "src/qt/res/bitcoin-qt-res.rc"
SPLASH_SCREEN = ROOT / "src/qt/splashscreen.cpp"
ARCHIVE_TOOL = ROOT / "ci/release/archive.py"
RELEASE_NOTES = ROOT / "doc/release-notes.md"
TAG_VALIDATOR = ROOT / "ci/release/validate-release-tag.sh"


class ReleaseVersionTest(unittest.TestCase):
    def run_cmake(self, body, definitions=None):
        with tempfile.TemporaryDirectory() as temporary_dir:
            script = Path(temporary_dir) / "test.cmake"
            script.write_text(f'include("{MODULE}")\n{body}', encoding="utf-8")
            command = ["cmake"]
            for name, value in (definitions or {}).items():
                command.append(f"-D{name}={value}")
            command.extend(["-P", script])
            return subprocess.run(command, capture_output=True, text=True)

    def test_parses_supported_release_tags(self):
        cases = {
            "v29.4-roots.1": "29|4|0|0|29.4-roots.1|v29.4-roots.1|29.4.0",
            "v29.3-roots.1": "29|3|0|0|29.3-roots.1|v29.3-roots.1|29.3.0",
            "v29.3.0-roots.2-rc1": "29|3|0|1|29.3.0-roots.2-rc1|v29.3.0-roots.2-rc1|29.3.0",
            "v30.0+roots.1": "30|0|0|0|30.0+roots.1|v30.0+roots.1|30.0.0",
        }
        body = """
set_client_version_from_tag("${RELEASE_TAG}")
message("RESULT=${CLIENT_VERSION_MAJOR}|${CLIENT_VERSION_MINOR}|${CLIENT_VERSION_BUILD}|${CLIENT_VERSION_RC}|${CLIENT_VERSION_STRING}|${CLIENT_VERSION_FULL}|${CLIENT_VERSION_NUMERIC}")
"""
        for tag, expected in cases.items():
            with self.subTest(tag=tag):
                result = self.run_cmake(body, {"RELEASE_TAG": tag})
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f"RESULT={expected}", result.stderr)

    def test_rejects_unsupported_release_tag(self):
        result = self.run_cmake('set_client_version_from_tag("v29")\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Invalid release version tag: v29", result.stderr)

    def test_configures_tagged_manpage_copies(self):
        with tempfile.TemporaryDirectory() as temporary_dir:
            work = Path(temporary_dir)
            for source in sorted((ROOT / "doc/man").glob("*.1")):
                output = work / source.name
                body = f"""
                set(CLIENT_VERSION_TAG v29.4-roots.1)
                set(CLIENT_VERSION_BASE_STRING 29.4.0.roots-dev)
                set(CLIENT_VERSION_FULL v29.4-roots.1)
configure_tagged_document("{source}" "{output}")
"""
                result = self.run_cmake(body)
                self.assertEqual(result.returncode, 0, result.stderr)
                content = output.read_text(encoding="utf-8")
                self.assertIn("v29.4-roots.1", content)
                self.assertNotIn("v29.4.0.roots-dev", content)

    def test_untagged_and_archive_identities_are_explicit(self):
        build_config = BUILD_CONFIG.read_text(encoding="utf-8")
        self.assertIn("set(CLIENT_VERSION_MINOR 4)", build_config)
        self.assertIn('set(CLIENT_VERSION_SUFFIX ".roots-dev")', build_config)
        self.assertIn('set(CLIENT_VERSION_IS_RELEASE "false")', build_config)
        environment = {**os.environ, "RELEASE_TAG": "", "GITHUB_SHA": "ABCDEF0123456789"}
        result = subprocess.run(["python3", ARCHIVE_TOOL, "root-name"], env=environment, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "bitcoin-roots-git-abcdef012345")
        tagged = subprocess.run(["python3", ARCHIVE_TOOL, "root-name", "--tag", "v29.4-roots.1"], capture_output=True, text=True)
        self.assertEqual(tagged.returncode, 0, tagged.stderr)
        self.assertEqual(tagged.stdout.strip(), "bitcoin-roots-29.4-roots.1")
        invalid = subprocess.run(["bash", TAG_VALIDATOR, "v29.4-roots.2"], capture_output=True, text=True)
        self.assertNotEqual(invalid.returncode, 0)

    def test_release_notes_bind_roots_policy_and_provenance(self):
        notes = RELEASE_NOTES.read_text(encoding="utf-8")
        for required in (
            "Bitcoin Roots 29.4", "v29.4", "3fc0865963a38b871e9f7d94e6151c4953563516",
            "conservative, configurable transaction relay and mempool", "Bitcoin Core consensus",
            "does not enforce RDTS/BIP110", "How to Upgrade", "inherited", "contrib/roots/",
        ):
            with self.subTest(required=required):
                self.assertIn(required, notes)
        self.assertNotIn("Bitcoin Core version 29.3 is now available", notes)

    def test_release_tag_is_authoritative_in_generated_metadata(self):
        config_header = CONFIG_HEADER.read_text(encoding="utf-8")
        client_version = CLIENT_VERSION_SOURCE.read_text(encoding="utf-8")
        info_plist = INFO_PLIST.read_text(encoding="utf-8")
        qt_resources = QT_RESOURCES.read_text(encoding="utf-8")
        self.assertIn('#cmakedefine CLIENT_VERSION_TAG "@CLIENT_VERSION_TAG@"', config_header)
        self.assertLess(client_version.index("#ifdef CLIENT_VERSION_TAG"),
                        client_version.index("defined(BUILD_GIT_TAG)"))
        self.assertIn("<string>@CLIENT_VERSION_STRING@, Copyright", info_plist)
        self.assertIn("<string>@CLIENT_VERSION_FULL@</string>", info_plist)
        self.assertIn("#define VER_PRODUCTVERSION_STR CLIENT_VERSION_STRING", qt_resources)

    def test_release_ci_forwards_tag_to_all_build_paths(self):
        workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        ci_script = CI_SCRIPT.read_text(encoding="utf-8")
        self.assertIn("RELEASE_TAG: ${{ startsWith(github.ref, 'refs/tags/v') && github.ref_name || '' }}", workflow)
        self.assertIn('-DCLIENT_VERSION_TAG="$env:RELEASE_TAG"', workflow)
        self.assertIn("-DCLIENT_VERSION_TAG=$RELEASE_TAG", ci_script)

    def test_user_facing_copyright_attribution(self):
        build_config = BUILD_CONFIG.read_text(encoding="utf-8")
        client_version = CLIENT_VERSION_SOURCE.read_text(encoding="utf-8")
        info_plist = INFO_PLIST.read_text(encoding="utf-8")
        windows_installer = WINDOWS_INSTALLER.read_text(encoding="utf-8")
        splash_screen = SPLASH_SCREEN.read_text(encoding="utf-8")
        notices = (
            "Copyright (C) 2026 Plan-₿ Foundation\n"
            "Copyright (C) 2009-2026 The Bitcoin Knots developers\n"
            "Copyright (C) 2009-2026 The Bitcoin Core developers"
        )

        self.assertIn('set(COPYRIGHT_HOLDERS_SUBSTITUTION "Bitcoin Knots")', build_config)
        self.assertIn('set(COPYRIGHT_FOUNDATION "Plan-₿ Foundation")', build_config)
        self.assertIn("return CopyrightInfo()", client_version)
        self.assertIn("COPYRIGHT_FOUNDATION", splash_screen)
        self.assertIn("@COPYRIGHT_FOUNDATION@", info_plist)
        self.assertIn("@COPYRIGHT_FOUNDATION@", windows_installer)
        for manpage in sorted((ROOT / "doc/man").glob("*.1")):
            with self.subTest(manpage=manpage.name):
                content = manpage.read_text(encoding="utf-8")
                self.assertIn(notices, content)
                self.assertNotIn("The Bitcoin Roots developers", content)


if __name__ == "__main__":
    unittest.main()
