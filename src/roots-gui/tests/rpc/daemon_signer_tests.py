# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Drive the approved daemon signer through owned loopback RPC responses."""

import argparse
import base64
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from rpc.fake_http import FakeHttp, Step
from rpc.funding_runner import encode_maps, maps
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)

GENESIS = "1" * 64
WALLET = "signing & test"
SECRET = 'signrpcfixture"\\'
HELD = ("late-epoch", "switch-node", "cancel")
NO_OPERATION = (
    "unreviewed",
    "review-only",
    "stale-source",
    "stale-epoch",
    "watch-only",
    "bad-sighash",
)
SELECTED = (
    NO_OPERATION
    + (
        "locked",
        "wrong-wallet",
        "wrong-network",
        "changed-watch-only",
        "enrich",
        "enrich-signature",
        "signed",
        "malformed",
        "wrong-transaction",
        "invalid-signature",
        "final-fields",
        "unlock",
        "unlock-rejected",
        "lost-response",
        "timeout",
    )
    + HELD
)


class TimeoutGate(threading.Event):
    def wait(self, _limit=None):
        return super().wait(19)


def steps_for(name, unsigned, signed, alternatives, checks):
    def validate(scope, params):
        def verify(path, body, _headers):
            assert path == scope
            assert body["params"] == params
            checks.append(body["method"])

        return verify

    if name in NO_OPERATION:
        return []
    wallet_path = "/wallet/signing%20%26%20test"
    genesis = "2" * 64 if name == "wrong-network" else GENESIS
    steps = [Step("getblockhash", genesis, request_check=validate("/", [0]))]
    if name == "wrong-network":
        return steps
    wallet = dict(
        walletname="another wallet" if name == "wrong-wallet" else WALLET,
        descriptors=True,
        private_keys_enabled=name != "changed-watch-only",
        external_signer=False,
    )
    if name in ("locked", "unlock", "unlock-rejected"):
        wallet["unlocked_until"] = 0
    steps.append(Step("getwalletinfo", wallet, request_check=validate(wallet_path, [])))
    if name in ("locked", "wrong-wallet", "changed-watch-only"):
        return steps
    if name in ("unlock", "unlock-rejected"):
        steps.append(
            Step(
                "walletpassphrase",
                None,
                error=-14 if name.endswith("rejected") else None,
                request_check=validate(
                    wallet_path, dict(passphrase=SECRET, timeout=30)
                ),
            )
        )
        return steps
    enrich = name in ("enrich", "enrich-signature")
    response = unsigned if name == "enrich" else alternatives.get(name, signed)
    steps.append(
        Step(
            "walletprocesspsbt",
            dict(psbt=response, complete=True, hex="does not confer authority"),
            disconnect=name == "lost-response",
            gate=TimeoutGate()
            if name == "timeout"
            else threading.Event()
            if name in HELD
            else None,
            request_check=validate(
                wallet_path,
                dict(
                    psbt=unsigned, sign=not enrich, finalize=False, bip32derivs=enrich
                ),
            ),
        )
    )
    return steps


