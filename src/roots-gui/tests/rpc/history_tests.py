# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Read-only production history client against explicit, test-owned Roots regtest."""

import sys

sys.dont_write_bytecode = True
import argparse
from datetime import datetime, timezone
from decimal import Decimal
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import selectors
import signal
import subprocess
import threading
import socket
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))
from owned_regtest import OwnedRegtest
from rpc.fake_http import RpcError, strict_json
from support.runtime import binary_identity, evidence_directory, write_json


def sat(value):
    n = Decimal(value) * 100000000
    assert n == n.to_integral_value()
    return int(n)


class ReadOnlyProxy:
    """Observe only public RPC method/params; never retain auth or raw bodies."""

    allowed = {
        "getnetworkinfo",
        "getblockchaininfo",
        "getblockhash",
        "getwalletinfo",
        "help",
        "getopenrpcinfo",
        "listdescriptors",
        "deriveaddresses",
        "getdescriptorinfo",
        "getaddressinfo",
        "getbalances",
        "listsinceblock",
        "gettransaction",
        "listunspent",
        "listlockunspent",
    }

    def __init__(self, node, disconnect_flag=None, before_read=None, after_read=None):
        self.node = node
        self.events = []
        self.failure = False
        proxy = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def do_POST(self):
                request = json.loads(
                    self.rfile.read(int(self.headers["Content-Length"]))
                )
                method = request.get("method")
                params = request.get("params")
                if method not in proxy.allowed or (
                    method == "listdescriptors" and params != {"private": False}
                ):
                    proxy.failure = True
                    self.send_error(403)
                    return
                proxy.events.append(dict(method=method, path=self.path, params=params))
                if before_read:
                    try:
                        before_read(method)
                    except BaseException:
                        proxy.failure = True
                        self.send_error(500)
                        return
                if (
                    disconnect_flag
                    and disconnect_flag.exists()
                    and method == "listsinceblock"
                ):
                    self.close_connection = True
                    try:
                        self.connection.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass
                    return

                connection = http.client.HTTPConnection(
                    "127.0.0.1", node.port, timeout=15
                )
                try:
                    connection.request(
                        "POST",
                        self.path,
                        json.dumps(request),
                        {
                            "Authorization": self.headers.get("Authorization", ""),
                            "Content-Type": "application/json",
                        },
                    )
                    response = connection.getresponse()
                    body = response.read()
                    if after_read:
                        after_read(method, self.path, params, response.status, body)
                    self.send_response(response.status)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                except (OSError, http.client.HTTPException):
                    try:
                        self.send_error(502)
                    except OSError:
                        pass
                finally:
                    connection.close()

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()
        self.port = self.server.server_address[1]

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=3)
        assert not self.thread.is_alive()


