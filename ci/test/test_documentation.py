#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Current candidate documentation and stable preservation vocabulary."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
DOCUMENTS = ['README.md', 'doc/roots-features.md', 'doc/wallet-privacy.md',
             'doc/roots-build-portability.md', 'contrib/roots/README.md', 'contrib/release/release-notes.md']
STABLE_IDS = {
    'policy-parsing', 'effective-policy', 'policy-standardness', 'policy-carrier', 'policy-dust',
    'policy-subdust', 'policy-replacement', 'policy-coinage', 'policy-reuse', 'mining-template',
    'peer-controls', 'peer-health', 'wallet-foundation', 'wallet-rbf', 'wallet-sweep',
    'wallet-coin-control', 'runtime-config', 'operator-stats', 'runtime-resources', 'build-portability',
    'identity-desktop', 'maintenance-ci', 'maintenance-release', 'maintenance-docs', 'maintenance-native',
}


class CandidateDocumentationTest(unittest.TestCase):
    def test_preservation_ids_and_current_candidate_state(self):
        catalog = (ROOT / 'doc/roots-features.md').read_text()
        self.assertEqual(set(re.findall(r'^\| `([a-z-]+)`', catalog, re.MULTILINE)), STABLE_IDS)
        for name in ['README.md', 'doc/roots-features.md', 'doc/wallet-privacy.md', 'contrib/release/release-notes.md']:
            text = (ROOT / name).read_text()
            self.assertIn('v32.1', text, name)
            self.assertIn('unpublished', text, name)
            self.assertIn('candidate', text, name)
        notes = (ROOT / 'contrib/release/release-notes.md').read_text()
        self.assertIn('Historical', notes)
        self.assertTrue((ROOT / 'doc/release-notes/roots-v31.1-roots.6.md').is_file())
        self.assertNotIn('optional Qt 5 support', notes)

    def test_key_disclosure_and_local_policy_boundaries_are_explicit(self):
        wallet = ' '.join((ROOT / 'doc/wallet-privacy.md').read_text().split())
        for term in ['HD master private key', 'ancestor xpub', 'taproot_output', 'musig2_participant',
                     'private_key_hex', 'watch-only', 'not a wallet backup', 'not promised']:
            self.assertIn(term.lower(), wallet.lower(), term)
        catalog = (ROOT / 'doc/roots-features.md').read_text()
        for term in ['multi-op-return', '100-entry', '10 decimal MB', 'PSBT', 'Satoshi', 'dormant']:
            self.assertIn(term, catalog)
        self.assertIn('consensus-valid blocks remain acceptable', catalog)

    def test_owned_document_links_resolve_locally(self):
        checked = 0
        for name in DOCUMENTS:
            path = ROOT / name
            for target in re.findall(r'\[[^\]]*\]\(([^)]+)\)', path.read_text()):
                if '://' in target or target.startswith(('mailto:', '#')):
                    continue
                relative = target.split('#', 1)[0]
                if not relative:
                    continue
                destination = ROOT / relative.lstrip('/') if relative.startswith('/') else path.parent / relative
                self.assertTrue(destination.exists(), (name, target))
                checked += 1
        self.assertGreater(checked, 0)


if __name__ == '__main__':
    unittest.main()
