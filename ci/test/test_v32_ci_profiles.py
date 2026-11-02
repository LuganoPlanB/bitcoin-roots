#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Current CI feature requirements, workflow paths and diagnostic boundaries."""
import json
import os
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class V32CIProfilesTest(unittest.TestCase):
    def profile(self, name):
        result = subprocess.run(['bash', '-c', 'source "$1"; python3 -c "import json,os; print(json.dumps(dict(os.environ)))"',
                                 'profile-fixture', str(ROOT / 'ci/test' / name)],
                                env=os.environ | {'INSTALL_BCC_TRACING_TOOLS': 'false'},
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_fast_ipc_and_qt6_profiles_require_their_dependencies(self):
        for name in ['00_setup_env_native_fast.sh', '00_setup_env_native_gui.sh',
                     '00_setup_env_native_extended_functional.sh', '00_setup_env_native_system_libraries.sh']:
            with self.subTest(profile=name):
                profile = self.profile(name)
                self.assertIn('-DENABLE_IPC=ON', profile['BITCOIN_CONFIG'])
                self.assertIn('libcapnp-dev', profile['PACKAGES'])
                self.assertIn('pycapnp', profile['PIP_PACKAGES'])
                self.assertNotIn('libevent', profile['PACKAGES'])
        gui = self.profile('00_setup_env_native_gui.sh')
        self.assertIn('qt6-base-dev', gui['PACKAGES'])
        self.assertIn('-DBUILD_GUI_TESTS=ON', gui['BITCOIN_CONFIG'])
        system = self.profile('00_setup_env_native_system_libraries.sh')
        self.assertIn('-DWITH_SYSTEM_LEVELDB=ON', system['BITCOIN_CONFIG'])
        self.assertIn('-DWITH_SYSTEM_LIBSECP256K1=ON', system['BITCOIN_CONFIG'])

    def test_workflows_reference_existing_v32_profiles_and_presets(self):
        presets = {item['name'] for item in json.loads((ROOT / 'CMakePresets.json').read_text())['configurePresets']}
        count = 0
        for name in ['ci.yml', 'nightly.yml', 'reusable-sanitizer.yml']:
            text = (ROOT / '.github/workflows' / name).read_text()
            for path in re.findall(r'\./ci/test/00_setup_env_[a-z0-9_]+\.sh', text):
                self.assertTrue((ROOT / path).is_file(), path)
                count += 1
            for preset in re.findall(r'--preset ([a-z0-9-]+)', text):
                self.assertIn(preset, presets)
            self.assertNotIn('libevent', text)
        self.assertGreater(count, 0)

    def test_uploads_use_collected_diagnostics_instead_of_raw_datadirs(self):
        for name in ['ci.yml', 'nightly.yml', 'reusable-sanitizer.yml']:
            text = (ROOT / '.github/workflows' / name).read_text()
            self.assertIn('roots-failures', text) if name == 'nightly.yml' else self.assertIn('ROOTS_ARTIFACTS_DIR', text)
            self.assertNotRegex(text, r'\$\{\{[^\n]+\}\}/ci/scratch[^\n]*/test_runner/')
        ci = (ROOT / '.github/workflows/ci.yml').read_text()
        self.assertIn('python3 -m unittest discover -s ci/test', ci)
        reusable = (ROOT / '.github/workflows/reusable-sanitizer.yml').read_text()
        self.assertIn("INSTALL_BCC_TRACING_TOOLS=true", reusable)
        self.assertNotIn('sed -i', reusable)


if __name__ == '__main__':
    unittest.main()
