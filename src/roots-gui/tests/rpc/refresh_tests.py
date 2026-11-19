# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Read-only production refresh calls against disposable loopback fixtures."""

import sys

sys.dont_write_bytecode = True
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import selectors
import subprocess
import tempfile
import threading
import unittest
from urllib.parse import quote

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)
from fake_http import FakeHttp, Step
from capability_tests import GENESIS
from integration.owned_regtest import OwnedRegtest

WALLET = "public/札幌% wallet"
TIP = "a" * 64


def refresh_steps(*, wallet=WALLET, genesis=GENESIS, cursor=None):
    def named(path, body, headers):
        assert path == "/wallet/" + quote(wallet, safe="")
        assert body["params"] == dict(
            include_removed=True, **({"blockhash": cursor} if cursor else {})
        )

    return [
        Step(
            "getblockchaininfo",
            dict(blocks=7, initialblockdownload=False, pruned=False, bestblockhash=TIP),
        ),
        Step("getblockhash", genesis),
        Step("getwalletinfo", dict(walletname=wallet, scanning=False)),
        Step(
            "getbalances",
            raw=b'{"jsonrpc":"2.0","id":4,"result":{"mine":{"trusted":1e-8,"untrusted_pending":0.00000002,"immature":0.00000003,"nonmempool":0}}}',
        ),
        Step(
            "listsinceblock",
            dict(transactions=[], removed=[], lastblock=TIP),
            request_check=named,
        ),
    ]


