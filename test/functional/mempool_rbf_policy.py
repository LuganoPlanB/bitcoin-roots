#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that getmempoolinfo reports the configured replacement policy.

-mempoolreplacement and -mempoolfullrbf select whether conflicting
transactions are never replaced, replaced when the original signals BIP125
replaceability or is TRUC/v3, or always replaced by fee. The "fullrbf" field
reports Always, even though OptIn also permits non-signaling TRUC conflicts.
"""

from decimal import Decimal

from test_framework.messages import (
    MAX_BIP125_RBF_SEQUENCE,
    SEQUENCE_FINAL,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)
from test_framework.wallet import MiniWallet

# (extra_args, fullrbf, non-signaling v2 replaceable, signaling original replaceable,
#  non-signaling TRUC/v3 replaceable)
POLICIES = [
    ([], True, True, True, True),
    (["-mempoolreplacement=fee,-optin"], True, True, True, True),
    (["-mempoolreplacement=fee,optin"], False, False, True, True),
    (["-mempoolfullrbf=0"], False, False, True, True),
    (["-mempoolfullrbf=1"], True, True, True, True),
    (["-mempoolreplacement=0"], False, False, False, False),
]


class MempoolRBFPolicyTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1

    def run_test(self):
        self.wallet = MiniWallet(self.nodes[0])

        for extra_args, fullrbf, nonsignaling_replaceable, signaling_replaceable, truc_replaceable in POLICIES:
            self.log.info(f"Check replacement policy with {extra_args or 'default options'}")
            self.restart_node(0, extra_args=extra_args)
            assert_equal(self.nodes[0].getmempoolinfo()["fullrbf"], fullrbf)
            for version, nonsignaling in ((2, nonsignaling_replaceable), (3, truc_replaceable)):
                self.check_replacement(version=version, sequence=SEQUENCE_FINAL, replaceable=nonsignaling)
                self.check_replacement(version=version, sequence=MAX_BIP125_RBF_SEQUENCE, replaceable=signaling_replaceable)
            self.generate(self.nodes[0], 1)

    def check_replacement(self, *, version, sequence, replaceable):
        node = self.nodes[0]
        utxo = self.wallet.get_utxo()
        original = self.wallet.create_self_transfer(utxo_to_spend=utxo, version=version, sequence=sequence)
        replacement = self.wallet.create_self_transfer(utxo_to_spend=utxo, version=version, sequence=sequence, fee_rate=Decimal("0.01"))
        node.sendrawtransaction(original["hex"])
        if replaceable:
            node.sendrawtransaction(replacement["hex"])
            mempool = node.getrawmempool()
            assert original["txid"] not in mempool
            assert replacement["txid"] in mempool
        else:
            assert_raises_rpc_error(-26, "txn-mempool-conflict", node.sendrawtransaction, replacement["hex"])
            assert original["txid"] in node.getrawmempool()
            assert replacement["txid"] not in node.getrawmempool()


if __name__ == '__main__':
    MempoolRBFPolicyTest(__file__).main()
