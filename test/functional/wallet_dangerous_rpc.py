#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Require a command-line startup opt-in for all four restricted wallet RPCs."""

import http.client
import json
from urllib.parse import urlsplit

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.wallet_util import WalletUnlock


DISABLED = "Dangerous RPC methods are disabled; restart with --allow-dangerous-rpc"


class DangerousRPCTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.wallet_names = []

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def wallet(self):
        node = self.nodes[0]
        if "gated" not in node.listwallets():
            node.loadwallet("gated")
        return node.get_wallet_rpc("gated")

    def assert_disabled(self, address):
        wallet = self.wallet()
        assert_raises_rpc_error(-4, DISABLED, wallet.exportaddresskey, address)
        for private in [None, False, True]:
            if private is None:
                assert_raises_rpc_error(-4, DISABLED, wallet.listdescriptors)
                assert_raises_rpc_error(-4, DISABLED, wallet.gethdkeys)
                assert_raises_rpc_error(-4, DISABLED, wallet.derivehdkey, "m/0h")
            else:
                assert_raises_rpc_error(-4, DISABLED, wallet.listdescriptors, private=private)
                assert_raises_rpc_error(-4, DISABLED, wallet.gethdkeys, private=private)
                assert_raises_rpc_error(-4, DISABLED, wallet.derivehdkey, "m/0h", private=private)
        # Unrelated wallet operations remain available.
        assert_equal(wallet.getwalletinfo()["walletname"], "gated")
        assert wallet.getnewaddress()

    def assert_enabled(self, address):
        wallet = self.wallet()
        assert wallet.exportaddresskey(address)["keys"]
        for private in [False, True]:
            assert wallet.listdescriptors(private=private)["descriptors"]
            keys = wallet.gethdkeys(private=private)
            assert keys
            assert_equal("xprv" in keys[0], private)
            derived = wallet.derivehdkey("m/0h", private=private)
            assert "xpub" in derived
            assert_equal("xprv" in derived, private)

    def run_test(self):
        node = self.nodes[0]
        node.createwallet("gated")
        address = self.wallet().getnewaddress()
        self.log.info("All four methods, including public modes, are disabled by default")
        self.assert_disabled(address)

        config = node.datadir_path / "bitcoin.conf"
        original_config = config.read_text()
        self.stop_node(0)
        config.write_text(original_config + "\nallow-dangerous-rpc=1\n")
        self.start_node(0)
        self.log.info("Configuration cannot enable the restricted methods")
        self.assert_disabled(address)

        self.stop_node(0)
        config.write_text(original_config)
        settings = node.chain_path / "settings.json"
        data = json.loads(settings.read_text()) if settings.exists() else {}
        data["allow-dangerous-rpc"] = True
        settings.write_text(json.dumps(data))
        self.start_node(0)
        self.log.info("Persistent settings cannot enable the restricted methods")
        self.assert_disabled(address)

        self.restart_node(0, extra_args=["--allow-dangerous-rpc=0"])
        self.assert_disabled(address)
        self.restart_node(0, extra_args=["--allow-dangerous-rpc"])
        self.log.info("An explicit command-line opt-in enables all four methods")
        self.assert_enabled(address)

        # Enabling the methods does not disable HTTP authentication.
        url = urlsplit(node.url)
        connection = http.client.HTTPConnection(url.hostname, url.port, timeout=10)
        try:
            connection.request("POST", "/", json.dumps({"jsonrpc": "2.0", "id": 1, "method": "gethdkeys", "params": []}))
            assert_equal(connection.getresponse().status, 401)
        finally:
            connection.close()

        wallet = self.wallet()
        wallet.encryptwallet("synthetic-passphrase")
        self.log.info("Wallet unlock remains mandatory for private-key access")
        assert_raises_rpc_error(-13, "walletpassphrase", wallet.exportaddresskey, address)
        assert_raises_rpc_error(-13, "walletpassphrase", wallet.listdescriptors, private=True)
        assert_raises_rpc_error(-13, "walletpassphrase", wallet.gethdkeys, private=True)
        assert_raises_rpc_error(-13, "walletpassphrase", wallet.derivehdkey, "m/0h", private=True)
        with WalletUnlock(wallet, "synthetic-passphrase"):
            self.assert_enabled(address)

        self.restart_node(0)
        self.log.info("The opt-in is not persisted across a restart without the flag")
        self.assert_disabled(address)
        self.stop_node(0)
        node.assert_start_raises_init_error(
            extra_args=["--allow-dangerous-rpc=2"],
            expected_msg="Error: Invalid -allow-dangerous-rpc value (expected 0 or 1)",
        )
        self.start_node(0, extra_args=["--allow-dangerous-rpc=1"])
        with WalletUnlock(self.wallet(), "synthetic-passphrase"):
            self.assert_enabled(address)


if __name__ == "__main__":
    DangerousRPCTest(__file__).main()
