#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Roots local template resource boundaries and coin-age priority selection."""
from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet


class RootsTemplateTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [["-blockprioritysize=0", "-blockmintxfee=0.00002"]]

    def run_test(self):
        node = self.nodes[0]
        wallet = MiniWallet(node)
        self.generate(wallet, 101)
        tx = wallet.send_self_transfer(from_node=node, fee_rate=Decimal("0.00001"))
        txid = tx["txid"]
        size = len(bytes.fromhex(tx["hex"]))
        weight = tx["tx"].get_weight()

        def check_template(args, included):
            self.restart_node(0, extra_args=args)
            entries = node.getblocktemplate({"rules": ["segwit"]})["transactions"]
            assert_equal([entry["txid"] for entry in entries], [txid] if included else [])
            if included:
                assert_equal(entries[0]["fee"], int(tx["fee"] * 100_000_000))

        # Default/disabled priority cannot bypass the explicitly higher fee floor.
        check_template(["-blockprioritysize=0", "-blockmintxfee=0.00002"], False)
        check_template(["-blockprioritysize=100000", "-blockmintxfee=0.00002"], True)
        # Equality is excluded; one byte/weight unit above it includes the tx.
        common = ["-blockprioritysize=0", "-blockmintxfee=0"]
        for delta, included in [(0, False), (1, True)]:
            check_template(common + [f"-blockmaxsize={1000 + size + delta}"], included)
            check_template(common + [f"-blockmaxweight={8000 + weight + delta}"], included)
        # Both caps apply together; an unconstraining one cannot override the other.
        check_template(common + [f"-blockmaxsize={1000 + size}", "-blockmaxweight=4000000"], False)
        check_template(common + ["-blockmaxsize=4000000", f"-blockmaxweight={8000 + weight}"], False)
        # A reserve exceeding the weight limit normalizes to an empty template.
        check_template(common + ["-blockmaxweight=8000", "-blockreservedweight=9000"], False)
        check_template(common + ["-blockmaxsize=0", "-blockprioritysize=9223372036854775807"], False)
        # The local size cap never changes block-connection consensus validity.
        before = node.getblockcount()
        block = self.generateblock(node, output=wallet.get_address(), transactions=[tx["hex"]])
        assert_equal(node.getblockcount(), before + 1)
        assert txid in node.getblock(block["hash"])["tx"]


if __name__ == '__main__':
    RootsTemplateTest(__file__).main()
