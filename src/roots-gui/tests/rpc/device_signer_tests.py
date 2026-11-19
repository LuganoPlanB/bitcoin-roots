# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying COPYING file.
"""Owned Linux loopback external-signer RPC emulator and native viewer runner."""

import argparse
import base64
from datetime import datetime, timezone
import hashlib
import hmac
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
from urllib.parse import quote

TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))
sys.path.insert(0, str(TESTS / "protocol"))
sys.path.insert(0, str(TESTS / "platform"))
from capture_manifest import encode_capture
from rpc.fake_http import FakeHttp, Step
from signature_exchange import (
    KEYS,
    ORDER,
    OPAQUE,
    integer,
    packed,
    sha2,
    sign,
    transaction,
    vector,
)
from support.runtime import (
    binary_identity,
    evidence_directory,
    validated_scratch,
    write_json,
)

GENESIS = "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206"
WALLET = "usb fixture & test"
WALLET_PATH = "/wallet/" + quote(WALLET, safe="")
WALLET_INFO = dict(
    walletname=WALLET,
    descriptors=True,
    private_keys_enabled=False,
    external_signer=True,
    scanning=False,
)
METHODS = {
    "getblockhash": [],
    "getwalletinfo": [],
    "getaddressinfo": ["address"],
    "enumeratesigners": [],
    "listdescriptors": ["private"],
    "walletdisplayaddress": ["address"],
    "walletprocesspsbt": ["psbt", "sign", "finalize", "bip32derivs", "sighashtype"],
}
SCANS = (
    "scan-absent",
    "scan-rejected",
    "scan-unconfigured",
    "scan-empty",
    "scan-no-match",
    "scan-multiple",
    "scan-duplicate",
    "scan-malformed",
    "scan-too-many",
    "scan-limit",
    "scan-cancel",
)
DISPLAYS = (
    "display",
    "display-injected",
    "display-mismatch",
    "display-locked",
    "display-passphrase",
    "display-failure",
    "display-disconnect",
    "display-changed-signer",
    "display-foreign-wallet",
    "display-foreign-network",
    "display-cancel",
    "display-privacy",
    "display-target-change",
)
SIGNS = (
    "sign",
    "sign-unapproved",
    "sign-stale",
    "sign-ordinary",
    "sign-wrong-transaction",
    "sign-invalid",
    "sign-disconnect",
    "sign-changed-policy",
    "sign-cancel",
    "sign-privacy",
    "sign-profile",
)
HELD = {
    "scan-cancel",
    "display-cancel",
    "display-privacy",
    "display-target-change",
    "sign-cancel",
    "sign-privacy",
    "sign-profile",
}
NO_ACTION = {"display-injected", "sign-unapproved", "sign-stale", "sign-ordinary"}


def utc():
    return datetime.now(timezone.utc).isoformat()


def hash160(data):
    return hashlib.new("ripemd160", hashlib.sha256(data).digest()).digest()


def base58check(data):
    alphabet = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"
    data += sha2(data)[:4]
    value = int.from_bytes(data, "big")
    output = ""
    while value:
        value, digit = divmod(value, 58)
        output = alphabet[digit] + output
    return "1" * (len(data) - len(data.lstrip(b"\0"))) + output


def public_key(openssl, key_file, scalar):
    # Fixed disposable test scalars. These are never used with an actual wallet.
    key_file.write_bytes(
        bytes.fromhex("302e0201010420")
        + scalar.to_bytes(32, "big")
        + bytes.fromhex("a00706052b8104000a")
    )
    key_file.chmod(0o600)
    return subprocess.run(
        [
            openssl,
            "ec",
            "-inform",
            "DER",
            "-in",
            str(key_file),
            "-pubout",
            "-outform",
            "DER",
            "-conv_form",
            "compressed",
        ],
        capture_output=True,
        check=True,
        timeout=5,
    ).stdout


def child(pub, chain, scalar, index):
    data = hmac.digest(chain, pub + index.to_bytes(4, "big"), "sha512")
    return (scalar + int.from_bytes(data[:32], "big")) % ORDER, data[32:]


