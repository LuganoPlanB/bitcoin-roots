# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license; see COPYING.
"""Owned, isolated regtest RPC/help probes; no signing, mining or external peers."""

import argparse
import base64
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request


class RpcError(Exception):
    def __init__(self, error):
        self.error = error
        super().__init__(str(error["code"]) + ": " + error["message"])


class Daemon:
    def __init__(self, binary, data, log, disabled):
        with socket.socket() as allocator:
            allocator.bind(("127.0.0.1", 0))
            port = allocator.getsockname()[1]
        self.url = f"http://127.0.0.1:{port}"
        self.profile = "disabled-wallet" if disabled else "wallet-pruned"
        self.data = data
        self.log = log.open("wb")
        command = [
            str(binary),
            "-regtest",
            "-server",
            "-listen=0",
            "-discover=0",
            "-dnsseed=0",
            "-connect=0",
            "-listenonion=0",
            "-natpmp=0",
            "-prune=550",
            "-printtoconsole=1",
            "-rpcbind=127.0.0.1",
            "-rpcallowip=127.0.0.1",
            f"-rpcport={port}",
            f"-datadir={data}",
        ]
        if disabled:
            command += ["-disablewallet=1"]
        self.process = subprocess.Popen(
            command, stdout=self.log, stderr=subprocess.STDOUT
        )
        self.auth = None
        self.request_id = 0

    def ready(self):
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise RuntimeError("owned daemon exited during startup")
            try:
                cookie = (self.data / "regtest/.cookie").read_bytes().strip()
                self.auth = "Basic " + base64.b64encode(cookie).decode()
                return self.rpc("getblockchaininfo")
            except (OSError, RpcError, urllib.error.URLError):
                time.sleep(0.05)
        raise RuntimeError("owned daemon startup timeout")

    def rpc(self, method, params=None, wallet=False):
        self.request_id += 1
        body = json.dumps(
            {
                "jsonrpc": "2.0",
                "id": self.request_id,
                "method": method,
                "params": {} if params is None else params,
            }
        ).encode()
        request = urllib.request.Request(
            self.url + ("/wallet/fixture-public" if wallet else ""),
            body,
            {"Authorization": self.auth, "Content-Type": "application/json"},
        )
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                payload = response.read()
        except urllib.error.HTTPError as error:
            payload = error.read()
        result = json.loads(payload)
        if result.get("error"):
            raise RpcError(result["error"])
        return result["result"]

    def close(self):
        try:
            if self.process.poll() is None and self.auth:
                try:
                    self.rpc("stop")
                except (OSError, RpcError, urllib.error.URLError):
                    pass
            if self.process.poll() is None:
                try:
                    self.process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    self.process.terminate()
                    try:
                        self.process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        self.process.kill()
                        self.process.wait(timeout=5)
        finally:
            self.log.close()
        return self.process.returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--daemon", type=Path, required=True)
    parser.add_argument("--provider-probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    context = json.loads(os.environ["GESTALT_SESSION_TMP_CONTEXT"])
    assert Path(context["directory"]).resolve() == Path(os.environ["TMPDIR"]).resolve()
    marker = json.loads(Path(context["markerPath"]).read_text())
    assert (
        marker["ownerId"] == context["ownerId"]
        and marker["workspacePath"] == context["workspacePath"]
    )
    output = args.output.resolve()
    if Path(__file__).resolve().parents[4] in output.parents:
        raise RuntimeError("RPC evidence must stay out of source")
    output.mkdir(parents=True, exist_ok=True)
    checks, help_text, observations = [], {}, {}

    def check(name, callback):
        try:
            callback()
            checks.append({"id": name, "passed": True})
        except Exception as error:
            checks.append({"id": name, "passed": False, "error": str(error)[:500]})

    def require(value):
        if not value:
            raise AssertionError("RPC contract assertion failed")

    def expected_error(node, method, params, codes):
        try:
            node.rpc(method, params)
        except RpcError as error:
            require(error.error["code"] in codes)
            observations[node.profile + "." + method + ".error"] = error.error
            return
        raise AssertionError("expected RPC error")

    for disabled in [False, True]:
        name = "disabled-wallet" if disabled else "wallet-pruned"
        with tempfile.TemporaryDirectory(prefix="rpc-provider-") as temporary:
            node = Daemon(
                args.daemon.resolve(),
                Path(temporary),
                output / (name + ".log"),
                disabled,
            )
            try:
                chain = node.ready()
                observations[name] = {
                    "chain": chain["chain"],
                    "pruned": chain["pruned"],
                    "version": node.rpc("getnetworkinfo")["version"],
                }
                check(
                    name + ".isolated-regtest-pruned",
                    lambda: require(chain["chain"] == "regtest" and chain["pruned"]),
                )
                check(
                    name + ".unsupported-method",
                    lambda: expected_error(
                        node, "roots_gui_nonexistent_method", {}, [-32601]
                    ),
                )
                check(
                    name + ".unsupported-argument",
                    lambda: expected_error(
                        node, "getblockchaininfo", {"bogus": True}, [-8]
                    ),
                )
                if disabled:
                    check(
                        name + ".wallet-unavailable",
                        lambda: expected_error(node, "getwalletinfo", {}, [-32601]),
                    )
                    continue
                methods = [
                    "walletprocesspsbt",
                    "walletcreatefundedpsbt",
                    "createpsbt",
                    "combinepsbt",
                    "finalizepsbt",
                    "descriptorprocesspsbt",
                    "psbtbumpfee",
                    "createwallet",
                    "restorewallet",
                    "importdescriptors",
                    "getaddressinfo",
                    "getnewaddress",
                    "listdescriptors",
                    "deriveaddresses",
                    "listunspent",
                    "listsinceblock",
                    "gettransaction",
                    "lockunspent",
                    "sendrawtransaction",
                    "testmempoolaccept",
                    "enumeratesigners",
                    "walletdisplayaddress",
                    "sweepprivkeys",
                    "exportaddresskey",
                ]
                for method in methods:
                    check(
                        "help." + method,
                        lambda method=method: help_text.update(
                            {method: node.rpc("help", {"command": method})}
                        ),
                    )
                check(
                    "wallet.none-loaded",
                    lambda: expected_error(node, "getwalletinfo", {}, [-18]),
                )
                node.rpc(
                    "createwallet",
                    {
                        "wallet_name": "fixture-public",
                        "disable_private_keys": True,
                        "blank": True,
                        "descriptors": True,
                        "load_on_startup": False,
                    },
                )
                check(
                    "wallet.watch-only",
                    lambda: require(
                        not node.rpc("getwalletinfo", wallet=True)[
                            "private_keys_enabled"
                        ]
                    ),
                )
                check(
                    "roots.public-descriptor-policy-gate",
                    lambda: expected_error(
                        node, "listdescriptors", {"private": False}, [-4, -1, -8]
                    ),
                )

                def signer():
                    try:
                        result = node.rpc("enumeratesigners")
                        require(isinstance(result["signers"], list))
                        observations["external-signer"] = result
                    except RpcError as error:
                        require("signer" in error.error["message"].lower())
                        observations["external-signer"] = error.error

                check("signer.missing-honest", signer)
                key = (
                    "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"
                )
                xpub = "xpub69H7F5d8KSRgmmdJg2KhpAK8SR3DjMwAdkxj3ZuxV27CprR9LgpeyGmXUbC6wb7ERfvrnKZjXoUmmDznezpbZb7ap6r1D3tgFxHmwMkQTPH"
                check(
                    "descriptor.reject-wrong-network-xpub",
                    lambda: expected_error(
                        node,
                        "getdescriptorinfo",
                        {"descriptor": f"wpkh({xpub}/0/*)"},
                        [-5],
                    ),
                )
                converted = subprocess.run(
                    [str(args.provider_probe.resolve()), "xpub-regtest", xpub],
                    capture_output=True,
                    text=True,
                    timeout=10,
                    check=True,
                )
                public_testnet_key = converted.stdout.strip()
                for expression in [
                    f"wpkh({key})",
                    f"sh(wpkh({key}))",
                    f"wsh(sortedmulti(1,{key}))",
                    f"tr({key[2:]})",
                    f"tr({key[2:]},pk({key[2:]}))",
                    f"wpkh({public_testnet_key}/0/*)",
                ]:

                    def compare(expression=expression):
                        descriptor = node.rpc(
                            "getdescriptorinfo", {"descriptor": expression}
                        )["descriptor"]
                        params = {"descriptor": descriptor}
                        if "*" in expression:
                            params["range"] = [0, 1]
                        expected = node.rpc("deriveaddresses", params)
                        actual = []
                        for index in range(len(expected)):
                            result = subprocess.run(
                                [
                                    str(args.provider_probe.resolve()),
                                    "descriptor-regtest",
                                    expression,
                                    str(index),
                                ],
                                capture_output=True,
                                text=True,
                                timeout=10,
                                check=True,
                            )
                            actual.append(result.stdout.split("\t")[0])
                        require(actual == expected)

                    check("descriptor-oracle." + expression, compare)
            finally:
                code = node.close()
                checks.append(
                    {"id": name + ".owned-teardown", "passed": code == 0, "exit": code}
                )
                debug = node.data / "regtest/debug.log"
                if debug.exists():
                    shutil.copyfile(debug, output / (name + "-debug.log"))
    (output / "help.json").write_text(json.dumps(help_text, indent=2) + "\n")
    report = {
        "selected": len(checks),
        "executed": len(checks),
        "passed": sum(x["passed"] for x in checks),
        "failed": sum(not x["passed"] for x in checks),
        "skipped": 0,
        "checks": checks,
        "observations": observations,
    }
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))
    return bool(report["failed"])


if __name__ == "__main__":
    raise SystemExit(main())
