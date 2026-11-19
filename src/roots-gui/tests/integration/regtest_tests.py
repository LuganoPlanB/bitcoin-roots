# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Fund public descriptor/watch-only/multisig wallets in owned regtest only."""

import sys

sys.dont_write_bytecode = True
import argparse
from decimal import Decimal
import json
from pathlib import Path
import socket
import signal

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import evidence_directory, Checks, write_json
from rpc.fake_http import RpcError, TransportError, request
from owned_regtest import OwnedRegtest

PUB1 = "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"
PUB2 = "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5"


def profile(binary, out, kind):
    checks = Checks()
    nodes = []
    observed = {}
    error = None

    def expected(node, method, params, codes, wallet=None):
        try:
            node.call(method, params, wallet=wallet)
        except RpcError as e:
            checks.check(e.code in codes, method + " expected error code")
            return
        checks.check(False, method + " must reject")

    try:
        with OwnedRegtest(binary, out / "funded") as n:
            nodes.append(n)
            chain = n.ready()
            checks.check(
                chain["chain"] == "regtest" and chain["blocks"] == 0,
                "fresh explicit regtest",
            )
            network = n.call("getnetworkinfo")
            checks.check(network["connections"] == 0, "external peers disabled")
            checks.check(network["version"] == 320100, "explicit backend v32.1 profile")
            try:
                request(n.port, "Basic deliberately-invalid", "getblockchaininfo")
            except TransportError as e:
                checks.check(
                    e.kind == "authentication" and e.status == 401,
                    "real daemon authentication rejection",
                )
            else:
                checks.check(False, "unauthenticated daemon access must fail")
            expected(n, "getwalletinfo", {}, [-18])
            descriptors = {}
            addresses = {}
            for wallet, expr in [
                ("fixture-watch", f"wpkh({PUB1})"),
                ("fixture-multisig", f"wsh(sortedmulti(2,{PUB1},{PUB2}))"),
            ]:
                n.call(
                    "createwallet",
                    dict(
                        wallet_name=wallet,
                        disable_private_keys=True,
                        blank=True,
                        descriptors=True,
                        load_on_startup=False,
                    ),
                )
                info = n.call("getwalletinfo", wallet=wallet)
                checks.check(
                    info["descriptors"] and not info["private_keys_enabled"],
                    wallet + " public descriptor wallet",
                )
                desc = n.call("getdescriptorinfo", dict(descriptor=expr))["descriptor"]
                descriptors[wallet] = desc
                imported = n.call(
                    "importdescriptors",
                    dict(requests=[dict(desc=desc, timestamp="now")]),
                    wallet=wallet,
                )
                checks.check(
                    imported[0]["success"], wallet + " public descriptor imported"
                )
                addresses[wallet] = n.call("deriveaddresses", dict(descriptor=desc))[0]
            n.call(
                "generatetoaddress",
                dict(nblocks=1, address=addresses["fixture-multisig"]),
            )
            n.call(
                "generatetoaddress",
                dict(nblocks=101, address=addresses["fixture-watch"]),
            )
            checks.check(
                n.call("getblockcount") == 102, "synthetic regtest funding matured"
            )
            for wallet in addresses:
                balance = n.call("getbalances", wallet=wallet)
                available = n.call("listunspent", dict(minconf=1), wallet=wallet)
                checks.check(
                    bool(available) and all(u["solvable"] for u in available),
                    wallet + " mature watch-only solvable UTXOs",
                )
                checks.check(
                    all(u["spendable"] for u in available),
                    wallet + " deprecated spendable field is not custody evidence",
                )
                checks.check(
                    balance["mine"]["trusted"] > Decimal(0),
                    "funding visible in descriptor wallet",
                )
                psbt = n.call(
                    "walletcreatefundedpsbt",
                    dict(
                        inputs=[],
                        outputs=[{addresses["fixture-watch"]: 1}],
                        options=dict(
                            include_watching=True,
                            change_address=addresses[wallet],
                            fee_rate=1,
                            lock_unspents=True,
                        ),
                    ),
                    wallet=wallet,
                )
                decoded = n.call("decodepsbt", dict(psbt=psbt["psbt"]))
                checks.check(
                    bool(decoded["inputs"]) and len(decoded["outputs"]) >= 1,
                    wallet + " funded unsigned construction",
                )
                checks.check(
                    all(
                        not x.get("partial_signatures")
                        and not x.get("final_scriptwitness")
                        and not x.get("final_scriptsig")
                        for x in decoded["inputs"]
                    ),
                    wallet + " no signatures or finalized bytes",
                )
                enriched = n.call(
                    "walletprocesspsbt",
                    dict(psbt=psbt["psbt"], sign=False, finalize=False),
                    wallet=wallet,
                )
                checks.check(
                    not enriched["complete"],
                    wallet + " public enrichment remains unsigned",
                )
                final = n.call("finalizepsbt", dict(psbt=enriched["psbt"]))
                checks.check(
                    not final["complete"], wallet + " unsigned cannot finalize"
                )
                locked = n.call("listlockunspent", wallet=wallet)
                checks.check(bool(locked), wallet + " fixture-owned locks observed")
                checks.check(
                    n.call(
                        "lockunspent",
                        dict(unlock=True, transactions=locked),
                        wallet=wallet,
                    ),
                    wallet + " fixture-owned locks released",
                )
                checks.check(
                    n.call("listlockunspent", wallet=wallet) == [],
                    wallet + " lock cleanup settled",
                )
            checks.check(
                n.call("getrawmempool") == [], "no signing or broadcast effects"
            )
            expected(n, "roots_gui_missing_method", {}, [-32601])
            expected(n, "getwalletinfo", {}, [-18], wallet="fixture missing / wallet")
            checks.check(
                not n.call("validateaddress", dict(address="not-a-mainnet-address"))[
                    "isvalid"
                ],
                "invalid destination rejected",
            )
            observed = dict(
                chain=n.call("getblockchaininfo")["chain"],
                height=n.call("getblockcount"),
                backend=n.record["backend"],
                wallets=list(addresses),
                custody="daemon wallets contain public descriptors only; no private keys supplied",
            )
        checks.check(
            nodes[-1].record["exit"] == 0 and nodes[-1].record["datadirRemoved"],
            "funded owned daemon clean shutdown",
        )
        # Occupied port belongs to this fixture. The failing daemon may never stop it.
        with socket.socket() as occupied:
            occupied.bind(("127.0.0.1", 0))
            occupied.listen()
            port = occupied.getsockname()[1]
            with OwnedRegtest(binary, out / "startup-failure", port=port) as n:
                nodes.append(n)
                try:
                    n.ready(timeout=3)
                except RuntimeError:
                    checks.check(
                        n.process.poll() is not None,
                        "injected startup bind failure observed",
                    )
                else:
                    checks.check(False, "occupied port must not start daemon")
            checks.check(
                occupied.fileno() != -1, "foreign-to-daemon owned port not signaled"
            )
            checks.check(
                n.record["exit"] != 0
                and n.record["settled"]
                and n.record["datadirRemoved"],
                "startup failure cleanup",
            )
        with OwnedRegtest(binary, out / "readiness-timeout") as n:
            nodes.append(n)
            n.ready()
            checks.check(
                n.call("getblockchaininfo")["chain"] == "regtest",
                "timeout injection starts after actual RPC readiness",
            )
            try:
                n.ready(timeout=0.2, hold=True)
            except TimeoutError:
                checks.check(
                    n.process.poll() is None,
                    "held readiness timeout with live owned child",
                )
            else:
                checks.check(False, "readiness gate must time out")
        checks.check(
            n.record["settled"] and n.record["datadirRemoved"],
            "timeout releases and settles owned process",
        )
    except BaseException as e:
        # Exception text/HTTP bodies may contain credentials; preserve only safe type.
        error = type(e).__name__
        if not checks.items or checks.items[-1]["passed"]:
            checks.items.append(
                dict(id="unexpected fixture exception " + error, passed=False)
            )
    result = checks.result()
    result.update(
        selected=41,
        unexecuted=max(0, 41 - len(checks.items)),
        profile=kind,
        observed=observed,
        processes=[n.record for n in nodes],
        errorType=error,
    )
    write_json(out / "result.json", result)
    return result


def main():
    def interrupted(*_):
        raise SystemExit(143)

    signal.signal(signal.SIGTERM, interrupted)
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--roots-daemon", type=Path)
    p.add_argument("--core-daemon", type=Path)
    p.add_argument("--output", required=True, type=Path)
    a = p.parse_args()
    if not a.roots_daemon and not a.core_daemon:
        p.error(
            "supply at least one exact --roots-daemon or --core-daemon binary; no autodiscovery or capability pass"
        )
    out = evidence_directory(a.output)
    results = []
    for kind, binary in [("roots", a.roots_daemon), ("core", a.core_daemon)]:
        if binary:
            results.append(profile(binary, out / kind, kind))
    result = {
        k: sum(r[k] for r in results)
        for k in ["selected", "executed", "passed", "failed", "skipped"]
    }
    result.update(
        profiles=results,
        unexecutedProfiles=[
            k for k, b in [("roots", a.roots_daemon), ("core", a.core_daemon)] if not b
        ],
    )
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
                    "unexecutedProfiles",
                ]
            }
        )
    )
    return int(result["failed"] > 0 or result["executed"] != 41 * len(results))


if __name__ == "__main__":
    raise SystemExit(main())
