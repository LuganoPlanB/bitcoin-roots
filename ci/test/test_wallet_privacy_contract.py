#!/usr/bin/env python3
"""Static contracts for transient sweep-key handling and cancellation."""

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


if __name__ == "__main__":
    unittest.main()