def scenario(binary, scratch, name, unsigned, signed, descriptor, alternatives):
    observed = []
    steps = steps_for(name, unsigned, signed, alternatives, observed)
    with FakeHttp(steps) as server:
        cookie = scratch / "cookie"
        cookie.write_text("fixture:" + server.secret)
        cookie.chmod(0o600)
        argv = [
            str(binary),
            name,
            str(server.port),
            str(cookie),
            str(scratch / "original.psbt"),
            descriptor,
        ]
        if name in HELD:
            with subprocess.Popen(
                argv,
                text=True,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            ) as child:
                try:
                    assert steps[-1].entered.wait(3), (
                        "Held request did not reach the server."
                    )
                    stdout, stderr = child.communicate("retire\n", timeout=5)
                finally:
                    steps[-1].gate.set()
                    if child.poll() is None:
                        child.kill()
                        child.communicate(timeout=3)
                code = child.returncode
        else:
            child = subprocess.run(argv, text=True, capture_output=True, timeout=24)
            stdout, stderr, code = child.stdout, child.stderr, child.returncode
        assert SECRET not in stdout + stderr and server.secret not in stdout + stderr
        assert code == 0 and not stderr, (name, code, stderr)
        result = json.loads(stdout)
        assert result["settled"] and result["admitted"] == (
            name not in NO_OPERATION[:-1]
        ), (name, result)
        assert not server.check_failures and observed == [
            step.method for step in steps
        ], (name, observed)
        if name in HELD:
            assert result["retired"]
        elif result["admitted"]:
            assert result["success"] == (name in ("enrich", "signed", "unlock")), (
                name,
                result,
            )
            assert result["psbt"] == (name in ("enrich", "signed"))
            expected = {
                "bad-sighash": "sighash",
                "locked": "locked",
                "wrong-wallet": "descriptor wallet",
                "wrong-network": "network",
                "changed-watch-only": "private keys",
                "enrich-signature": "unexpected signature",
                "malformed": "malformed",
                "wrong-transaction": "amount",
                "invalid-signature": "does not validate",
                "final-fields": "final scripts",
                "unlock-rejected": "rejected",
                "lost-response": "uncertain",
                "timeout": "uncertain",
            }
            if name in expected:
                assert expected[name] in result["status"], (name, result)
    assert not server.active and not server.thread.is_alive()
    return dict(
        id=name,
        exit=code,
        observedRequests=observed,
        result=result,
        resourcesSettled=True,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("binary", "vectors", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    args = parser.parse_args()
    out = Path(tempfile.mkdtemp(prefix="attempt-", dir=evidence_directory(args.output)))
    vector = json.loads(args.vectors.read_text())["finalizer"][2]
    rows = maps(base64.b64decode(vector["psbt"]))
    # The pinned vector permits PSBT edits. Fix the container before review;
    # this flag is outside the signed transaction.
    rows[0][b"\x06"] = b"\x00"
    signed = encode_maps(rows)
    original = [dict(row) for row in rows]
    (sig,) = (key for key in original[1] if key[0] == 2)
    original[1].pop(sig)
    unsigned = encode_maps(original)
    wrong = [dict(row) for row in rows]
    wrong[-1][b"\x03"] = (int.from_bytes(wrong[-1][b"\x03"], "little") - 1).to_bytes(
        8, "little"
    )
    invalid = [dict(row) for row in rows]
    signature = invalid[1][sig]
    invalid[1][sig] = signature[:-3] + bytes([signature[-3] ^ 1]) + signature[-2:]
    final = [dict(row) for row in rows]
    final[1][b"\x07"] = b"\x51"
    alternatives = {
        "malformed": "malformed",
        "wrong-transaction": encode_maps(wrong),
        "invalid-signature": encode_maps(invalid),
        "final-fields": encode_maps(final),
    }
    results = []
    with tempfile.TemporaryDirectory(
        prefix="rpc-daemon-signer-", dir=validated_scratch()
    ) as private:
        scratch = Path(private)
        (scratch / "original.psbt").write_bytes(base64.b64decode(unsigned))
        for name in SELECTED:
            try:
                results.append(
                    scenario(
                        args.binary,
                        scratch,
                        name,
                        unsigned,
                        signed,
                        "wpkh(" + sig[1:].hex() + ")",
                        alternatives,
                    )
                )
            except Exception as error:
                results.append(dict(id=name, failed=True, diagnostic=str(error)))
    failed = sum(result.get("failed", False) for result in results)
    report = dict(
        selected=len(SELECTED),
        executed=len(results),
        passed=len(results) - failed,
        failed=failed,
        skipped=0,
        scratchRemoved=not Path(private).exists(),
        binary=binary_identity(args.binary),
        cases=results,
    )
    write_json(out / "result.json", report)
    print(
        json.dumps(
            {
                k: report[k]
                for k in ("selected", "executed", "passed", "failed", "skipped")
            }
        )
    )
    print(f"Daemon signer evidence: {out}")
    return int(bool(failed))


if __name__ == "__main__":
    raise SystemExit(main())