def fixtures(folder, openssl):
    parent, chain = KEYS[0], sha2(b"disposable external signer test")
    fingerprint = hash160(parent)[:4]
    tpub = base58check(bytes.fromhex("043587cf") + bytes(9) + chain + parent)
    branch_scalar, branch_chain = child(parent, chain, 1, 0)
    key_file = folder / "synthetic.der"
    branch_pub = public_key(openssl, key_file, branch_scalar)[-33:]
    scalar, _ = child(branch_pub, branch_chain, branch_scalar, 0)
    pub_der = public_key(openssl, key_file, scalar)
    pub = pub_der[-33:]
    fp = fingerprint.hex()
    descriptor = f"wpkh([{fp}/0/0]{pub.hex()})"
    branch = f"wpkh([{fp}]{tpub}/0/*)"
    script = b"\x00\x14" + hash160(pub)
    previous = transaction(
        [bytes(32) + b"\xff" * 4], [(25_000, script)], 0, b"\x51\x51"
    )
    point = sha2(previous) + integer(0)
    outputs = [(23_000, b"\x00\x14" + hash160(KEYS[2]))]
    unsigned = transaction([point], outputs)
    preimage = (
        integer(2)
        + sha2(point)
        + sha2(integer(0xFFFFFFFD))
        + point
        + vector(b"\x76\xa9\x14" + hash160(pub) + b"\x88\xac")
        + integer(25_000, 8)
        + integer(0xFFFFFFFD)
        + sha2(integer(outputs[0][0], 8) + vector(outputs[0][1]))
        + integer(0)
        + integer(1)
    )
    digest = sha2(preimage)
    signature = sign(openssl, key_file, digest)
    (folder / "fixture-pub.der").write_bytes(pub_der)
    (folder / "fixture-sig.der").write_bytes(signature[:-1])
    subprocess.run(
        [
            openssl,
            "pkeyutl",
            "-verify",
            "-pubin",
            "-inkey",
            str(folder / "fixture-pub.der"),
            "-keyform",
            "DER",
            "-sigfile",
            str(folder / "fixture-sig.der"),
        ],
        input=digest,
        capture_output=True,
        check=True,
        timeout=5,
    )
    key_file.unlink()
    rows = [
        {b"\x00": unsigned, OPAQUE: b"external-signer-opaque"},
        {
            b"\x00": previous,
            b"\x01": integer(25_000, 8) + vector(script),
            b"\x06" + pub: fingerprint + integer(0) + integer(0),
        },
        {},
    ]
    original = packed(rows)
    rows[1][b"\x02" + pub] = signature
    signed = packed(rows)
    rows[0][b"\x00"] = transaction([point], [(22_999, outputs[0][1])])
    wrong = packed(rows)
    rows[0][b"\x00"] = unsigned
    rows[1][b"\x02" + pub] = (
        signature[:-5] + bytes([signature[-5] ^ 1]) + signature[-4:]
    )
    invalid = packed(rows)
    (folder / "original.psbt").write_bytes(original)
    (folder / "signed.psbt").write_bytes(signed)
    data = dict(
        fingerprint=fp,
        descriptor=descriptor,
        branch=branch,
        script=script.hex(),
        digest=digest.hex(),
        original=base64.b64encode(original).decode(),
        signed=base64.b64encode(signed).decode(),
        wrong=base64.b64encode(wrong).decode(),
        invalid=base64.b64encode(invalid).decode(),
    )
    write_json(folder / "fixture.json", data)
    return data


