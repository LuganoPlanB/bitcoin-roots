# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Production capability negotiation against loopback profile fixtures and an explicit owned daemon."""

import sys

sys.dont_write_bytecode = True
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)
from fake_http import FakeHttp, Step
from integration.owned_regtest import OwnedRegtest

GENESIS = "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"


def schema(*, sign=True, add_inputs=True):
    def method(name, params):
        return dict(
            name=name,
            params=[dict(name=key, schema=value) for key, value in params.items()],
        )

    return dict(
        openrpc="1.4.1",
        methods=[
            method("getwalletinfo", {}),
            method("listdescriptors", {"private": {"type": "boolean"}}),
            method(
                "walletprocesspsbt",
                {
                    "psbt": {"type": "string"},
                    **({"sign": {"type": "boolean"}} if sign else {}),
                },
            ),
            method(
                "walletcreatefundedpsbt",
                {
                    "options": {
                        "type": "object",
                        "properties": {"add_inputs": {"type": "boolean"}}
                        if add_inputs
                        else {},
                    }
                },
            ),
        ],
    )


def steps(
    *,
    wallet="fixture-public",
    unavailable=None,
    policy=False,
    ibd=False,
    pruned=False,
    brand="Core",
    sign=True,
    add_inputs=True,
    no_schema=False,
):
    values = [
        Step("getnetworkinfo", dict(version=320100, subversion=brand)),
        Step(
            "getblockchaininfo",
            dict(chain="regtest", blocks=0, initialblockdownload=ibd, pruned=pruned),
        ),
        Step("getblockhash", GENESIS),
        Step(
            "help",
            "getwalletinfo\nlistdescriptors ( private )\nwalletprocesspsbt psbt\nwalletcreatefundedpsbt inputs\n",
        ),
        Step(
            "getopenrpcinfo",
            schema(sign=sign, add_inputs=add_inputs),
            error=-32601 if no_schema else None,
        ),
    ]
    if wallet is not None:
        values.append(
            Step(
                "getwalletinfo",
                dict(
                    walletname=wallet,
                    descriptors=True,
                    private_keys_enabled=False,
                    external_signer=False,
                    scanning=False,
                ),
                error=unavailable,
            )
        )
        if unavailable is None and not no_schema:

            def public_only(path, body, headers):
                assert body["params"] == {"private": False}, (
                    "public-only probe required"
                )

            values.append(
                Step(
                    "listdescriptors",
                    dict(descriptors=[]),
                    error=-4 if policy else None,
                    request_check=public_only,
                )
            )
    return values


class Cases(unittest.TestCase):
    records = []

    def run_client(self, port, cookie, wallet="fixture-public", genesis=GENESIS):
        command = [
            str(ARGS.client),
            str(port),
            str(cookie),
            genesis,
            wallet if wallet is not None else "node",
        ]
        before = datetime.now(timezone.utc).isoformat()
        completed = subprocess.run(command, capture_output=True, text=True, timeout=35)
        self.assertEqual(completed.returncode, 0)
        self.assertEqual(completed.stderr, "")
        self.records.append(
            dict(
                argv=command,
                cwd=os.getcwd(),
                observedAt=before,
                exit=completed.returncode,
                result=json.loads(completed.stdout),
            )
        )
        return self.records[-1]["result"]

    def fixture(self, scripted, *, wallet="fixture-public", genesis=GENESIS):
        with (
            tempfile.TemporaryDirectory(prefix="cap-", dir=validated_scratch()) as tmp,
            FakeHttp(scripted) as server,
        ):
            cookie = Path(tmp) / "explicit-cookie"
            cookie.write_text("fixture:" + server.secret)
            result = self.run_client(server.port, cookie, wallet, genesis)
            self.assertNotIn(server.secret, json.dumps(self.records[-1]))
            self.assertNotIn(server.auth, json.dumps(self.records[-1]))
        self.assertFalse(server.thread.is_alive())
        self.assertEqual(server.active, 0)
        self.assertFalse(server.check_failures, "read-only probe payload verified")
        self.records[-1]["serverSettled"] = True
        return result, server

    def test_current_roots_core_metadata_and_brand_independence(self):
        for brand in [
            "Bitcoin Core v32.1",
            "Bitcoin Roots 32.1-roots.1",
            "unbranded fixture",
        ]:
            result, server = self.fixture(steps(brand=brand))
            self.assertTrue(result["apply"])
            self.assertEqual(result["wallet"], "Wallet available")
            self.assertEqual(
                (
                    result["sign_argument"],
                    result["fund_exact_inputs"],
                    result["invented_argument"],
                ),
                ("Available", "Available", "Required argument unproven"),
            )
            self.assertEqual(
                server.completed,
                [
                    "getnetworkinfo",
                    "getblockchaininfo",
                    "getblockhash",
                    "help",
                    "getopenrpcinfo",
                    "getwalletinfo",
                    "listdescriptors",
                ],
            )

    def test_no_wallet_disabled_pruned_and_ibd_are_distinct(self):
        for wallet, unavailable, expected in [
            (None, None, "No wallet selected"),
            ("fixture-public", -18, "Wallet not loaded"),
            ("fixture-public", -32601, "Wallet RPC unavailable"),
        ]:
            result, _ = self.fixture(
                steps(wallet=wallet, unavailable=unavailable, ibd=True, pruned=True),
                wallet=wallet,
            )
            self.assertTrue(
                result["apply"]
                and result["authenticated"]
                and result["ibd"]
                and result["pruned"]
            )
            self.assertEqual(result["wallet"], expected)
            self.assertNotEqual(result["sign_argument"], "Available")

    def test_arguments_policy_and_old_schema_fail_closed(self):
        result, _ = self.fixture(steps(sign=False, add_inputs=False, policy=True))
        self.assertEqual(
            (
                result["sign_argument"],
                result["fund_exact_inputs"],
                result["public_descriptors"],
            ),
            (
                "Required argument unproven",
                "Required argument unproven",
                "Daemon rejected public probe",
            ),
        )
        result, _ = self.fixture(steps(no_schema=True))
        self.assertTrue(result["apply"])
        self.assertEqual(result["sign_argument"], "Required argument unproven")
        bounded = steps(no_schema=True)
        bounded[3].result = "\n".join("method" + str(i) for i in range(2049))
        result, _ = self.fixture(bounded)
        self.assertEqual(result["error"], "limit")
        self.assertFalse(result["apply"])

    def test_network_and_wallet_identity_fail_closed(self):
        result, server = self.fixture(steps(), genesis="0" * 64)
        self.assertTrue(result["authenticated"] and result["mismatch"])
        self.assertFalse(result["apply"])
        self.assertEqual(len(server.records), 3)
        scripted = steps(wallet="another-wallet")
        result, _ = self.fixture(scripted)
        self.assertEqual(result["error"], "response-identity")
        self.assertFalse(result["apply"])

    def test_authentication_and_malformed_metadata_do_not_apply(self):
        for scripted, error in [
            ([Step("getnetworkinfo", status=401)], "authentication"),
            ([Step("getnetworkinfo", {"version": "not-an-integer"})], "json"),
        ]:
            result, _ = self.fixture(scripted)
            self.assertEqual(result["error"], error)
            self.assertFalse(result["apply"])


