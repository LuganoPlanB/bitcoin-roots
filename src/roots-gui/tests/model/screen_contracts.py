# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Observable inventory contracts, not rendered UI or implemented services."""

import copy
import json
from pathlib import Path
import unittest

REGISTER = Path(__file__).resolve().parents[1] / "fixtures" / "contracts.json"
NAVIGATION = ["Transactions", "Send", "Receive", "Addresses", "UTXOs", "Settings"]
EFFECTS = {
    "Review Transaction": "public-document",
    "Approve Transaction": "local-approval",
    "Prepare Transaction": "unsigned-construction",
    "Sign Transaction": "explicit-daemon-signing",
    "Finalize Transaction": "public-finalization",
    "Broadcast Transaction": "explicit-submission",
}
REQUIREMENTS = {
    "future-service",
    "opt-in",
    "online",
    "capability",
    "valid-context",
    "unlocked",
    "approved-identity",
    "supported-satisfaction",
    "verified-signatures",
    "final-bytes",
    "reconciled-submission",
    "locked",
    "public-inputs-complete",
    "supported-construction",
    "matching-network",
    "exact-fee-policy",
}
SCREEN_SERVICES = {
    "shell": "workspace",
    "wallet-setup": "wallet-service",
    "transactions": "history-service",
    "send": "draft-service",
    "receive": "receive-service",
    "addresses": "address-presentation-service",
    "utxos": "coin-inventory-service",
    "wallet-settings": "wallet-settings",
    "workbench": "document-service",
    "preferences": "preferences-service",
    "tools": "tools-platform-service",
    "privacy": "privacy-lock-service",
    "recovery": "operation-recovery-service",
    "help": "help-service",
}
DISCLOSURE_EFFECTS = {
    "Copy Transaction ID": ("clipboard-service", "local-clipboard"),
    "Show Address QR": ("address-presentation-service", "presentation"),
    "Inspect Address": ("address-presentation-service", "presentation"),
    "Send Selected Coins": ("draft-service", "local-edit"),
    "Export CSV": ("export-service", "local-file-disclosure"),
}