class Scenario:
    def __init__(self, name, data):
        self.name, self.data = name, data
        self.steps = []
        self.transcript = []
        self.held = None
        self.device = {
            "fingerprint": data["fingerprint"],
            "name": "Controlled signer emulator",
        }
        self.descriptors = dict(
            wallet_name=WALLET,
            descriptors=[
                dict(desc=data["branch"], active=True, internal=False, range=[0, 30])
            ],
        )

    def add(
        self,
        method,
        result=None,
        *,
        wallet=False,
        params=None,
        error=None,
        message=None,
        raw=None,
        disconnect=False,
        hold=False,
    ):
        scope = WALLET_PATH if wallet else "/"
        expected = [] if params is None else params

        def check(path, body, _):
            assert path == scope and body["params"] == expected
            self.transcript.append(
                dict(
                    path=path,
                    request=body,
                    expectedResult=result,
                    error=error,
                    errorMessage=message,
                    transportDrop=disconnect,
                    rawReplyBytes=len(raw or b""),
                )
            )

        if message is not None:
            # Typed backend errors are scripted as bounded JSON with the RPC id
            # supplied by the checked request. Tests never retain auth headers.
            def with_error(path, body, headers):
                check(path, body, headers)
                step.raw = json.dumps(
                    dict(
                        jsonrpc="2.0",
                        id=body["id"],
                        error=dict(code=error, message=message),
                    )
                ).encode()

            validator = with_error
        else:
            validator = check
        step = Step(
            method,
            result,
            request_check=validator,
            error=error,
            disconnect=disconnect,
            raw=raw,
            gate=threading.Event() if hold else None,
        )
        self.steps.append(step)
        if hold:
            self.held = step
        return step

    def wallet(self, wrong=False):
        self.add("getblockhash", GENESIS, params=[0])
        self.add(
            "getwalletinfo",
            dict(WALLET_INFO, walletname="foreign") if wrong else WALLET_INFO,
            wallet=True,
        )

    def scan(self, name="match"):
        self.wallet()
        desc = self.descriptors
        if name == "policy-change":
            desc = dict(
                wallet_name=WALLET,
                descriptors=[
                    dict(
                        desc=desc["descriptors"][0]["desc"],
                        active=True,
                        internal=False,
                        range=[0, 29],
                    )
                ],
            )
        self.add("listdescriptors", desc, wallet=True, params={"private": False})
        variants = {
            "empty": [],
            "no-match": [dict(self.device, fingerprint="12345678")],
            "duplicate": [self.device, dict(self.device, name="second alias")],
            "multiple": [self.device, dict(self.device, fingerprint="12345678")],
            "malformed": [dict(self.device, fingerprint="12345")],
            "too-many": [dict(self.device, fingerprint=f"{i:08x}") for i in range(17)],
        }
        self.add(
            "enumeratesigners",
            dict(signers=variants.get(name, [self.device])),
            error=-32601
            if name == "rejected"
            else -1
            if name == "unconfigured"
            else None,
            message="Error: restart bitcoind with -signer=<cmd>"
            if name == "unconfigured"
            else None,
            raw=b" " * 17000 if name == "limit" else None,
            hold=name == "cancel",
        )

    def display(self, name, address, fixed, *, hold=False):
        if name == "foreign-network":
            self.add("getblockhash", "1" * 64, params=[0])
            return
        self.wallet(wrong=name == "foreign-wallet")
        if name == "foreign-wallet":
            return
        params = dict(address=address)
        self.add(
            "getaddressinfo",
            dict(
                ismine=True,
                address=address,
                scriptPubKey=self.data["script"],
                desc=fixed,
                hdkeypath="m/0/0",
            ),
            wallet=True,
            params=params,
        )
        self.scan()
        messages = {
            "locked": "Device is locked",
            "passphrase": "Passphrase required",
            "failure": "backend-private-reason",
        }
        self.add(
            "walletdisplayaddress",
            dict(address="foreign" if name == "mismatch" else address),
            wallet=True,
            params=params,
            error=-1 if name in messages else None,
            message="Signer returned error: " + messages[name]
            if name in messages
            else None,
            disconnect=name == "disconnect",
            hold=hold or "display-" + name in HELD,
        )
        if name in ("ok", "changed-signer"):
            self.scan("empty" if name == "changed-signer" else "match")

    def signer(self, name):
        self.wallet()
        self.scan()
        signed = self.data.get(
            {"wrong-transaction": "wrong", "invalid": "invalid"}.get(name, "signed")
        )
        self.add(
            "walletprocesspsbt",
            dict(psbt=signed, complete=True, hex="ignored"),
            wallet=True,
            params=dict(
                psbt=self.data["original"], sign=True, finalize=False, bip32derivs=False
            ),
            disconnect=name == "disconnect",
            hold="sign-" + name in HELD,
        )
        if name in ("ok", "changed-policy"):
            self.scan("policy-change" if name == "changed-policy" else "match")