class Client:
    def __init__(self, binary, node, proxy, wallet, genesis):
        self.argv = [
            str(binary),
            str(proxy.port),
            str(node.data / "regtest/.cookie"),
            genesis,
            wallet,
        ]
        self.commands = []
        self.process = subprocess.Popen(
            self.argv,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    def read(self):
        with selectors.DefaultSelector() as selector:
            selector.register(self.process.stdout, selectors.EVENT_READ)
            assert selector.select(20), "owned history client deadline"
        line = self.process.stdout.readline()
        assert line, "history client exited"
        return strict_json(line)

    def call(self, command):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()
        start = datetime.now(timezone.utc).isoformat()
        result = self.read()
        self.commands.append(
            dict(
                command=command,
                startUTC=start,
                endUTC=datetime.now(timezone.utc).isoformat(),
                result=result,
            )
        )
        return result

    def close(self):
        try:
            self.process.stdin.close()
        except BrokenPipeError:
            pass
        forced = False
        try:
            self.process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            forced = True
            self.process.kill()
            self.process.wait(timeout=3)
        error = self.process.stderr.read()
        self.process.stderr.close()
        self.process.stdout.close()
        return dict(
            argv=self.argv,
            commands=self.commands,
            exit=self.process.returncode,
            stderr=error,
            settled=self.process.poll() is not None,
            forcedTermination=forced,
        )


def main():
    def interrupted(*_):
        raise SystemExit(143)

    signal.signal(signal.SIGTERM, interrupted)
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", type=Path, required=True)
    p.add_argument("--daemon", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    out = evidence_directory(args.output)
    checks = []
    snapshots = []
    clients = []
    client_records = []
    proxy = None
    node = None
    failure = None
    phase = "startup"
    time = 1800000000
    selected = [
        "initial-two-wallet",
        "pending-receive",
        "replacement-conflict",
        "self-transfer",
        "multiple-recipients",
        "immature",
        "maturity",
        "reorg",
        "reorg-rebroadcast",
        "reconsider",
        "offline-lock",
    ]
    phases = []

    def check(ok, label):
        checks.append(dict(phase=phase, id=label, passed=bool(ok)))
        assert ok, label

    try:
        with OwnedRegtest(args.daemon, out / "daemon") as node:
            chain = node.ready()
            check(
                chain["chain"] == "regtest"
                and node.call("getnetworkinfo")["connections"] == 0,
                "explicit isolated regtest",
            )
            for method in ["sendtoaddress", "sendmany", "bumpfee"]:
                (out / (method + "-help.txt")).write_text(
                    node.call("help", dict(command=method))
                )
            funder = "history fixture funding"
            wallet_a = "history sender / 札幌"
            wallet_b = "history recipient / café"
            for wallet in [funder, wallet_a, wallet_b]:
                node.call(
                    "createwallet",
                    dict(wallet_name=wallet, descriptors=True, load_on_startup=False),
                )

            def advance():
                nonlocal time
                time += 600
                node.call("setmocktime", dict(timestamp=time))

            def address(wallet):
                return node.call("getnewaddress", wallet=wallet)

            def mine(count, destination=None):
                advance()
                return node.call(
                    "generatetoaddress",
                    dict(nblocks=count, address=destination or address(funder)),
                )

            def broadcast(wallet, txid):
                raw = node.call("gettransaction", dict(txid=txid), wallet=wallet)["hex"]
                node.call("sendrawtransaction", dict(hexstring=raw))
                return txid

            def send(wallet, destination, amount):
                advance()
                return broadcast(
                    wallet,
                    node.call(
                        "sendtoaddress",
                        dict(
                            address=destination,
                            amount=amount,
                            fee_rate=2,
                            replaceable=True,
                        ),
                        wallet=wallet,
                    ),
                )

            mine(101)
            advance()
            deposit = broadcast(
                funder,
                node.call(
                    "sendmany",
                    dict(
                        dummy="",
                        amounts={
                            address(wallet_a): "10.00000000",
                            address(wallet_b): "4.00000000",
                        },
                        fee_rate=2,
                    ),
                    wallet=funder,
                ),
            )
            mine(1)
            genesis = node.call("getblockhash", [0])
            proxy = ReadOnlyProxy(node)
            a = Client(args.binary, node, proxy, wallet_a, genesis)
            clients.append(a)
            check(a.read()["ready"], "sender production client ready")
            b = Client(args.binary, node, proxy, wallet_b, genesis)
            clients.append(b)
            check(b.read()["ready"], "recipient production client ready")
            check(
                a.call("empty-policy")["totals"]["total"] is None,
                "policy alone cannot fabricate zero",
            )
            b.call("empty-policy")

            def observe(client, wallet, expected_complete=True):
                start_calls = len(proxy.events)
                snapshot = client.call("run")
                check(
                    snapshot["errors"] == ["success", "success", "success"],
                    "production RPC normalization succeeds",
                )
                check(
                    snapshot["ledger"]
                    and snapshot["has_rpc"]
                    and snapshot["complete"] == expected_complete
                    and not snapshot["limited"],
                    "known reconciliation or explicit Unknown during nonmempool uncertainty",
                )
                queried = [
                    e["params"]["txid"]
                    for e in proxy.events[start_calls:]
                    if e["method"] == "gettransaction"
                ]
                check(
                    len(queried) == len(set(queried)),
                    "each delta transaction enriched once despite multiple RPC entries",
                )
                balances = node.call("getbalances", wallet=wallet)["mine"]
                total = sum(
                    sat(balances[k])
                    for k in ["trusted", "untrusted_pending", "immature"]
                )
                rows = {r["txid"]: r for r in snapshot["rows"]}
                check(
                    len(rows) == len(snapshot["rows"]),
                    "unique parent per txid within wallet",
                )
                raw = node.call(
                    "listsinceblock", dict(include_removed=True), wallet=wallet
                )
                unique = {r["txid"] for r in raw["transactions"]}
                check(unique <= set(rows), "all RPC-enumerated transactions accounted")
                expected = {}
                contribution = 0
                for txid, row in rows.items():
                    tx = node.call("gettransaction", dict(txid=txid), wallet=wallet)
                    value = sat(tx["amount"]) + sat(tx.get("fee", 0))
                    if tx.get("generated"):
                        value = sum(
                            sat(d["amount"])
                            for d in tx["details"]
                            if d["category"] in ["generate", "immature", "orphan"]
                        )
                    check(
                        row["delta"] == value
                        and row["amount"] == sat(tx["amount"])
                        and row["fee"] == (sat(tx["fee"]) if "fee" in tx else None),
                        "canonical net/one fee exact " + txid,
                    )
                    check(
                        len(row["details"]) == len(tx["details"]),
                        "canonical child count " + txid,
                    )
                    check(
                        row["confirmations"] == int(tx["confirmations"]),
                        "current confirmation depth " + txid,
                    )
                    if row["state"] in [0, 1, 2]:
                        contribution += row["delta"]
                    expected[txid] = dict(
                        amount=sat(tx["amount"]),
                        fee=sat(tx["fee"]) if "fee" in tx else None,
                        confirmations=int(tx["confirmations"]),
                        detailAmounts=[sat(d["amount"]) for d in tx["details"]],
                        detailFees=[sat(d["fee"]) for d in tx["details"] if "fee" in d],
                    )
                check(
                    snapshot["totals"]["total"] == total,
                    "authoritative wallet total retained exactly",
                )
                check(
                    contribution == total
                    if expected_complete
                    else all(r["running"] is None for r in rows.values()),
                    "active history reconciles or running balances remain Unknown",
                )
                running = 0
                for row in sorted(rows.values(), key=lambda r: (r["time"], r["txid"])):
                    if row["state"] in [0, 1, 2]:
                        running += row["delta"]
                    check(
                        row["running"] == (running if expected_complete else None),
                        "chronological running balance stays attached or Unknown "
                        + row["txid"],
                    )
                coins = node.call(
                    "listunspent",
                    dict(
                        minconf=0,
                        include_unsafe=True,
                        query_options=dict(include_immature_coinbase=True),
                    ),
                    wallet=wallet,
                )
                confirmed = pending = immature = fundable = 0
                for coin in coins:
                    tx = node.call(
                        "gettransaction", dict(txid=coin["txid"]), wallet=wallet
                    )
                    is_immature = any(
                        d["category"] == "immature" for d in tx["details"]
                    )
                    amount = sat(coin["amount"])
                    if "used" in balances and coin.get("reused"):
                        continue
                    if is_immature:
                        immature += amount
                    elif coin["confirmations"] > 0:
                        confirmed += amount
                    else:
                        pending += amount
                    if not is_immature and coin["safe"] and coin["solvable"]:
                        fundable += amount
                if expected_complete or snapshot["inventory_reconciled"]:
                    check(
                        snapshot["inventory_reconciled"]
                        and confirmed + pending + immature == total,
                        "inventory disjoint categories reconcile",
                    )
                    check(
                        snapshot["totals"]["confirmed"] == confirmed
                        and snapshot["totals"]["mempool"] == pending
                        and snapshot["totals"]["immature"] == immature,
                        "exact confirmed/mempool/immature totals",
                    )
                    check(
                        snapshot["totals"]["rpc_fundable"]
                        == fundable
                        == snapshot["totals"]["spendable"],
                        "explicit empty local policy; RPC safe/solvable maturity, never deprecated spendable flag",
                    )
                else:
                    check(
                        all(
                            snapshot["totals"][k] is None
                            for k in [
                                "confirmed",
                                "mempool",
                                "rpc_fundable",
                                "spendable",
                            ]
                        ),
                        "unreconciled inventory categories stay Unknown",
                    )
                snapshots.append(
                    dict(
                        phase=phase,
                        wallet=wallet,
                        snapshot=snapshot,
                        oracle=expected,
                        rpcUniqueCount=len(unique),
                        rpcEntryCount=len(raw["transactions"]),
                        walletTotal=total,
                        confirmed=confirmed,
                        mempool=pending,
                        immature=immature,
                        rpcFundable=fundable,
                    )
                )
                return rows

            phase = "initial-two-wallet"
            ra = observe(a, wallet_a)
            rb = observe(b, wallet_b)
            check(
                ra[deposit]["delta"] == 1000000000
                and rb[deposit]["delta"] == 400000000,
                "same txid independent daemon-wallet accounting",
            )
            phases.append(phase)
            phase = "pending-receive"
            old = send(funder, address(wallet_a), "1.25000000")
            ra = observe(a, wallet_a)
            check(ra[old]["state"] == 1, "incoming mempool state")
            phases.append(phase)
            phase = "replacement-conflict"
            advance()
            replacement = node.call(
                "bumpfee", dict(txid=old, options=dict(fee_rate=10)), wallet=funder
            )["txid"]
            broadcast(funder, replacement)
            mine(1)
            ra = observe(a, wallet_a)
            check(
                ra[old]["state"] == 5 and ra[replacement]["state"] == 0,
                "replaced incoming transaction becomes conflicted without duplicate credit",
            )
            phases.append(phase)
            phase = "self-transfer"
            self_tx = send(wallet_a, address(wallet_a), "2.00000000")
            ra = observe(a, wallet_a)
            check(
                ra[self_tx]["amount"] == 0
                and ra[self_tx]["delta"] == ra[self_tx]["fee"],
                "self-transfer counts only canonical fee",
            )
            phases.append(phase)
            phase = "multiple-recipients"
            advance()
            multi = broadcast(
                wallet_a,
                node.call(
                    "sendmany",
                    dict(
                        dummy="",
                        amounts={
                            address(wallet_b): "1.00000000",
                            address(wallet_b): "2.00000000",
                            address(wallet_a): "0.50000000",
                        },
                        fee_rate=2,
                    ),
                    wallet=wallet_a,
                ),
            )
            ra = observe(a, wallet_a)
            rb = observe(b, wallet_b)
            check(
                len([d for d in ra[multi]["details"] if d["category"] == "send"]) == 3
                and len([d for d in rb[multi]["details"] if d["category"] == "receive"])
                == 2,
                "multiple children with one parent/one fee",
            )
            phases.append(phase)
            phase = "immature"
            block = mine(1, address(wallet_a))[0]
            ra = observe(a, wallet_a)
            coinbase = node.call("getblock", dict(blockhash=block))["tx"][0]
            check(
                ra[coinbase]["state"] == 2
                and ra[coinbase]["amount"] == 0
                and ra[coinbase]["delta"] > 0,
                "immature canonical zero credit does not erase reward",
            )
            phases.append(phase)
            phase = "maturity"
            mine(101)
            ra = observe(a, wallet_a)
            check(
                ra[coinbase]["state"] == 0
                and ra[coinbase]["amount"] == ra[coinbase]["delta"],
                "maturation updates same transaction parent once",
            )
            phases.append(phase)
            phase = "reorg"
            node.call("invalidateblock", dict(blockhash=block))
            ra = observe(a, wallet_a, False)
            check(
                snapshots[-1]["snapshot"]["totals"]["nonmempool"] < 0,
                "signed nonmempool adjustment is accepted without fabricated running balances",
            )
            check(
                ra[coinbase]["state"] in [3, 5] and ra[coinbase]["removed"],
                "removed coinbase retained but excluded",
            )
            phases.append(phase)
            phase = "reorg-rebroadcast"
            broadcast(wallet_a, self_tx)
            broadcast(wallet_a, multi)
            ra = observe(a, wallet_a)
            check(
                ra[coinbase]["removed"],
                "explicit fixture-only rebroadcast reconciles same known transactions",
            )
            phases.append(phase)
            phase = "reconsider"
            node.call("reconsiderblock", dict(blockhash=block))
            ra = observe(a, wallet_a)
            check(
                ra[coinbase]["state"] == 0 and ra[coinbase]["removed"],
                "re-added branch reuses parent and retains removal event",
            )
            phases.append(phase)
            phase = "offline-lock"
            before = len(proxy.events)
            cached = a.call("offline")
            a.call("run")
            check(
                len(proxy.events) == before and cached["freshness"] == 3,
                "offline transport suppressed with dated history",
            )
            locked = a.call("lock")
            check(
                locked["locked"]
                and not locked["ledger"]
                and len(proxy.events) == before,
                "privacy lock retires financial history without RPC",
            )
            phases.append(phase)
            check(
                not proxy.failure
                and all(e["method"] in proxy.allowed for e in proxy.events),
                "production client never issues mutation RPC",
            )
            for client in clients:
                client_records.append(client.close())
            clients.clear()
            proxy.close()
            proxy_events = proxy.events
            proxy = None
            check(
                all(
                    c["settled"]
                    and not c["forcedTermination"]
                    and c["exit"] == 0
                    and not c["stderr"]
                    for c in client_records
                ),
                "client workers/processes settled",
            )
    except BaseException as error:
        failure = dict(
            type=type(error).__name__,
            rpcCode=error.code if isinstance(error, RpcError) else None,
            phase=phase,
        )
    finally:
        for client in clients:
            try:
                client_records.append(client.close())
            except BaseException:
                failure = failure or dict(type="ClientCleanup", phase=phase)
        if proxy:
            proxy_events = proxy.events
            try:
                proxy.close()
            except BaseException:
                failure = failure or dict(type="ProxyCleanup", phase=phase)
        elif "proxy_events" not in locals():
            proxy_events = []
    result = dict(
        selected=len(selected),
        executed=len(phases)
        + (1 if failure and phase in selected and phase not in phases else 0),
        passed=len(phases) - (1 if failure and phase in phases else 0),
        failed=int(failure is not None),
        skipped=0,
        unexecuted=[x for x in selected if x not in phases and x != phase],
        selectedPhases=selected,
        completedPhases=phases,
        checks=checks,
        failure=failure,
        snapshots=snapshots,
        clients=client_records,
        proxyCalls=proxy_events,
        proxySettled=proxy is None or not proxy.thread.is_alive(),
        daemon=node.record if node else None,
        binary=binary_identity(args.binary),
        daemonBinary=binary_identity(args.daemon),
        CoreRuntime="UNEXECUTED",
    )
    if node and (
        not node.record.get("settled")
        or not node.record.get("datadirRemoved")
        or node.record.get("exit") != 0
    ):
        result["failed"] += 1
    write_json(out / "result.json", result)
    print(
        json.dumps(
            {
                k: result[k]
                for k in [
                    "selected",
                    "executed",
                    "passed",
                    "failed",
                    "skipped",
                    "unexecuted",
                ]
            }
        )
    )
    return int(result["failed"] != 0 or len(phases) != len(selected))


if __name__ == "__main__":
    raise SystemExit(main())