def resolve(data):
    """Resolve inherited ownership once; reject ambiguous or incomplete inventories."""

    def require(condition, message):
        if not condition:
            raise ValueError(message)

    groups = {}
    acceptance = {x["id"] for x in data["acceptance"]}
    for group in data["action_groups"]:
        require(
            all(
                group.get(k)
                for k in (
                    "owner",
                    "support",
                    "effect",
                    "transition",
                    "acceptance",
                    "fixture",
                )
            ),
            "group contract",
        )
        require(set(group["acceptance"]) <= acceptance, "acceptance")
        require(group["fixture"] in data["fixtures"], "fixture")
        for action in group["actions"]:
            require(action not in groups, "duplicate action")
            groups[action] = {k: v for k, v in group.items() if k != "actions"}
    screens = {x["id"]: x for x in data["screens"]}
    require(len(screens) == len(data["screens"]), "duplicate screen")
    require(screens.keys() == SCREEN_SERVICES.keys(), "screen service coverage")
    require(
        all(
            screens[s]["owner"] == owner and screens[s].get("owner_kind") == "service"
            for s, owner in SCREEN_SERVICES.items()
        ),
        "screen service ownership",
    )
    require(
        set(s for x in screens.values() for s in x["sections"]) == set(range(1, 21)),
        "section coverage",
    )
    require([x["action"] for x in data["navigation"]] == NAVIGATION, "navigation")
    require(
        [x["position"] for x in data["navigation"]] == list(range(1, 7)),
        "navigation order",
    )
    require(
        all(x["screen"] in screens for x in data["navigation"]), "navigation screen"
    )
    dialogs = {x["id"]: x for x in data["dialogs"]}
    for dialog in dialogs.values():
        require(
            all(dialog.get(k) for k in ("initial_focus", "dismiss", "return_focus")),
            "dialog focus",
        )
        require(set(dialog["actions"]) <= groups.keys(), "dialog action")
    resolved = {}
    for detail in data["action_details"]:
        action = detail["action"]
        require(action in groups and action not in resolved, "action detail")
        require(not (set(detail) & set(groups[action])), "ownership override")
        require(detail["screen"] in screens, "screen")
        require(
            all(
                detail.get(k)
                for k in (
                    "focus_rule",
                    "focus_target",
                    "disabled_reason",
                    "sections",
                    "state_fixture",
                )
            ),
            "action behavior",
        )
        require(set(detail["sections"]) <= set(range(1, 21)), "action section")
        require(set(detail["dialogs"]) <= dialogs.keys(), "dialog route")
        require(
            isinstance(detail.get("requirements"), list)
            and set(detail["requirements"]) <= REQUIREMENTS,
            "availability requirements",
        )
        require(
            all(action in dialogs[x]["actions"] for x in detail["dialogs"]),
            "dialog correspondence",
        )
        require(detail["state_fixture"] in data["state_fixtures"], "state fixture")
        resolved[action] = groups[action] | detail
    require(resolved.keys() == groups.keys(), "missing action details")
    for screen in screens:
        require(screen in data["state_fixtures"], "screen states")
        states = data["state_fixtures"][screen]
        require(set(states) == set(data["states"]), "state coverage")
        require(
            all(v.get("presentation") and v.get("command") for v in states.values()),
            "state behavior",
        )
    require(set(data["menus"]) == {"File", "View", "Tools", "Help"}, "menus")
    for menu in data["menus"].values():
        require(
            set(menu) <= resolved.keys() and len(menu) == len(set(menu)), "menu action"
        )
    bindings = set()
    for shortcut in data["shortcuts"]:
        require(
            shortcut["action"] in resolved
            and shortcut.get("guard")
            and shortcut.get("platform"),
            "shortcut action",
        )
        binding = (shortcut["context"], shortcut["key"])
        require(binding not in bindings, "shortcut collision")
        bindings.add(binding)
    for action, effect in EFFECTS.items():
        require(
            resolved[action]["effect"] == effect
            and resolved[action]["support"] == "required",
            "financial separation",
        )
    for action in [
        "Prepare Transaction",
        "Approve Transaction",
        "Sign Transaction",
        "Finalize Transaction",
        "Broadcast Transaction",
    ]:
        require(
            {"valid-context", "unlocked"} <= set(resolved[action]["requirements"]),
            "financial context gate",
        )
    for action in ["Sign Transaction", "Finalize Transaction", "Broadcast Transaction"]:
        require(
            "approved-identity" in resolved[action]["requirements"], "approval gate"
        )
    require(
        {"online", "capability", "final-bytes", "reconciled-submission"}
        <= set(resolved["Broadcast Transaction"]["requirements"]),
        "submission gate",
    )
    require(
        {"verified-signatures", "supported-satisfaction"}
        <= set(resolved["Finalize Transaction"]["requirements"]),
        "finalization gate",
    )
    for action, (owner, effect) in DISCLOSURE_EFFECTS.items():
        require(
            (resolved[action]["owner"], resolved[action]["effect"]) == (owner, effect),
            "disclosure/edit separation",
        )
    for action in ["Prepare Transaction", "Prepare RBF", "Prepare CPFP"]:
        require(
            {"online", "capability"} <= set(resolved[action]["requirements"]),
            "RPC funding gate",
        )
    offline = resolved["Construct Offline Transaction"]
    require(
        offline["owner"] == "public-construction-service"
        and offline["support"] == "required"
        and offline["effect"] == "unsigned-construction",
        "offline construction service",
    )
    require(
        set(offline["requirements"])
        == {
            "valid-context",
            "unlocked",
            "public-inputs-complete",
            "supported-construction",
            "matching-network",
            "exact-fee-policy",
        },
        "offline construction gate",
    )
    require(
        offline.get("result_contract")
        == {
            "output": "unsigned-psbt",
            "signs": False,
            "submits": False,
            "spendability": "not-observed",
        },
        "offline construction assurances",
    )
    for action in [
        "Add Account",
        "Payment Code Contacts",
        "Payjoin",
        "Local Seed Custody",
        "Privacy Optimization",
        "Indexed Spending History",
    ]:
        require(
            resolved[action]["support"] == "extension"
            and resolved[action]["effect"] == "unavailable",
            "extended profile",
        )
    require(
        data["platform_evidence"]["Linux"]["requirement"] == "required",
        "Linux required",
    )
    require(
        all(
            data["platform_evidence"][x]["requirement"] == "follow-on"
            for x in ["Windows", "macOS"]
        ),
        "platform honesty",
    )
    return resolved


def command_contract(action, facts):
    """Evaluate planned prerequisites; this dispatches no service or UI operation."""
    missing = set(action["requirements"]) - set(facts)
    # Extensions cannot be enabled by supplying fictional fixture facts.
    if action["support"] == "extension":
        missing.add("future-service")
    return {
        "enabled": not missing,
        "missing": sorted(missing),
        "effect": action["effect"] if not missing else "none",
        "reason": action["disabled_reason"] if missing else "",
        "result_contract": action.get("result_contract") if not missing else None,
    }


