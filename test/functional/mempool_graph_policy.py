#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Native cluster policy controls, script claims and reorg exceptions."""

from decimal import Decimal

from test_framework.messages import CTxOut
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.wallet import MiniWallet


class GraphPolicyTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [["-checkmempool=1"]]

    def make_tx(self, wallet, destination, fee_rate=Decimal("0.001")):
        result = wallet.create_self_transfer(fee_rate=fee_rate)
        result["tx"].vout[0].scriptPubKey = destination.get_output_script()
        wallet.sign_tx(result["tx"])
        return result["tx"]

    def run_test(self):
        node = self.nodes[0]
        funding = MiniWallet(node)
        other = MiniWallet(node, tag_name="graph-other")
        destination = MiniWallet(node, tag_name="graph-destination")
        self.generate(other, 101)
        funding.rescan_utxos()
        other.rescan_utxos()

        for mode in ["reject", "accept", "enforce"]:
            for replacement_mode in ["0", "optin", "fee,-optin"]:
                self.restart_node(0, extra_args=[f"-mempooltruc={mode}", f"-mempoolreplacement={replacement_mode}", "-checkmempool=1"])
                tx = funding.create_self_transfer(version=3, sequence=0xffffffff)
                if mode == "reject":
                    assert_equal(node.testmempoolaccept([tx["hex"]])[0]["reject-reason"], "version")
                    # Local version rejection cannot reject a valid block.
                    self.generateblock(node, output=funding.get_address(), transactions=[tx["hex"]])
                    block_hash = node.getbestblockhash()
                    node.invalidateblock(block_hash)
                    assert_equal(node.getrawmempool(), [])
                    node.reconsiderblock(block_hash)
                else:
                    node.sendrawtransaction(tx["hex"])
                    # A direct non-signaling v3 conflict is replaceable in OptIn.
                    replacement = tx["tx"]
                    replacement.vout[0].nValue -= 10000
                    funding.sign_tx(replacement)
                    if replacement_mode == "0":
                        assert_raises_rpc_error(-26, "txn-mempool-conflict", node.sendrawtransaction, replacement.serialize().hex())
                    else:
                        node.sendrawtransaction(replacement.serialize().hex())
                    self.generate(funding, 1)
                funding.rescan_utxos()

            # Accept mode permits a non-TRUC parent; Enforce mode checks inheritance.
            if mode != "reject":
                parent = funding.create_self_transfer(version=2)
                node.sendrawtransaction(parent["hex"])
                child = funding.create_self_transfer(utxo_to_spend=parent["new_utxo"], version=3)
                result = node.testmempoolaccept([child["hex"]])[0]
                assert_equal(result["allowed"], mode == "accept")
                if mode == "enforce":
                    assert_equal(result["reject-reason"], "TRUC-violation")
                self.generate(funding, 1)
                funding.rescan_utxos()

        self.restart_node(0, extra_args=["-spkreuse=conflict", "-checkmempool=1"])
        change = funding.create_self_transfer()
        assert_equal(node.testmempoolaccept([change["hex"]])[0]["reject-reason"], "txn-spk-reused-change")
        twin = self.make_tx(funding, destination)
        twin.vout.append(CTxOut(1000, destination.get_output_script()))
        twin.vout[0].nValue -= 1000
        funding.sign_tx(twin)
        assert_equal(node.testmempoolaccept([twin.serialize().hex()])[0]["reject-reason"], "txn-spk-reused-twinoutputs")

        original = self.make_tx(funding, destination)
        original_id = node.sendrawtransaction(original.serialize().hex())
        # Same output script, independent input, fee-based policy replacement.
        replacement = self.make_tx(other, destination, Decimal("0.01"))
        replacement_id = node.sendrawtransaction(replacement.serialize().hex())
        assert_equal(node.getrawmempool(), [replacement_id])
        assert original_id != replacement_id
        assert_raises_rpc_error(-26, "insufficient fee", node.sendrawtransaction, original.serialize().hex())
        self.generate(funding, 1)

        # Reorg admission retains local reuse checks. A consensus-valid block
        # with conflicting output scripts is accepted; only the first original
        # returns because the second cannot pay for policy replacement.
        funding.rescan_utxos()
        other.rescan_utxos()
        first = self.make_tx(funding, destination)
        second = self.make_tx(other, destination)
        block = self.generateblock(node, output=funding.get_address(), transactions=[first.serialize().hex(), second.serialize().hex()])
        node.invalidateblock(block["hash"])
        assert_equal(node.getrawmempool(), [first.txid_hex])
        other.rescan_utxos()
        probe = self.make_tx(other, destination)
        assert_equal(node.testmempoolaccept([probe.serialize().hex()])[0]["allowed"], False)
        self.generateblock(node, output=funding.get_address(), transactions=[first.txid_hex])
        assert_equal(node.testmempoolaccept([probe.serialize().hex()])[0]["allowed"], True)

        self.restart_node(0, extra_args=["-spkreuse=allow", "-checkmempool=1"])
        assert_equal(node.testmempoolaccept([twin.serialize().hex()])[0]["allowed"], True)


if __name__ == '__main__':
    GraphPolicyTest(__file__).main()
