# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Validate requirement ownership; these checks do not claim service delivery."""

import json
from pathlib import Path
import unittest

FIXTURES = Path(__file__).resolve().parents[1] / "fixtures" / "contracts.json"


class FixtureContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = json.loads(FIXTURES.read_text(encoding="utf-8"))

    def test_all_spec_sections_and_acceptance_ids_have_owners(self):
        self.assertEqual(
            [x["section"] for x in self.data["sections"]], list(range(1, 21))
        )
        self.assertEqual(
            [x["id"] for x in self.data["acceptance"]],
            [f"AC{x:02}" for x in range(1, 21)],
        )
        for item in self.data["sections"] + self.data["acceptance"]:
            self.assertTrue(item["owner"])
            self.assertIn(item["fixture"], self.data["fixtures"])
        self.assertEqual(len({x["test"] for x in self.data["acceptance"]}), 20)

    def test_menu_and_navigation_inventory(self):
        actions = [
            action
            for group in self.data["action_groups"]
            for action in group["actions"]
        ]
        self.assertEqual(len(actions), len(set(actions)))
        required = {
            "New Wallet",
            "Open Wallet",
            "Import Wallet",
            "Export Wallet",
            "Rename Wallet",
            "Delete Wallet",
            "Open Transaction File",
            "Open Transaction ID",
            "Open Transaction Text",
            "Open Transaction QR",
            "Save Transaction",
            "Save PSBT",
            "Show Transaction QR",
            "Show PSBT QR",
            "Preferences",
            "Close Tab",
            "Quit",
            "BTC Units",
            "Sats Units",
            "Automatic Units",
            "Decimal Format",
            "Address Grouping",
            "Hide Empty Used Addresses",
            "System Theme",
            "Light Theme",
            "Dark Theme",
            "Camera",
            "Separate Wallet Windows",
            "Hide Amounts",
            "Loading Log",
            "Transaction Hex",
            "Tray",
            "Lock Current",
            "Lock All",
            "Search Current",
            "Search All",
            "Summaries",
            "Go To Section",
            "Refresh",
            "Sign Message",
            "Verify Message",
            "Send To Many",
            "Sweep Private Key",
            "Payment Code Contacts",
            "Switch Server",
            "Verify Download",
            "Hardware Access Setup",
            "Prevent Sleep",
            "Restart GUI With Profile",
            "Introduction",
            "Documentation",
            "Log File",
            "Support",
            "Report Issue",
            "About",
            "Transactions",
            "Send",
            "Receive",
            "Addresses",
            "UTXOs",
            "Settings",
            "Add Account",
        }
        self.assertTrue(required.issubset(actions), required - set(actions))

    def test_effects_states_and_fixture_links(self):
        self.assertEqual(
            set(self.data["states"]),
            {
                "normal",
                "empty",
                "loading",
                "error",
                "stale",
                "offline",
                "locked",
                "unavailable",
            },
        )
        ids = {item["id"] for item in self.data["acceptance"]}
        for group in self.data["action_groups"]:
            self.assertIn(group["fixture"], self.data["fixtures"])
            self.assertTrue(
                group["actions"]
                and group["owner"]
                and group["effect"]
                and group["transition"]
            )
            self.assertTrue(set(group["acceptance"]).issubset(ids))
            self.assertIn(
                group["support"], {"required", "extension", "optional-opt-in"}
            )
        for section in self.data["sections"]:
            self.assertTrue(set(section["acceptance"]).issubset(ids))

    def test_extensions_do_not_claim_required_delivery(self):
        self.assertEqual(self.data["delivery"], "Linux RPC wallet")
        self.assertIn("planned", self.data["registration_status"])
        groups = {
            action: group
            for group in self.data["action_groups"]
            for action in group["actions"]
        }
        for action in [
            "Add Account",
            "Payment Code Contacts",
            "Payjoin",
            "Local Seed Custody",
            "Privacy Optimization",
            "Indexed Spending History",
        ]:
            self.assertEqual(groups[action]["support"], "extension")
            self.assertEqual(groups[action]["effect"], "unavailable")
        for action in [
            "Prepare Transaction",
            "Sign Transaction",
            "Finalize Transaction",
            "Broadcast Transaction",
        ]:
            self.assertEqual(groups[action]["support"], "required")
        for action, effect in {
            "Review Transaction": "public-document",
            "Approve Transaction": "local-approval",
            "Prepare Transaction": "unsigned-construction",
            "Sign Transaction": "explicit-daemon-signing",
            "Finalize Transaction": "public-finalization",
            "Broadcast Transaction": "explicit-submission",
            "Revert Wallet Settings": "local-edit",
        }.items():
            self.assertEqual(groups[action]["effect"], effect)


if __name__ == "__main__":
    unittest.main(verbosity=2)