class ScreenContracts(unittest.TestCase):
    def setUp(self):
        self.data = json.loads(REGISTER.read_text(encoding="utf-8"))

    def test_resolved_contracts_cover_every_action(self):
        actions = resolve(self.data)
        self.assertEqual(
            len(actions), sum(len(g["actions"]) for g in self.data["action_groups"])
        )
        for action in actions.values():
            self.assertTrue(
                action["owner"] and action["transition"] and action["acceptance"]
            )

    def test_critical_negative_inventories(self):
        cases = [
            lambda d: d["action_details"][0].pop("focus_rule"),
            lambda d: d["action_details"][0].update(owner="invented-service"),
            lambda d: d["action_details"].pop(),
            lambda d: d["shortcuts"].append(copy.deepcopy(d["shortcuts"][0])),
            lambda d: d["navigation"].pop(),
            lambda d: d["state_fixtures"]["send"].pop("offline"),
            lambda d: d["menus"]["File"].append("Invented Action"),
            lambda d: d["screens"].__setitem__(0, d["screens"][1]),
            lambda d: next(
                g for g in d["action_groups"] if "Add Account" in g["actions"]
            ).update(support="required"),
            lambda d: next(
                g for g in d["action_groups"] if "Prepare Transaction" in g["actions"]
            ).update(effect="explicit-submission"),
            lambda d: next(
                a for a in d["action_details"] if a["action"] == "Sign Transaction"
            )["requirements"].remove("approved-identity"),
        ]
        for index, mutate in enumerate(cases):
            with self.subTest(malformed=index):
                data = copy.deepcopy(self.data)
                mutate(data)
                with self.assertRaises(ValueError):
                    resolve(data)

    def test_context_sensitive_save_and_batch_escape(self):
        bindings = {
            (x["context"], x["key"]): x["action"] for x in self.data["shortcuts"]
        }
        self.assertEqual(bindings["workspace", "Primary+S"], "Save Transaction")
        self.assertEqual(bindings["psbt-document", "Primary+S"], "Save PSBT")
        self.assertEqual(bindings["batch-grid", "Ctrl+Tab"], "Exit Batch Grid")
        self.assertEqual(bindings["cancellable-dialog", "Escape"], "Dismiss Dialog")
        for number, action in enumerate(NAVIGATION, 1):
            self.assertEqual(bindings["workspace", f"Primary+{number}"], action)
        expected = {
            "Primary+N",
            "Primary+O",
            "Primary+F",
            "Primary+I",
            "Primary+T",
            "Primary+U",
            "Primary+S",
            "Primary+Comma",
            "Primary+W",
            "Primary+R",
            "Primary+B",
            "Primary+Shift+H",
            "Primary+L",
            "Primary+Shift+L",
            "Primary+Shift+S",
            "Primary+Shift+Alt+S",
            "Primary+M",
        }
        self.assertTrue(
            expected <= {key for context, key in bindings if context == "workspace"}
        )

    def test_screen_service_ownership_and_disclosures(self):
        actions = resolve(self.data)
        self.assertEqual(
            {s["id"]: s["owner"] for s in self.data["screens"]}, SCREEN_SERVICES
        )
        for name, (owner, effect) in DISCLOSURE_EFFECTS.items():
            self.assertEqual(
                (actions[name]["owner"], actions[name]["effect"]), (owner, effect)
            )
        self.assertEqual(actions["Send Selected Coins"]["screen"], "send")

    def test_reject_observed_effect_owner_and_mode_regressions(self):
        old_effects = {
            "Copy Transaction ID": "public-document",
            "Show Address QR": "local-clipboard",
            "Inspect Address": "local-clipboard",
            "Send Selected Coins": "local-metadata",
            "Export CSV": "local-metadata",
        }
        for action, effect in old_effects.items():
            with self.subTest(misclassified=action):
                data = copy.deepcopy(self.data)
                next(g for g in data["action_groups"] if action in g["actions"])[
                    "effect"
                ] = effect
                with self.assertRaisesRegex(ValueError, "disclosure/edit separation"):
                    resolve(data)
        data = copy.deepcopy(self.data)
        next(s for s in data["screens"] if s["id"] == "shell")["owner"] = (
            "contracts-providers"
        )
        with self.assertRaisesRegex(ValueError, "screen service ownership"):
            resolve(data)
        for prerequisite in ["online", "capability"]:
            data = copy.deepcopy(self.data)
            next(
                a
                for a in data["action_details"]
                if a["action"] == "Prepare Transaction"
            )["requirements"].remove(prerequisite)
            with self.assertRaisesRegex(ValueError, "RPC funding gate"):
                resolve(data)
        data = copy.deepcopy(self.data)
        next(
            g
            for g in data["action_groups"]
            if "Construct Offline Transaction" in g["actions"]
        )["result_contract"]["spendability"] = "live"
        with self.assertRaisesRegex(ValueError, "offline construction assurances"):
            resolve(data)

    def test_rpc_and_offline_construction_conditions(self):
        actions = resolve(self.data)
        rpc = actions["Prepare Transaction"]
        self.assertFalse(
            command_contract(rpc, {"valid-context", "unlocked"})["enabled"]
        )
        for prerequisite in ["online", "capability"]:
            result = command_contract(rpc, set(rpc["requirements"]) - {prerequisite})
            self.assertEqual(result["missing"], [prerequisite])
            self.assertEqual(result["effect"], "none")
        offline = actions["Construct Offline Transaction"]
        public = set(offline["requirements"])
        self.assertFalse(public & {"online", "capability"})
        result = command_contract(offline, public)
        self.assertEqual(result["effect"], "unsigned-construction")
        self.assertEqual(
            result["result_contract"],
            {
                "output": "unsigned-psbt",
                "signs": False,
                "submits": False,
                "spendability": "not-observed",
            },
        )
        for prerequisite in public:
            with self.subTest(offline_missing=prerequisite):
                result = command_contract(offline, public - {prerequisite})
                self.assertFalse(result["enabled"])
                self.assertEqual(result["effect"], "none")
                self.assertIsNone(result["result_contract"])

    def test_complete_top_level_menus(self):
        self.assertEqual(
            self.data["menus"]["File"],
            [
                "New Wallet",
                "Open Wallet",
                "Import Wallet",
                "Export Wallet",
                "Open Transaction File",
                "Open Transaction ID",
                "Open Transaction Text",
                "Open Transaction QR",
                "Save Transaction",
                "Save PSBT",
                "Show Transaction QR",
                "Show PSBT QR",
                "Rename Wallet",
                "Delete Wallet",
                "Preferences",
                "Close Tab",
                "Quit",
            ],
        )
        self.assertEqual(
            self.data["menus"]["View"],
            [
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
            ],
        )
        self.assertEqual(
            self.data["menus"]["Tools"],
            [
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
            ],
        )
        self.assertEqual(
            self.data["menus"]["Help"],
            [
                "Introduction",
                "Documentation",
                "Log File",
                "Support",
                "Report Issue",
                "About",
            ],
        )

    def test_financial_prerequisites_and_effects(self):
        actions = resolve(self.data)
        for name, effect in EFFECTS.items():
            action = actions[name]
            facts = set(action["requirements"])
            self.assertEqual(command_contract(action, facts)["effect"], effect)
            for prerequisite in facts:
                with self.subTest(action=name, missing=prerequisite):
                    result = command_contract(action, facts - {prerequisite})
                    self.assertFalse(result["enabled"])
                    self.assertEqual(result["effect"], "none")
                    self.assertTrue(result["reason"])

    def test_offline_locked_and_unavailable_fixtures(self):
        actions = resolve(self.data)
        for name in [
            "Open Transaction File",
            "Open Transaction Text",
            "Save PSBT",
            "Verify Message",
            "Review Transaction",
        ]:
            facts = set(actions[name]["requirements"]) - {"online"}
            self.assertTrue(command_contract(actions[name], facts)["enabled"], name)
        for name in [
            "Next Receive Address",
            "Sign Transaction",
            "Broadcast Transaction",
            "Open Transaction ID",
        ]:
            facts = set(actions[name]["requirements"]) - {"online"}
            self.assertEqual(
                command_contract(actions[name], facts)["effect"], "none", name
            )
        for name in [
            "Prepare Transaction",
            "Approve Transaction",
            "Sign Transaction",
            "Finalize Transaction",
            "Broadcast Transaction",
        ]:
            facts = set(actions[name]["requirements"]) - {"unlocked"}
            self.assertFalse(command_contract(actions[name], facts)["enabled"], name)
        self.assertFalse(
            command_contract(actions["Add Account"], REQUIREMENTS)["enabled"]
        )
        self.assertFalse(command_contract(actions["Fiat Provider"], set())["enabled"])
        self.assertTrue(
            command_contract(actions["Fiat Provider"], {"opt-in"})["enabled"]
        )
        self.assertEqual(
            command_contract(actions["Unlock Workspace"], {"locked"})["effect"],
            "local-lock",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