def main():
    global ARGS
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--client", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--daemon", type=Path)
    ARGS = p.parse_args()
    out = evidence_directory(ARGS.output)
    suite = unittest.TestSuite(
        unittest.defaultTestLoader.loadTestsFromName(
            "Cases." + name, sys.modules[__name__]
        )
        for name in [
            "test_current_roots_core_metadata_and_brand_independence",
            "test_no_wallet_disabled_pruned_and_ibd_are_distinct",
            "test_arguments_policy_and_old_schema_fail_closed",
            "test_network_and_wallet_identity_fail_closed",
            "test_authentication_and_malformed_metadata_do_not_apply",
        ]
    )
    with (out / "cases.log").open("w") as log:
        result = unittest.TextTestRunner(stream=log, verbosity=2).run(suite)
    runtime = dict(executed=0, reason="No explicit daemon; runtime profile unexecuted")
    if ARGS.daemon:
        case = Cases()
        with OwnedRegtest(ARGS.daemon, out / "owned-roots") as node:
            node.ready()
            before = case.run_client(node.port, node.data / "regtest/.cookie", None)
            assert before["apply"] and before["wallet"] == "No wallet selected"
            node.call(
                "createwallet",
                dict(
                    wallet_name="fixture-public",
                    disable_private_keys=True,
                    blank=True,
                    descriptors=True,
                    load_on_startup=False,
                ),
            )
            live = case.run_client(node.port, node.data / "regtest/.cookie")
            assert (
                live["apply"]
                and live["wallet"] == "Wallet available"
                and live["sign_argument"] == "Available"
                and live["fund_exact_inputs"] == "Available"
            )
            assert live["public_descriptors"] == "Daemon rejected public probe"
        runtime = dict(executed=2, passed=2, failed=0, process=node.record)
    failed = sorted(
        {test.id().split(" (")[0] for test, _ in result.failures + result.errors}
    )
    report = dict(
        selected=5,
        executed=result.testsRun,
        passed=result.testsRun - len(failed),
        failed=len(failed),
        skipped=len(result.skipped),
        failedCaseIDs=failed,
        binary=binary_identity(ARGS.client),
        calls=Cases.records,
        runtime=runtime,
        coreRuntime=dict(
            executed=0,
            reason="No pinned Core binary supplied; source-bound metadata fixtures only",
        ),
        observedAt=datetime.now(timezone.utc).isoformat(),
    )
    write_json(out / "result.json", report)
    print(
        json.dumps(
            {
                k: report[k]
                for k in ["selected", "executed", "passed", "failed", "skipped"]
            }
        )
    )
    return int(
        not result.wasSuccessful() or result.testsRun != 5 or bool(result.skipped)
    )


if __name__ == "__main__":
    raise SystemExit(main())