class Client:
    def __init__(self, port, cookie, wallet=WALLET):
        self.command = [
            str(ARGS.client),
            str(port),
            str(cookie),
            GENESIS,
            wallet if wallet is not None else "node",
        ]
        self.process = subprocess.Popen(
            self.command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.results = []

    def call(self, command="run"):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()
        with selectors.DefaultSelector() as selector:
            selector.register(self.process.stdout, selectors.EVENT_READ)
            if not selector.select(15):
                raise AssertionError("bounded refresh client deadline")
        value = json.loads(self.process.stdout.readline())
        self.results.append(value)
        return value

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise
        error = self.process.stderr.read()
        self.process.stdout.close()
        self.process.stderr.close()
        assert self.process.returncode == 0 and error == "", (
            "refresh client must settle without diagnostics"
        )
        Cases.records.append(
            dict(
                argv=self.command,
                cwd=os.getcwd(),
                exit=self.process.returncode,
                settled=True,
                results=self.results,
            )
        )


class Cases(unittest.TestCase):
    records = []
    servers = []

    @contextmanager
    def fixture(self, steps, wallet=WALLET):
        with (
            tempfile.TemporaryDirectory(
                prefix="refresh-", dir=validated_scratch()
            ) as tmp,
            FakeHttp(steps) as server,
        ):
            cookie = Path(tmp) / "explicit-cookie"
            cookie.write_text("fixture:" + server.secret)
            client = Client(server.port, cookie, wallet)
            try:
                yield client, server, cookie
            finally:
                client.close()
            self.assertNotIn(server.secret, json.dumps(self.records[-1]))
            self.assertNotIn(server.auth, json.dumps(self.records[-1]))
        self.assertFalse(server.thread.is_alive())
        self.assertEqual(server.active, 0)
        self.assertFalse(server.check_failures)
        self.servers.append(dict(requests=server.records, settled=True))

    def test_exact_categories_wallet_path_and_incremental_cursor(self):
        with self.fixture(refresh_steps() + refresh_steps(cursor=TIP)) as (
            client,
            server,
            _,
        ):
            for _ in range(2):
                value = client.call()
                self.assertEqual(value["errors"], ["success"] * 3)
                self.assertTrue(
                    value["node"]
                    and value["wallet"]
                    and value["history"]
                    and value["dated"]
                )
                self.assertEqual(
                    (value["trusted"], value["pending"], value["immature"]), (1, 2, 3)
                )
        self.assertEqual(len(server.records), 10)

    def test_network_and_wallet_replacement_fail_closed(self):
        with self.fixture(refresh_steps(genesis="b" * 64)) as (client, server, _):
            value = client.call()
            self.assertEqual(value["errors"], ["response-identity"] * 3)
            self.assertFalse(value["node"] or value["wallet"])
        self.assertEqual(len(server.records), 2)
        scripted = refresh_steps()
        scripted[2].result["walletname"] = "another-wallet"
        with self.fixture(scripted) as (client, server, _):
            value = client.call()
            self.assertTrue(value["node"])
            self.assertFalse(value["wallet"] or value["history"])
            self.assertEqual(value["errors"][1:], ["response-identity"] * 2)
        self.assertEqual(len(server.records), 3)

    def test_invalid_money_and_partial_history_are_screen_errors(self):
        for token in ["0.000000001", "-1", "21000001", '"invalid"']:
            scripted = refresh_steps()
            scripted[3].raw = (
                '{"jsonrpc":"2.0","id":4,"result":{"mine":{"trusted":'
                + token
                + ',"untrusted_pending":0,"immature":0}}}'
            ).encode()
            with self.fixture(scripted) as (client, _, _):
                value = client.call()
                self.assertTrue(value["node"])
                self.assertFalse(value["wallet"])
                self.assertEqual(value["errors"][1:], ["json"] * 2)
        scripted = refresh_steps()
        scripted[4].result.pop("removed")
        with self.fixture(scripted) as (client, _, _):
            value = client.call()
            self.assertTrue(value["wallet"])
            self.assertFalse(value["history"])
            self.assertEqual(value["errors"][2], "json")

    def test_cookie_rotation_and_authentication_never_replay(self):
        with self.fixture(refresh_steps() + refresh_steps(cursor=TIP)) as (
            client,
            server,
            cookie,
        ):
            self.assertTrue(client.call()["wallet"])
            cookie.write_text("fixture:expired-disposable-credential")
            value = client.call()
            self.assertEqual(value["errors"], ["authentication"] * 3)
            self.assertEqual(len(server.records), 6)
            replacement = cookie.with_suffix(".new")
            replacement.write_text("fixture:" + server.secret)
            replacement.replace(cookie)
            self.assertTrue(client.call()["wallet"])
        self.assertEqual(len(server.records), 11)

    def test_failure_and_cancel_do_not_issue_followup_calls(self):
        with self.fixture([Step("getblockchaininfo", status=503)]) as (
            client,
            server,
            _,
        ):
            self.assertEqual(client.call()["errors"], ["http"] * 3)
        self.assertEqual(len(server.records), 1)
        gate = threading.Event()
        with self.fixture([Step("getblockchaininfo", gate=gate)]) as (
            client,
            server,
            _,
        ):
            value = client.call("cancel")
            self.assertEqual(value["errors"], ["cancelled"] * 3)
            self.assertFalse(value["node"])
            gate.set()
        self.assertLessEqual(len(server.records), 1)

    def test_node_only_issues_no_wallet_requests(self):
        with self.fixture(refresh_steps()[:2], wallet=None) as (client, server, _):
            value = client.call()
            self.assertTrue(value["node"])
            self.assertFalse(value["wallet"] or value["history"])
        self.assertEqual(len(server.records), 2)


def main():
    global ARGS
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--client", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--daemon", type=Path)
    ARGS = p.parse_args()
    out = evidence_directory(ARGS.output)
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(Cases)
    with (out / "cases.log").open("w") as log:
        result = unittest.TextTestRunner(stream=log, verbosity=2).run(suite)
    runtime = dict(executed=0, reason="No explicit disposable daemon supplied")
    if ARGS.daemon:
        observations = []
        runtime_error = None
        node = None
        try:
            with OwnedRegtest(ARGS.daemon, out / "owned-roots") as node:
                node.ready()
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
                client = Client(
                    node.port, node.data / "regtest/.cookie", "fixture-public"
                )
                try:
                    for _ in range(2):
                        observations.append(client.call())
                finally:
                    client.close()
        except Exception as error:
            runtime_error = type(
                error
            ).__name__  # fixed type only, no response/credential diagnostics
        passed = sum(
            value["errors"] == ["success"] * 3
            and value["dated"]
            and value["history"]
            and value["trusted"] == value["pending"] == value["immature"] == 0
            for value in observations
        )
        runtime = dict(
            selected=2,
            executed=len(observations),
            passed=passed,
            failed=len(observations) - passed,
            skipped=2 - len(observations),
            error=runtime_error,
            observations=observations,
            process=node.record if node else None,
        )
    failed = sorted({test.id() for test, _ in result.failures + result.errors})
    report = dict(
        selected=6,
        executed=result.testsRun,
        passed=result.testsRun - len(failed),
        failed=len(failed),
        skipped=len(result.skipped),
        failedCaseIDs=failed,
        binary=binary_identity(ARGS.client),
        calls=Cases.records,
        servers=Cases.servers,
        runtime=runtime,
        coreRuntime=dict(executed=0, reason="No pinned Core runtime binary supplied"),
        observedUTC=datetime.now(timezone.utc).isoformat(),
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
        not result.wasSuccessful()
        or result.testsRun != 6
        or bool(result.skipped)
        or bool(ARGS.daemon and (runtime["passed"] != 2 or runtime["error"]))
    )


if __name__ == "__main__":
    raise SystemExit(main())
