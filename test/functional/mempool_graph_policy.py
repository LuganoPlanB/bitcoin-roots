#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Native cluster policy controls, script claims and reorg exceptions."""

from decimal import Decimal

from test_framework.messages import COIN, SEQUENCE_FINAL, CTxOut
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

    def test_reuse_replacement_modes(self, funding, other, destination):
        node = self.nodes[0]
        for mode in ("0", "optin"):
            self.log.info("Independent-input script reuse obeys exact replacement economics under %s", mode)
            self.restart_node(0, extra_args=["-spkreuse=conflict", f"-mempoolreplacement={mode}",
                                             "-incrementalrelayfee=0.000001", "-checkmempool=1"])
            assert_equal(node.getrawmempool(), [])

            def make_reuse_tx(wallet, utxo, fee):
                tx = wallet.create_self_transfer(utxo_to_spend=utxo, fee=Decimal(fee) / COIN,
                                                 version=2, sequence=SEQUENCE_FINAL)["tx"]
                tx.vout[0].scriptPubKey = destination.get_output_script()
                wallet.sign_tx(tx)
                assert_equal(int(utxo["value"] * COIN) - sum(output.nValue for output in tx.vout), fee)
                assert all(txin.nSequence == SEQUENCE_FINAL for txin in tx.vin)
                return tx

            original_utxo = funding.get_utxo()
            original = make_reuse_tx(funding, original_utxo, 1000)
            original_id = node.sendrawtransaction(original.serialize().hex())
            original_entry = node.getmempoolentry(original_id)
            assert_equal(original_entry["fees"]["modified"], Decimal(1000) / COIN)
            direct = make_reuse_tx(funding, original_utxo, 2000)
            assert_equal(node.testmempoolaccept([direct.serialize().hex()])[0]["reject-reason"], "txn-mempool-conflict")
            assert_raises_rpc_error(-26, "txn-mempool-conflict", node.sendrawtransaction, direct.serialize().hex())
            assert_equal(node.getrawmempool(), [original_id])
            independent_utxo = other.get_utxo()
            probe = make_reuse_tx(other, independent_utxo, 1000)
            assert original.vin[0].prevout != probe.vin[0].prevout
            assert_equal(probe.get_vsize(), original_entry["vsize_adjusted"])
            rate = int(node.getmempoolinfo()["incrementalrelayfee"] * COIN)
            assert_equal(rate, 100)
            # Independent integer ceiling; no production fee-policy calculator.
            increment = (rate * probe.get_vsize() + 999) // 1000
            self.log.info("reuse mode=%s: original1000sat, vsize%d, incremental%d sat/kvB; reject%d/accept%d sat",
                          mode, probe.get_vsize(), rate, 1000 + increment - 1, 1000 + increment)
            low = make_reuse_tx(other, independent_utxo, 1000 + increment - 1)
            rejected = node.testmempoolaccept([low.serialize().hex()])[0]
            assert_equal(rejected["allowed"], False)
            assert_equal(rejected["reject-reason"], "insufficient fee")
            assert "not enough additional fees to relay" in rejected["reject-details"]
            assert_raises_rpc_error(-26, "insufficient fee", node.sendrawtransaction, low.serialize().hex())
            assert_equal(node.getrawmempool(), [original_id])
            assert_equal(node.getmempoolentry(original_id)["fees"], original_entry["fees"])

            high = make_reuse_tx(other, independent_utxo, 1000 + increment)
            assert_equal(high.get_vsize(), probe.get_vsize())
            assert_equal(node.testmempoolaccept([high.serialize().hex()])[0]["allowed"], True)
            replacement_id = node.sendrawtransaction(high.serialize().hex())
            assert_equal(node.getrawmempool(), [replacement_id])
            assert original_id != replacement_id
            assert_equal(node.getmempoolentry(replacement_id)["fees"]["base"], Decimal(1000 + increment) / COIN)
            assert_equal(node.getmempoolentry(replacement_id)["fees"]["modified"], Decimal(1000 + increment) / COIN)
            self.generate(funding, 1)
            funding.rescan_utxos()
            other.rescan_utxos()

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

        self.test_reuse_replacement_modes(funding, other, destination)
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
