#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that getmempoolinfo reports the configured replacement policy.

-mempoolreplacement and -mempoolfullrbf select whether conflicting
transactions are never replaced, replaced only when the original signals
BIP125 replaceability, or always replaced by fee. The "fullrbf" field must
match the policy the node enforces.
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

# (extra_args, fullrbf, non-signaling original replaceable, signaling original replaceable)
POLICIES = [
    ([], True, True, True),
    (["-mempoolreplacement=fee,-optin"], True, True, True),
    (["-mempoolreplacement=fee,optin"], False, False, True),
    (["-mempoolfullrbf=0"], False, False, True),
    (["-mempoolreplacement=0"], False, False, False),
]


class MempoolRBFPolicyTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1

    def run_test(self):
        self.wallet = MiniWallet(self.nodes[0])

        for extra_args, fullrbf, nonsignaling_replaceable, signaling_replaceable in POLICIES:
            self.log.info(f"Check replacement policy with {extra_args or 'default options'}")
            self.restart_node(0, extra_args=extra_args)
            assert_equal(self.nodes[0].getmempoolinfo()["fullrbf"], fullrbf)
            self.check_replacement(sequence=SEQUENCE_FINAL, replaceable=nonsignaling_replaceable)
            self.check_replacement(sequence=MAX_BIP125_RBF_SEQUENCE, replaceable=signaling_replaceable)
            self.generate(self.nodes[0], 1)

    def check_replacement(self, *, sequence, replaceable):
        node = self.nodes[0]
        utxo = self.wallet.get_utxo()
        original = self.wallet.create_self_transfer(utxo_to_spend=utxo, sequence=sequence)
        replacement = self.wallet.create_self_transfer(utxo_to_spend=utxo, sequence=sequence, fee_rate=Decimal("0.01"))
        node.sendrawtransaction(original["hex"])
        if replaceable:
            node.sendrawtransaction(replacement["hex"])
            mempool = node.getrawmempool()
            assert original["txid"] not in mempool
            assert replacement["txid"] in mempool
        else:
            assert_raises_rpc_error(-26, "txn-mempool-conflict", node.sendrawtransaction, replacement["hex"])
            assert original["txid"] in node.getrawmempool()


if __name__ == '__main__':
    MempoolRBFPolicyTest(__file__).main()
