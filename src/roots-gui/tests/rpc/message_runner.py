# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Exercise only the current, explicitly scoped wallet message-signing RPC contract."""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)
from capability_tests import GENESIS, steps
from fake_http import FakeHttp, Step

WALLET = "fixture-message"
ADDRESS = "mrCDrCybB6J1vRfbwM5hemdJz73FwDBC8r"
MESSAGE = "  Café\nCafe\u0301\t "
SIGNATURE = "IHm+Zn753LusVaBilc6HCwcCm/zbLc4o2VnygVsW+BeYI7xnxy0YYCANGAt4lcLH9t0JOmsSPewVMTMj6gSoIeo="
WRONG_SIGNATURE = "IHm+Zn753LusVaBilc6HCwcCm/zbLc4o2VnygVsW+BeYd6IKM5RrNUKrEsjLf1+BZ9RNRNACGAi9EPulWxWpgYg="
PUBLIC_KEY = "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"
SCRIPT = "76a914751e76e8199196d454941c45d1b3a323f1433bd688ac"


def now():
    return datetime.now(timezone.utc).isoformat()


def exact(params, *, wallet=True):
    def check(path, body, _headers):
        assert path == ("/wallet/" + WALLET if wallet else "/"), "unexpected RPC scope"
        assert body["params"] == params, "message RPC argument bytes changed"

    return check


def scenario(mode, folder):
    script = steps(wallet=WALLET)
    extension = [
        ("getaddressinfo", ["address"]),
        ("signmessage", ["address", "message"]),
    ]
    for method, keys in extension:
        if mode == "schema" and method == "signmessage":
            keys = ["address"]
        script[4].result["methods"].append(
            dict(
                name=method,
                params=[
                    dict(
                        name=key,
                        schema=dict(
                            type="number"
                            if mode == "schema-type" and key == "message"
                            else "string"
                        ),
                    )
                    for key in keys
                ],
            )
        )
    script[5].result["private_keys_enabled"] = mode != "watch-only"
    wallet_scopes = [False] * 5 + [True, True]
    if mode in ("watch-only", "schema", "schema-type"):
        return script, wallet_scopes, None
    script.append(
        Step(
            "getblockhash",
            "f" * 64 if mode == "changed-genesis" else GENESIS,
            error=-1 if mode == "preflight-failed" else None,
            request_check=exact([0], wallet=False),
        )
    )
    wallet_scopes.append(False)
    if mode in ("changed-genesis", "preflight-failed"):
        return script, wallet_scopes, None
    information = dict(
        walletname=WALLET,
        private_keys_enabled=mode != "fresh-watch",
        external_signer=False,
    )
    if mode == "locked":
        information["unlocked_until"] = 0
    script.append(Step("getwalletinfo", information, request_check=exact([])))
    wallet_scopes.append(True)
    if mode in ("fresh-watch", "locked"):
        return script, wallet_scopes, None
    script.append(
        Step(
            "getaddressinfo",
            dict(
                address=ADDRESS,
                scriptPubKey=SCRIPT,
                ismine=mode != "not-owned",
                iswitness=False,
                pubkey=PUBLIC_KEY,
            ),
            request_check=exact(dict(address=ADDRESS)),
        )
    )
    wallet_scopes.append(True)
    if mode == "not-owned":
        return script, wallet_scopes, None
    gate = threading.Event() if mode == "late" else None

    def sign(path, body, headers):
        exact(dict(address=ADDRESS, message=MESSAGE))(path, body, headers)
        if gate:
            (folder / "entered").write_text("message request observed\n")

    last = Step(
        "signmessage",
        WRONG_SIGNATURE if mode == "bad-reply" else SIGNATURE,
        error=-1 if mode == "uncertain" else None,
        gate=gate,
        request_check=sign,
    )
    script.append(last)
    wallet_scopes.append(True)

    def release():
        deadline = time.monotonic() + 7
        while not (folder / "retired").exists() and time.monotonic() < deadline:
            time.sleep(0.005)
        gate.set()

    worker = threading.Thread(target=release) if gate else None
    return script, wallet_scopes, worker


def run_mode(args, output, mode):
    with tempfile.TemporaryDirectory(
        prefix="message-rpc-", dir=validated_scratch()
    ) as directory:
        root = Path(directory)
        script, scopes, gate = scenario(mode, root)
        with FakeHttp(script) as node:
            cookie = root / "rpc-cookie"
            cookie.write_text("fixture:" + node.secret)
            cookie.chmod(0o600)
            if gate:
                gate.start()
            command = [
                str(args.binary.resolve()),
                "--rpc",
                str(node.port),
                str(cookie),
                mode,
                str(root),
            ]
            started = now()
            try:
                result = subprocess.run(
                    command, capture_output=True, text=True, timeout=14
                )
            finally:
                if gate:
                    gate.join()
            ended = now()
        methods = [(r["method"], r["wallet"]) for r in node.records]
        transcript = (
            methods
            == [
                (step.method, wallet)
                for step, wallet in zip(script, scopes, strict=True)
            ]
            and not node.check_failures
        )
        settled = (
            not node.thread.is_alive()
            and node.active == 0
            and (not gate or not gate.is_alive())
        )
    for payload in (result.stdout, result.stderr):
        assert (
            node.auth not in payload
            and node.secret not in payload
            and MESSAGE not in payload
        )
    record = dict(
        id=mode,
        argv=command,
        cwd=str(Path.cwd()),
        startUTC=started,
        endUTC=ended,
        exit=result.returncode,
        stdout=result.stdout,
        stderr=result.stderr,
        requests=node.records,
        transcript=transcript,
        fixtureSettled=settled,
        scratchSettled=not root.exists(),
    )
    write_json(output / (mode + ".json"), record)
    return record


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    root = evidence_directory(args.output)
    output = Path(tempfile.mkdtemp(prefix="message-rpc-attempt-", dir=root))
    modes = (
        "success",
        "watch-only",
        "schema",
        "schema-type",
        "fresh-watch",
        "locked",
        "not-owned",
        "bad-reply",
        "changed-genesis",
        "preflight-failed",
        "uncertain",
        "late",
    )
    cases = [run_mode(args, output, mode) for mode in modes]
    passed = [
        c["id"]
        for c in cases
        if c["exit"] == 0
        and c["transcript"]
        and c["fixtureSettled"]
        and c["scratchSettled"]
    ]
    report = dict(
        observedUTC=now(),
        selected=len(modes),
        executed=len(cases),
        passed=len(passed),
        failed=len(cases) - len(passed),
        skipped=0,
        caseIDs=passed,
        binary=binary_identity(args.binary),
        cases=cases,
    )
    write_json(output / "report.json", report)
    print(f"Message RPC evidence: {output}")
    print(
        json.dumps(
            {
                key: report[key]
                for key in ("selected", "executed", "passed", "failed", "skipped")
            }
        )
    )
    return int(len(passed) != len(modes))


if __name__ == "__main__":
    raise SystemExit(main())
