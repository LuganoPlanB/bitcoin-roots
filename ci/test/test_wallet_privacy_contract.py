#!/usr/bin/env python3
"""Static contracts for wallet privacy behavior and documentation."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class WalletPrivacyContractTest(unittest.TestCase):
    def test_wif_paths_do_not_copy_secrets_to_std_string(self):
        sweep_dialog = (ROOT / "src/qt/sweepdialog.cpp").read_text(encoding="utf-8")
        wallet_interface = (ROOT / "src/wallet/interfaces.cpp").read_text(encoding="utf-8")
        self.assertNotIn("m_private_key->text().toStdString()", sweep_dialog)
        self.assertNotIn("std::string{private_key.begin(), private_key.end()}", wallet_interface)
        self.assertIn("memory_cleanse(encoded.data(), encoded.size())", sweep_dialog)
        self.assertIn("DecodeSecret(private_key)", wallet_interface)
        key_io = (ROOT / "src/key_io.cpp").read_text(encoding="utf-8")
        self.assertIn("DecodeSecretImpl<SecureString, secure_allocator<unsigned char>>", key_io)

    def test_broadcast_has_a_final_authorization_gate(self):
        spend = (ROOT / "src/wallet/spend.cpp").read_text(encoding="utf-8")
        begin = spend.index("authorization.BeginBroadcast()")
        broadcast = spend.index("broadcast(final_tx, error)")
        self.assertLess(begin, broadcast)
        self.assertIn("authorization.IsCancelled()", spend)

    def test_coin_control_documentation_matches_its_row_filter(self):
        documentation = (ROOT / "doc/wallet-privacy.md").read_text(encoding="utf-8")
        wallet_interface = (ROOT / "src/wallet/interfaces.cpp").read_text(encoding="utf-8")
        wallet_spend = (ROOT / "src/wallet/spend.cpp").read_text(encoding="utf-8")
        coin_control = (ROOT / "src/wallet/coincontrol.h").read_text(encoding="utf-8")

        self.assertIn("starts with the wallet's normal available-output list", documentation)
        self.assertIn("Locked\n  outputs remain visible", documentation)
        self.assertIn("Unsafe and immature outputs are not added merely to show a status", documentation)
        self.assertIn("not an\n  inventory of unavailable outputs", documentation)
        self.assertNotIn("Locked, unsafe,\n  immature, or unavailable inputs are reported", documentation)

        list_coins = wallet_spend[wallet_spend.index("ListCoins(const CWallet& wallet)"):]
        self.assertIn("coins_params.skip_locked = false;", list_coins)
        self.assertNotIn("coins_params.include_immature_coinbase = true;", list_coins)
        self.assertIn("bool m_include_unsafe_inputs = false;", coin_control)
        self.assertIn("for (const auto& entry : ListCoins(*m_wallet))", wallet_interface)

    def test_candidate_documents_release_state_and_verification(self):
        catalog = (ROOT / "doc/roots-features.md").read_text(encoding="utf-8")
        guide = (ROOT / "doc/wallet-privacy.md").read_text(encoding="utf-8")
        release_notes = (ROOT / "doc/release-notes.md").read_text(encoding="utf-8")

        for document in (catalog, guide, release_notes):
            self.assertIn("v29.4-roots.3", document)
            self.assertIn("candidate", document)
        self.assertIn("candidate documentation for the intended `v29.4-roots.3` release", release_notes)
        self.assertNotIn("v29.4-roots.3 is now available", release_notes)
        self.assertNotIn("v29.4-roots.3 has been released", release_notes)
        self.assertIn("bitcoin-cli help sweepprivkeys", guide)
        self.assertIn("bitcoin-cli --version", guide)
        self.assertIn("no wallet-format migration", release_notes)


if __name__ == "__main__":
    unittest.main()