def observe(argv, log, timeout=24, env=None, held=None):
    started = utc()
    cwd = str(Path.cwd())
    if held:
        with subprocess.Popen(
            argv,
            text=True,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        ) as proc:
            try:
                assert held.entered.wait(5), "held device RPC did not start"
                stdout, stderr = proc.communicate("retire\n", timeout=8)
            finally:
                held.gate.set()
                if proc.poll() is None:
                    proc.kill()
                    proc.communicate(timeout=3)
            code = proc.returncode
    else:
        proc = subprocess.run(
            argv, capture_output=True, text=True, timeout=timeout, env=env
        )
        stdout, stderr, code = proc.stdout, proc.stderr, proc.returncode
    log.with_suffix(".stdout").write_text(stdout)
    log.with_suffix(".stderr").write_text(stderr)
    return dict(
        argv=argv,
        cwd=cwd,
        startedUTC=started,
        completedUTC=utc(),
        exit=code,
        stdout=stdout,
        stderr=stderr,
    )


def scenario(name, args, scratch, output, data, address, fixed):
    scripted = Scenario(name, data)
    if name.startswith("scan-"):
        if name != "scan-absent":
            scripted.scan(name[5:])
    else:
        scripted.scan()
        if name not in NO_ACTION:
            if name.startswith("display"):
                scripted.display(name[8:] or "ok", address, fixed)
            else:
                scripted.signer(name[5:] or "ok")
    with FakeHttp(scripted.steps) as server:
        cookie = scratch / "cookie"
        cookie.write_text("fixture:" + server.secret)
        cookie.chmod(0o600)
        argv = [
            str(args.binary),
            name,
            str(server.port),
            str(cookie),
            str(scratch / "original.psbt"),
            data["descriptor"],
        ]
        receipt = observe(argv, output / name, held=scripted.held)
        assert server.secret not in receipt["stdout"] + receipt["stderr"]
        assert "backend-private-reason" not in receipt["stdout"] + receipt["stderr"]
    expected = [step.method for step in scripted.steps]
    assert not server.active and not server.thread.is_alive()
    write_json(
        output / (name + ".rpc.json"),
        dict(
            expected=expected, actual=scripted.transcript, socketRecords=server.records
        ),
    )
    assert [entry["request"]["method"] for entry in scripted.transcript] == expected, (
        name,
        scripted.transcript,
    )
    assert not server.check_failures and receipt["exit"] == 0, (name, receipt)
    return dict(
        id=name,
        receipt=receipt,
        result=json.loads(receipt["stdout"]),
        resourcesSettled=True,
    )


