# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Typed loopback descriptor/range/branch/identity boundaries; not daemon runtime."""

import sys

sys.dont_write_bytecode = True
import argparse
import copy
import json
import subprocess
import tempfile
from pathlib import Path
from datetime import datetime, timezone
from urllib.parse import quote

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from rpc.fake_http import FakeHttp, Step
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--binary", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
out = evidence_directory(a.output)
wallet = "address/札幌"
descs = ["wpkh(public-receive/*)#typed", "tr(public-change/*)#typed"]
cases = []
for scenario in [
    "normal",
    "near-max",
    "exhausted",
    "bad-range",
    "bad-next",
    "private",
    "wrong-parent",
    "heuristic-ischange",
    "duplicate-script",
    "multipath",
    "one-to-many",
    "unsupported",
    "pool-changed",
    "wrong-network",
    "cancel",
    "older-observed",
    "unproven-observed",
    "fixed",
]:
    rows = [
        dict(
            desc=d,
            timestamp=1,
            active=True,
            internal=bool(n),
            range=[0, 10],
            next_index=1,
            next=1,
        )
        for n, d in enumerate(descs)
    ]
    if scenario in ["near-max", "exhausted"]:
        for d in rows:
            d.update(
                range=[2147483646, 2147483647],
                next_index=2147483647 + (scenario == "exhausted"),
                next=2147483647 + (scenario == "exhausted"),
            )
    if scenario in ["older-observed", "unproven-observed"]:
        for d in rows:
            d["next_index"] = d["next"] = 9
    if scenario == "bad-range":
        rows[0]["range"] = [2, 1]
    if scenario == "bad-next":
        rows[0]["next_index"] = rows[0]["next"] = 2147483649
    listing = dict(wallet_name=wallet, descriptors=rows)
    steps = []

    def step(method, result=None, params=None, w=False, error=None):
        def check(path, body, headers):
            assert path == ("/wallet/" + quote(wallet, safe="") if w else "/")
            if params is not None:
                assert body["params"] == params, (method, body["params"], params)
            assert method not in [
                "getnewaddress",
                "getrawchangeaddress",
                "importdescriptors",
                "rescanblockchain",
                "walletprocesspsbt",
                "sendtoaddress",
            ]

        steps.append(Step(method, result, error=error, request_check=check))

    if scenario != "cancel":
        step("getblockhash", "c" * 64 if scenario == "wrong-network" else "a" * 64, [0])
    if scenario not in ["cancel", "wrong-network"]:
        step(
            "getwalletinfo",
            dict(walletname=wallet, descriptors=True, scanning=False),
            [],
            True,
        )
        step("getblockchaininfo", dict(bestblockhash="b" * 64), [])
        step("listdescriptors", listing, dict(private=False), True)
        abort = False
        for n, d in enumerate(rows):
            info = dict(
                descriptor=d["desc"],
                hasprivatekeys=scenario == "private" and n == 0,
                isrange=not (scenario == "fixed" and n == 0),
                issolvable=True,
            )
            if scenario == "multipath" and n == 0:
                info["multipath_expansion"] = descs
            step("getdescriptorinfo", info, dict(descriptor=d["desc"]))
            if n == 0 and scenario in ["bad-range", "bad-next", "private"]:
                abort = True
                break
            if scenario in ["multipath", "fixed"] and n == 0:
                continue
            start = max(d["range"][0], d["next_index"] - 1)
            start = min(start, d["range"][1])
            end = min(d["range"][1], start + 1)
            addresses = [f"public{n}index{i}" for i in range(start, end + 1)]
            if scenario == "one-to-many" and n == 0:
                addresses = [addresses]
            if scenario == "unsupported" and n == 0:
                step(
                    "deriveaddresses",
                    params=dict(descriptor=d["desc"], range=[start, end]),
                    error=-5,
                )
                continue
            step(
                "deriveaddresses",
                addresses,
                dict(descriptor=d["desc"], range=[start, end]),
            )
            if scenario == "one-to-many" and n == 0:
                continue
            for i, address in enumerate(addresses):
                script = f"{81 + n * 4 + i:02x}"
                if scenario == "duplicate-script" and n == 0 and i == 1:
                    script = "51"
                detail = dict(
                    address=address,
                    ismine=True,
                    ischange=not bool(n)
                    if scenario == "heuristic-ischange" and n == 0
                    else bool(n),
                    parent_desc="other"
                    if scenario == "wrong-parent" and n == 0
                    else d["desc"],
                    scriptPubKey=script,
                    labels=["typed label"],
                    hdkeypath=f"m/{n}/{start + i}",
                )
                step("getaddressinfo", detail, dict(address=address), True)
                if (scenario == "wrong-parent" and n == 0) or (
                    scenario == "duplicate-script" and n == 0 and i == 1
                ):
                    abort = True
                    break
            if abort:
                break
        if not abort and scenario in ["older-observed", "unproven-observed"]:
            address = "public0index0"
            detail = dict(
                address=address,
                ismine=True,
                ischange=True,
                parent_desc=descs[0],
                scriptPubKey="59",
                labels=[],
                hdkeypath="m/0/0" if scenario == "older-observed" else "m/0/100",
            )
            step("getaddressinfo", detail, dict(address=address), True)
            if scenario == "older-observed":
                step(
                    "deriveaddresses",
                    [address],
                    dict(descriptor=descs[0], range=[0, 0]),
                )
        if not abort:
            after = copy.deepcopy(listing)
            if scenario == "pool-changed":
                after["descriptors"][0]["next_index"] = after["descriptors"][0][
                    "next"
                ] = 2
            step("listdescriptors", after, dict(private=False), True)
            step("getblockchaininfo", dict(bestblockhash="b" * 64), [])
            step("getblockhash", "a" * 64, [0])
    start = datetime.now(timezone.utc).isoformat()
    with (
        tempfile.TemporaryDirectory(
            prefix="address-rpc-", dir=validated_scratch()
        ) as tmp,
        FakeHttp(steps) as server,
    ):
        cookie = Path(tmp) / "cookie"
        cookie.write_text("fixture:" + server.secret)
        cookie.chmod(0o600)
        argv = [str(a.binary), str(server.port), str(cookie), scenario]
        run = subprocess.run(argv, capture_output=True, text=True, timeout=15)
        result = json.loads(run.stdout) if run.returncode == 0 else {}
        consumed = (
            len(server.records) == len(steps)
            and not server.check_failures
            and all(s.entered.is_set() for s in steps)
        )
    settled = (
        not server.thread.is_alive() and server.active == 0 and not Path(tmp).exists()
    )
    positive = scenario in [
        "normal",
        "near-max",
        "exhausted",
        "multipath",
        "one-to-many",
        "unsupported",
        "heuristic-ischange",
        "pool-changed",
        "older-observed",
        "unproven-observed",
        "fixed",
    ]
    passed = (
        run.returncode == 0
        and consumed
        and settled
        and bool(result.get("snapshot")) == positive
    )
    if positive:
        passed = (
            passed
            and result["stable"] == (scenario != "pool-changed")
            and all(r["index"] <= 2147483647 for r in result["rows"])
        )
        if scenario in ["multipath", "one-to-many", "unsupported", "fixed"]:
            passed = passed and any(result["reasons"]) and len(result["rows"]) == 2
        if scenario == "older-observed":
            passed = (
                passed
                and len(result["rows"]) == 5
                and any(r["index"] == 0 and not r["internal"] for r in result["rows"])
            )
        if scenario == "unproven-observed":
            passed = passed and result["unmapped"] == 1 and len(result["rows"]) == 4
        if scenario == "normal":
            passed = passed and [
                (r["internal"], r["index"], r["lookahead"]) for r in result["rows"]
            ] == [(0, 0, 0), (0, 1, 1), (1, 0, 0), (1, 1, 1)]
        if scenario == "exhausted":
            passed = (
                passed
                and all(not r["lookahead"] for r in result["rows"])
                and len(result["rows"]) == 2
            )
    else:
        passed = passed and result.get("error") != "success"
    cases.append(
        dict(
            name=scenario,
            argv=argv,
            cwd=str(Path.cwd()),
            startUTC=start,
            endUTC=datetime.now(timezone.utc).isoformat(),
            exit=run.returncode,
            stdout=run.stdout,
            stderr=run.stderr,
            result=result,
            passed=bool(passed),
            requests=server.records,
            fixtureFailures=server.check_failures,
            settled=settled,
        )
    )
    write_json(
        out / "result.json",
        dict(
            selected=18,
            executed=len(cases),
            passed=sum(c["passed"] for c in cases),
            failed=sum(not c["passed"] for c in cases),
            skipped=18 - len(cases),
            cases=cases,
            binary=binary_identity(a.binary),
            coverage="Typed loopback source contract only; actual daemon/native coverage is separate.",
        ),
    )
    if not passed:
        raise SystemExit("FAIL address RPC " + scenario)
print(json.dumps(dict(selected=18, executed=18, passed=18, failed=0, skipped=0)))
