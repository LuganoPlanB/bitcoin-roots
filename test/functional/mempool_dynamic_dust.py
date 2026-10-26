#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Dynamic dust startup, publication, immutable deficits, retry and shutdown."""

from decimal import Decimal
import shutil

from test_framework.messages import COIN, CTxOut
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet


class DynamicDustTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1

    def run_test(self):
        node = self.nodes[0]
        modes = ["off", "0", "target:2", "target:1008", "1e0*target:2", "mempool:1", "mempool:2147483647", "2147483.647*mempool:1"]
        for mode in modes:
            offset = node.debug_log_path.stat().st_size
            self.restart_node(0, extra_args=[f"-dustdynamic={mode}", "-dustrelayfee=0.00003000"])
            current = node.debug_log_path.read_text()[offset:]
            assert_equal(current.count("Dynamic dust updates scheduled after chainstate load"), int(mode not in ["off", "0"]))
            # Unavailable estimates preserve the configured floor at legal extremes.
            wallet = MiniWallet(node)
            tx = self.probe(wallet, value=500)
            assert_equal(node.testmempoolaccept([tx.serialize().hex()])[0]["allowed"], True)
            node.mockscheduler(901)

        self.restart_node(0, extra_args=["-dustdynamic=target:2", "-blocksonly=1"])
        node.mockscheduler(901)
        self.restart_node(0, extra_args=["-dustdynamic=1*mempool:1", "-acceptnonstdtxn=1", "-checkmempool=1"])
        wallet = MiniWallet(node)
        for _ in range(10):
            wallet.send_self_transfer(from_node=node, confirmed_only=True, fee_rate=Decimal("0.001"))
        first = self.probe(wallet, value=100)
        first_id = node.sendrawtransaction(first.serialize().hex())
        first_fee = node.getmempoolentry(first_id)["fees"]
        assert_equal(first_fee["base"], Decimal("0.001"))
        assert_equal(first_fee["modified"], Decimal(100000 - 230) / COIN)
        node.prioritisetransaction(first_id, 0, 987)
        first_fee = node.getmempoolentry(first_id)["fees"]
        self.restart_node(0, extra_args=["-dustdynamic=1*mempool:1", "-acceptnonstdtxn=1", "-checkmempool=1"])
        self.wait_until(lambda: node.getmempoolinfo()["loaded"])
        assert_equal(node.getmempoolentry(first_id)["fees"], first_fee)
        wallet.rescan_utxos()
        with node.assert_debug_log(["Updating dust feerate to 100.000 sat/vB"]):
            node.mockscheduler(901)
        assert_equal(node.getmempoolentry(first_id)["fees"], first_fee)
        second = self.probe(wallet, value=100)
        second_id = node.sendrawtransaction(second.serialize().hex())
        assert_equal(node.getmempoolentry(second_id)["fees"]["modified"], Decimal(100000 - 10900) / COIN)
        # Priority/residual templates retain immutable deficits after publication,
        # but their fee fields and coinbase reward use actual fees.
        template = node.getblocktemplate({"rules": ["segwit"]})
        fees = {entry["txid"]: entry["fee"] for entry in template["transactions"]}
        assert_equal(fees[first_id], 100000)
        assert_equal(fees[second_id], 100000)
        subsidy = Decimal(50) / (2 ** ((node.getblockcount() + 1) // 150))
        expected_reward = subsidy + Decimal(sum(fees.values())) / COIN
        assert_equal(Decimal(template["coinbasevalue"]) / COIN, expected_reward)
        self.generate(wallet, 1)
        mined = node.getblock(node.getbestblockhash(), 2)
        assert first_id in [entry["txid"] for entry in mined["tx"]]
        assert second_id in [entry["txid"] for entry in mined["tx"]]
        assert_equal(sum(output["value"] for output in mined["tx"][0]["vout"]), expected_reward)
        with node.assert_debug_log(["Updating dust feerate to 3.000 sat/vB"]):
            node.mockscheduler(901)
        wallet.rescan_utxos()
        probe = self.probe(wallet, value=100)
        node.sendrawtransaction(probe.serialize().hex())
        self.generate(wallet, 1)

        # Exercise the existing production retry branch: its first mempool is
        # destroyed before any dynamic-dust task is registered.
        self.stop_node(0)
        shutil.rmtree(node.blocks_path / "index")
        offset = node.debug_log_path.stat().st_size
        self.start_node(0, extra_args=["-dustdynamic=mempool:1", "-test=reindex_after_failure_noninteractive_yes"])
        current = node.debug_log_path.read_text()[offset:]
        assert_equal(current.count("Dynamic dust updates scheduled after chainstate load"), 1)
        node.mockscheduler(901)
        self.restart_node(0, extra_args=["-dustdynamic=target:2"])
        node.mockscheduler(901)
        # Warm the actual long-horizon estimator with individually confirmed
        # transactions; then verify node scheduling publishes its raw estimate.
        wallet = MiniWallet(node)
        for _ in range(32):
            for _ in range(10):
                wallet.send_self_transfer(from_node=node, confirmed_only=True, fee_rate=Decimal("0.001"))
            self.generate(wallet, 1)
            wallet.rescan_utxos()
        estimate = node.estimaterawfee(2, 0.8)["long"]["feerate"]
        expected = estimate * COIN * 3 / 1000
        assert expected > 3
        with node.assert_debug_log([f"Updating dust feerate to {expected:.3f} sat/vB"]):
            node.mockscheduler(901)
        # The standardness snapshot must see publication too, while consensus
        # accepts a block containing the locally rejected transaction.
        probe = self.probe(wallet, value=500)
        result = node.testmempoolaccept([probe.serialize().hex()])[0]
        assert_equal(result["reject-reason"], "dust-nonanchor")
        self.generateblock(node, wallet.get_address(), [probe.serialize().hex()])

    def probe(self, wallet, value):
        transfer = wallet.create_self_transfer(confirmed_only=True, fee=Decimal("0.001"))
        tx = transfer["tx"]
        tx.vout[0].nValue -= value
        tx.vout.insert(0, CTxOut(value, wallet.get_output_script()))
        wallet.sign_tx(tx)
        return tx


if __name__ == '__main__':
    DynamicDustTest(__file__).main()