def native(args, scratch, output, data, address, fixed):
    scripted = Scenario("native-device-signers", data)
    scripted.add("getnetworkinfo", {"version": 320000})
    scripted.add(
        "getblockchaininfo",
        dict(chain="regtest", blocks=1, initialblockdownload=False, pruned=False),
    )
    scripted.add("getblockhash", GENESIS, params=[0])
    scripted.add("help", "Controlled external signer RPC fixture")
    scripted.add(
        "getopenrpcinfo",
        dict(
            openrpc="1.3.2",
            methods=[
                dict(name=name, params=[dict(name=arg, schema={}) for arg in params])
                for name, params in METHODS.items()
            ],
        ),
    )
    scripted.add("getwalletinfo", WALLET_INFO, wallet=True)
    scripted.add(
        "listdescriptors", scripted.descriptors, wallet=True, params={"private": False}
    )
    scripted.scan()
    scripted.display("ok", address, fixed, hold=True)
    scripted.display("mismatch", address, fixed)
    with FakeHttp(scripted.steps) as server:
        cookie = scratch / "cookie"
        cookie.write_text("fixture:" + server.secret)
        cookie.chmod(0o600)
        marker = output / "device-confirmation.semantics.txt"

        def release_after_capture():
            deadline = time.monotonic() + 32
            while not scripted.held.gate.is_set() and time.monotonic() < deadline:
                if marker.exists():
                    scripted.held.gate.set()
                    break
                time.sleep(0.01)

        release = threading.Thread(
            target=release_after_capture, name="owned-confirmation-gate"
        )
        release.start()
        try:
            argv = [
                sys.executable,
                "-B",
                str(TESTS / "platform/owned_display.py"),
                "--output",
                str(output / "display"),
                "--timeout",
                "29",
                "--",
                str(args.native),
                str(output),
                str(args.fonts),
                str(scratch),
                str(server.port),
                str(cookie),
                fixed,
            ]
            receipt = observe(argv, output / "native", timeout=33)
        finally:
            scripted.held.gate.set()
            release.join(timeout=2)
        assert (
            not release.is_alive()
            and server.secret not in receipt["stdout"] + receipt["stderr"]
        )
    expected = [step.method for step in scripted.steps]
    write_json(
        output / "native.rpc.json",
        dict(
            expected=expected, actual=scripted.transcript, socketRecords=server.records
        ),
    )
    assert [
        item["request"]["method"] for item in scripted.transcript
    ] == expected and not server.check_failures
    assert receipt["exit"] == 0 and marker.exists(), receipt
    renderer = json.loads((output / "display/processes.json").read_text())
    assert renderer["child"]["settled"] and all(
        p["settled"] and not p.get("remainingLiveGroupMembers")
        for p in renderer["processes"]
    )
    assert not server.active and not server.thread.is_alive()
    for name in (
        "device-selection",
        "device-confirmation",
        "device-address-echo",
        "device-address-rejected",
    ):
        encode_capture(output / (name + ".ppm"))
    result = json.loads((output / "native-result.json").read_text())
    assert result["actualNativeFrames"] == 4 and result["settled"]
    return dict(
        id="device-selection-and-confirmation",
        receipt=receipt,
        result=result,
        resourcesSettled=True,
    )


def main():
    parse = argparse.ArgumentParser(description=__doc__)
    for key in ("binary", "output", "openssl"):
        parse.add_argument("--" + key, type=Path, required=True)
    parse.add_argument("--native", type=Path)
    parse.add_argument("--fonts", type=Path)
    args = parse.parse_args()
    out = Path(tempfile.mkdtemp(prefix="attempt-", dir=evidence_directory(args.output)))
    results = []
    names = (
        ("device-selection-and-confirmation",)
        if args.native
        else SCANS + DISPLAYS + SIGNS
    )
    with tempfile.TemporaryDirectory(
        prefix="rpc-device-signers-", dir=validated_scratch()
    ) as private:
        scratch = Path(private)
        data = fixtures(scratch, str(args.openssl))
        normalization = observe(
            [str(args.binary), "target", data["descriptor"], data["branch"]],
            out / "normalize",
        )
        assert normalization["exit"] == 0, normalization
        target = json.loads(normalization["stdout"])
        data["branch"] = target["branch"]
        address, fixed = target["address"], target["descriptor"]
        for name in names:
            try:
                if args.native:
                    results.append(native(args, scratch, out, data, address, fixed))
                else:
                    results.append(
                        scenario(name, args, scratch, out, data, address, fixed)
                    )
            except Exception as error:
                results.append(dict(id=name, failed=True, diagnostic=str(error)))
        for name in (
            "original.psbt",
            "signed.psbt",
            "fixture.json",
            "fixture-pub.der",
            "fixture-sig.der",
        ):
            (out / name).write_bytes((scratch / name).read_bytes())
    failed = sum(bool(case.get("failed")) for case in results)
    report = dict(
        selected=len(names),
        executed=len(results),
        passed=len(results) - failed,
        failed=failed,
        skipped=0,
        scratchRemoved=not Path(private).exists(),
        binary=binary_identity(args.native or args.binary),
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
    print(f"Device signer evidence: {out}")
    return int(bool(failed))


if __name__ == "__main__":
    raise SystemExit(main())
