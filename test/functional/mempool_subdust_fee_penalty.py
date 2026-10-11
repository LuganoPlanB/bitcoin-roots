#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test sub-dust output fee penalty (-subdustfeepenalty)."""

from decimal import Decimal
from math import ceil

from test_framework.messages import (
    COIN,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
)
from test_framework.script import (
    CScript,
    OP_RETURN,
)
from test_framework.script_util import PAY_TO_ANCHOR
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet


DUST_THRESHOLD = 330  # P2TR: (43 + 67) * 3000 / 1000 = 330 sats


class SubDustFeePenaltyTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [["-acceptnonstdtxn=1", "-subdustfeepenalty=1"]]

    def run_test(self):
        self.wallet = MiniWallet(self.nodes[0])

        info = self.nodes[0].getmempoolinfo()
        self.minrelay_per_kvb = info['minrelaytxfee'] * COIN  # sats per kvB

        self.test_dust_output_increases_required_fee()
        self.test_partial_dust_proportional_penalty()
        self.test_multiple_dust_outputs_stack()
        self.test_penalty_disabled()
        self.test_op_return_not_penalized()
        self.test_above_dust_not_penalized()
        self.test_threshold_and_low_rate_boundaries()
        self.test_anchor_and_standardness()
        self.test_carrier_cost_interaction()
        self.test_fee_delta_and_replacement()
        self.test_package_and_truc()
        self.test_mining_and_block_acceptance()

    def build_tx_with_dust(self, dust_values, fee, *, utxo=None, dust_script=None, version=2, carrier=None):
        utxo = utxo or self.wallet.get_utxo()
        tx = CTransaction()
        tx.version = version
        tx.vin = [CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]))]
        input_value = int(utxo["value"] * COIN)
        change = input_value - sum(dust_values) - fee
        assert change > 0, f"Not enough funds: input={input_value}, dust={dust_values}, fee={fee}"
        tx.vout = [CTxOut(v, dust_script or self.wallet.get_output_script()) for v in dust_values]
        if carrier is not None:
            tx.vout.append(CTxOut(0, CScript([OP_RETURN, carrier])))
        tx.vout.append(CTxOut(change, self.wallet.get_output_script()))
        self.wallet.sign_tx(tx)
        return tx

    def get_min_relay_fee(self, tx):
        """Calculate the minimum relay fee for a transaction."""
        decoded = self.nodes[0].decoderawtransaction(tx.serialize().hex())
        return int(ceil(self.minrelay_per_kvb * decoded['vsize'] / 1000))

    def test_dust_output_increases_required_fee(self):
        self.log.info("Test: sub-dust output penalizes effective fee")

        # Build a probe tx to determine the exact min relay fee
        probe = self.build_tx_with_dust([0], fee=1000)
        min_fee = self.get_min_relay_fee(probe)

        # fee just covers min relay + penalty - 1: rejected
        reject_fee = DUST_THRESHOLD + min_fee - 1
        tx_low = self.build_tx_with_dust([0], fee=reject_fee)
        result = self.nodes[0].testmempoolaccept([tx_low.serialize().hex()])
        assert_equal(result[0]["allowed"], False)
        assert_equal(result[0]["reject-reason"], "min relay fee not met")

        # fee covers min relay + penalty: accepted
        accept_fee = DUST_THRESHOLD + min_fee
        tx_high = self.build_tx_with_dust([0], fee=accept_fee)
        result = self.nodes[0].testmempoolaccept([tx_high.serialize().hex()])
        assert_equal(result[0]["allowed"], True)

    def test_partial_dust_proportional_penalty(self):
        self.log.info("Test: partial dust value gets proportional penalty")

        partial_value = 100
        penalty = DUST_THRESHOLD - partial_value  # 230 sats

        probe = self.build_tx_with_dust([partial_value], fee=1000)
        min_fee = self.get_min_relay_fee(probe)

        reject_fee = penalty + min_fee - 1
        tx_low = self.build_tx_with_dust([partial_value], fee=reject_fee)
        result = self.nodes[0].testmempoolaccept([tx_low.serialize().hex()])
        assert_equal(result[0]["allowed"], False)
        assert_equal(result[0]["reject-reason"], "min relay fee not met")

        accept_fee = penalty + min_fee
        tx_high = self.build_tx_with_dust([partial_value], fee=accept_fee)
        result = self.nodes[0].testmempoolaccept([tx_high.serialize().hex()])
        assert_equal(result[0]["allowed"], True)

    def test_multiple_dust_outputs_stack(self):
        self.log.info("Test: multiple dust outputs stack penalties")

        total_penalty = DUST_THRESHOLD * 2  # 660 sats

        probe = self.build_tx_with_dust([0, 0], fee=1000)
        min_fee = self.get_min_relay_fee(probe)

        reject_fee = total_penalty + min_fee - 1
        tx_low = self.build_tx_with_dust([0, 0], fee=reject_fee)
        result = self.nodes[0].testmempoolaccept([tx_low.serialize().hex()])
        assert_equal(result[0]["allowed"], False)
        assert_equal(result[0]["reject-reason"], "min relay fee not met")

        accept_fee = total_penalty + min_fee
        tx_high = self.build_tx_with_dust([0, 0], fee=accept_fee)
        result = self.nodes[0].testmempoolaccept([tx_high.serialize().hex()])
        assert_equal(result[0]["allowed"], True)

    def test_penalty_disabled(self):
        self.log.info("Test: penalty disabled with -subdustfeepenalty=0")
        self.restart_node(0, extra_args=["-acceptnonstdtxn=1", "-subdustfeepenalty=0"])
        self.wallet.rescan_utxos()

        probe = self.build_tx_with_dust([0], fee=1000)
        min_fee = self.get_min_relay_fee(probe)

        # Without penalty, just the min relay fee suffices
        tx = self.build_tx_with_dust([0], fee=min_fee)
        result = self.nodes[0].testmempoolaccept([tx.serialize().hex()])
        assert_equal(result[0]["allowed"], True)

    def test_op_return_not_penalized(self):
        self.log.info("Test: OP_RETURN outputs are not penalized (threshold=0)")
        self.restart_node(0, extra_args=["-acceptnonstdtxn=1", "-subdustfeepenalty=1"])
        self.wallet.rescan_utxos()

        utxo = self.wallet.get_utxo()
        tx = CTransaction()
        tx.version = 2
        tx.vin = [CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"]))]
        input_value = int(utxo["value"] * COIN)
        fee = 400
        tx.vout = [
            CTxOut(0, CScript([OP_RETURN, b"test data"])),
            CTxOut(input_value - fee, self.wallet.get_output_script()),
        ]
        self.wallet.sign_tx(tx)

        result = self.nodes[0].testmempoolaccept([tx.serialize().hex()])
        assert_equal(result[0]["allowed"], True)

    def test_above_dust_not_penalized(self):
        self.log.info("Test: outputs above dust threshold are not penalized")

        probe = self.build_tx_with_dust([DUST_THRESHOLD + 100], fee=1000)
        min_fee = self.get_min_relay_fee(probe)

        tx = self.build_tx_with_dust([DUST_THRESHOLD + 100], fee=min_fee)
        result = self.nodes[0].testmempoolaccept([tx.serialize().hex()])
        assert_equal(result[0]["allowed"], True)

    def reset_policy(self, *args):
        # Confirm previous fixtures before reloading policy; avoid repricing saved entries.
        self.generate(self.wallet, 1)
        self.restart_node(0, extra_args=["-acceptnonstdtxn=1", "-subdustfeepenalty=1", *args])
        self.wallet.rescan_utxos()

    def check_boundary(self, values, penalty, **kwargs):
        utxo = self.wallet.get_utxo()
        probe = self.build_tx_with_dust(values, 1000, utxo=utxo, **kwargs)
        min_fee = self.get_min_relay_fee(probe)
        low = self.build_tx_with_dust(values, min_fee + penalty - 1, utxo=utxo, **kwargs)
        high = self.build_tx_with_dust(values, min_fee + penalty, utxo=utxo, **kwargs)
        rejection = self.nodes[0].testmempoolaccept([low.serialize().hex()])[0]
        assert_equal(rejection["allowed"], False)
        assert_equal(rejection["reject-reason"], "min relay fee not met")
        assert_equal(self.nodes[0].testmempoolaccept([high.serialize().hex()])[0]["allowed"], True)

    def test_threshold_and_low_rate_boundaries(self):
        self.log.info("Exact threshold, per-output ceiling at one sat/kvB, and zero rate")
        self.check_boundary([DUST_THRESHOLD - 1], 1)
        self.check_boundary([DUST_THRESHOLD], 0)
        self.reset_policy("-dustrelayfee=0.00000001")
        # Each P2TR threshold rounds up separately, rather than rounding the sum.
        self.check_boundary([0, 0], 2)
        self.check_boundary([1], 0)
        self.reset_policy("-dustrelayfee=0")
        self.check_boundary([0, 0], 0)
        self.reset_policy()

    def test_anchor_and_standardness(self):
        self.log.info("P2A permission does not exempt its zero-value output from the penalty")
        # P2A is a witness program: 13-byte output + 67-byte spend estimate.
        self.check_boundary([0], 240, dust_script=PAY_TO_ANCHOR)
        self.reset_policy("-acceptnonstdtxn=0")
        tx = self.build_tx_with_dust([0], 10000, dust_script=PAY_TO_ANCHOR)
        rejection = self.nodes[0].testmempoolaccept([tx.serialize().hex()])[0]
        assert_equal(rejection["allowed"], False)
        assert_equal(rejection["reject-reason"], "dust")
        # Paying the deficit cannot bypass standard dust rejection either.
        tx = self.build_tx_with_dust([0], 10000)
        assert_equal(self.nodes[0].testmempoolaccept([tx.serialize().hex()])[0]["reject-reason"], "dust-nonanchor")
        self.reset_policy("-acceptnonstdtxn=0", "-subdustfeepenalty=0")
        assert_equal(self.nodes[0].testmempoolaccept([tx.serialize().hex()])[0]["reject-reason"], "dust-nonanchor")
        self.reset_policy()

    def test_carrier_cost_interaction(self):
        self.log.info("Carrier weight and dust deficit independently raise the required effective fee")
        payload = b"x" * 16
        self.reset_policy("-datacarriercost=1.01")  # rounds to five weight units per byte
        utxo = self.wallet.get_utxo()
        probe = self.build_tx_with_dust([0], 1000, utxo=utxo, carrier=payload)
        # Recognized carrier bytes include OP_RETURN and the push opcode.
        policy_vsize = (probe.get_weight() + len(CScript([OP_RETURN, payload])) + 3) // 4
        fee = policy_vsize + DUST_THRESHOLD
        low = self.build_tx_with_dust([0], fee - 1, utxo=utxo, carrier=payload)
        high = self.build_tx_with_dust([0], fee, utxo=utxo, carrier=payload)
        assert_equal(self.nodes[0].testmempoolaccept([low.serialize().hex()])[0]["reject-reason"], "min relay fee not met")
        txid = self.nodes[0].sendrawtransaction(high.serialize().hex())
        entry = self.nodes[0].getmempoolentry(txid)
        assert_equal(entry["vsize"], policy_vsize)
        assert_equal(entry["fees"]["base"], Decimal(fee) / COIN)
        assert_equal(entry["fees"]["modified"], Decimal(policy_vsize) / COIN)
        self.reset_policy("-datacarriercost=0")
        self.check_boundary([0], DUST_THRESHOLD, carrier=payload)
        self.reset_policy("-datacarriersize=0")
        tx = self.build_tx_with_dust([0], 10000, carrier=payload)
        assert_equal(self.nodes[0].testmempoolaccept([tx.serialize().hex()])[0]["reject-reason"], "txn-datacarrier-exceeded")
        self.reset_policy("-datacarriersize=0", "-datacarrierfullcount=0")
        self.check_boundary([0], DUST_THRESHOLD, carrier=payload)
        self.reset_policy()

    def test_fee_delta_and_replacement(self):
        self.log.info("Fee deltas compose with the deficit; RBF compares modified fees")
        node = self.nodes[0]
        utxo = self.wallet.get_utxo()
        tx = self.build_tx_with_dust([0], 1000, utxo=utxo)
        txid = node.sendrawtransaction(tx.serialize().hex())
        assert_equal(node.getmempoolentry(txid)["fees"]["modified"], Decimal(670) / COIN)
        node.prioritisetransaction(txid, 0, 100)
        assert_equal(node.getmempoolentry(txid)["fees"]["base"], Decimal(1000) / COIN)
        assert_equal(node.getmempoolentry(txid)["fees"]["modified"], Decimal(770) / COIN)
        # A higher actual fee can still have a lower effective fee than the conflict.
        low = self.build_tx_with_dust([0], 1050, utxo=utxo)
        assert_equal(node.testmempoolaccept([low.serialize().hex()])[0]["reject-reason"], "insufficient fee")
        high = self.build_tx_with_dust([0], 2000, utxo=utxo)
        replacement = node.sendrawtransaction(high.serialize().hex())
        assert_equal(node.getrawmempool(), [replacement])
        assert_equal(node.getmempoolentry(replacement)["fees"]["modified"], Decimal(1670) / COIN)
        self.reset_policy()
        # A pre-admission delta raises policy fees without changing actual output values.
        tx = self.build_tx_with_dust([0], 0)
        delta = DUST_THRESHOLD + self.get_min_relay_fee(tx)
        node.prioritisetransaction(tx.txid_hex, 0, delta - 1)
        assert_equal(node.testmempoolaccept([tx.serialize().hex()])[0]["allowed"], False)
        node.prioritisetransaction(tx.txid_hex, 0, 1)
        txid = node.sendrawtransaction(tx.serialize().hex())
        assert_equal(node.getmempoolentry(txid)["fees"]["base"], 0)
        assert_equal(node.getmempoolentry(txid)["fees"]["modified"], Decimal(delta - DUST_THRESHOLD) / COIN)
        self.reset_policy()

    def test_package_and_truc(self):
        self.log.info("TRUC sponsorship pays the dust deficit; ordinary packages retain the relay floor")
        node = self.nodes[0]
        for version in (2, 3):
            parent = self.build_tx_with_dust([0], 0, version=version)
            child = CTransaction()
            child.version = version
            child.vin = [CTxIn(COutPoint(parent.txid_int, 1))]
            child.vout = [CTxOut(parent.vout[1].nValue - 1000, self.wallet.get_output_script())]
            self.wallet.sign_tx(child)
            assert_equal(node.testmempoolaccept([parent.serialize().hex()])[0]["allowed"], False)
            result = node.submitpackage([parent.serialize().hex(), child.serialize().hex()])
            if version == 2:
                assert_equal(result["tx-results"][parent.wtxid_hex]["error"], f"min relay fee not met, -{DUST_THRESHOLD} < {parent.get_vsize()}")
                assert_equal(node.getrawmempool(), [])
                self.generateblock(node, output=self.wallet.get_address(), transactions=[parent.serialize().hex(), child.serialize().hex()])
            else:
                assert_equal(result["package_msg"], "success")
                parent_entry = node.getmempoolentry(parent.txid_hex)
                child_entry = node.getmempoolentry(child.txid_hex)
                assert_equal(parent_entry["fees"]["base"], 0)
                assert_equal(parent_entry["fees"]["modified"], -Decimal(DUST_THRESHOLD) / COIN)
                assert_equal(parent_entry["fees"]["descendant"], Decimal(1000 - DUST_THRESHOLD) / COIN)
                assert_equal(child_entry["fees"]["ancestor"], Decimal(1000 - DUST_THRESHOLD) / COIN)
            self.reset_policy()

        # Enforced v3 can be sponsored below its individual relay floor, but the
        # combined modified fee must still reach the package's mempool minimum.
        parent = self.build_tx_with_dust([0], 0, version=3)
        child = CTransaction()
        child.version = 3
        child.vin = [CTxIn(COutPoint(parent.txid_int, 1))]
        child.vout = [CTxOut(parent.vout[1].nValue, self.wallet.get_output_script())]
        self.wallet.sign_tx(child)
        package_size = parent.get_vsize() + child.get_vsize()
        fee = DUST_THRESHOLD + package_size
        child.vout[0].nValue -= fee - 1
        result = node.submitpackage([parent.serialize().hex(), child.serialize().hex()])
        assert_equal(result["package_msg"], "transaction failed")
        assert_equal(result["tx-results"][child.wtxid_hex]["error"], f"min relay fee not met, {package_size - 1} < {package_size}")
        assert_equal(node.getrawmempool(), [])
        child.vout[0].nValue -= 1
        result = node.submitpackage([parent.serialize().hex(), child.serialize().hex()])
        assert_equal(result["package_msg"], "success")
        assert_equal(node.getmempoolentry(child.txid_hex)["fees"]["ancestor"], Decimal(package_size) / COIN)
        self.reset_policy("-mempooltruc=accept")
        parent = self.build_tx_with_dust([0], 0, version=3)
        assert_equal(node.testmempoolaccept([parent.serialize().hex()])[0]["reject-reason"], "min relay fee not met")
        self.reset_policy()

    def test_mining_and_block_acceptance(self):
        self.log.info("Templates select modified fees, while blocks reward actual fees and accept local rejects")
        node = self.nodes[0]
        self.reset_policy("-blockprioritysize=0", "-blockmintxfee=0.00002")
        probe = self.build_tx_with_dust([0], 1000)
        min_fee = self.get_min_relay_fee(probe)
        low = self.build_tx_with_dust([0], min_fee)
        assert_equal(node.testmempoolaccept([low.serialize().hex()])[0]["reject-reason"], "min relay fee not met")
        previous_height = node.getblockcount()
        block = self.generateblock(node, output=self.wallet.get_address(), transactions=[low.serialize().hex()])
        assert_equal(node.getblockcount(), previous_height + 1)
        assert low.txid_hex in node.getblock(block["hash"])["tx"]
        self.wallet.rescan_utxos()
        # Actual fee exceeds template minimum, but effective fee is one sat below it.
        fee = DUST_THRESHOLD + 2 * min_fee - 1
        tx = self.build_tx_with_dust([0], fee)
        txid = node.sendrawtransaction(tx.serialize().hex())
        template = node.getblocktemplate({"rules": ["segwit"]})
        assert txid not in [entry["txid"] for entry in template["transactions"]]
        node.prioritisetransaction(txid, 0, 1)
        # Changing mocktime expires the RPC's cached template without wall-clock sleeps.
        node.setmocktime(node.getblockheader(node.getbestblockhash())["time"] + 10)
        template = node.getblocktemplate({"rules": ["segwit"]})
        entry = next(entry for entry in template["transactions"] if entry["txid"] == txid)
        assert_equal(entry["fee"], fee)
        self.generate(self.wallet, 1)
        mined = node.getblock(node.getbestblockhash(), 2)
        assert txid in [entry["txid"] for entry in mined["tx"]]
        # Regtest at this height has a 25 BTC subsidy. Penalty/delta do not alter reward.
        assert_equal(sum(output["value"] for output in mined["tx"][0]["vout"]), Decimal(25) + Decimal(fee) / COIN)
        self.reset_policy("-blockprioritysize=0", "-blockmintxfee=0.00002", "-subdustfeepenalty=0")
        tx = self.build_tx_with_dust([0], fee)
        txid = node.sendrawtransaction(tx.serialize().hex())
        assert txid in [entry["txid"] for entry in node.getblocktemplate({"rules": ["segwit"]})["transactions"]]
        self.reset_policy("-blockprioritysize=100000", "-blockmintxfee=0.00002")
        # The explicit priority area can mine sufficiently aged coins below the
        # template's fee floor. The dust penalty still appears in entry accounting.
        tx = self.build_tx_with_dust([0], DUST_THRESHOLD + min_fee)
        txid = node.sendrawtransaction(tx.serialize().hex())
        assert_equal(node.getmempoolentry(txid)["fees"]["modified"], Decimal(min_fee) / COIN)
        assert txid in [entry["txid"] for entry in node.getblocktemplate({"rules": ["segwit"]})["transactions"]]
        self.generate(self.wallet, 1)
        mined = node.getblock(node.getbestblockhash(), 2)
        assert_equal([entry["txid"] for entry in mined["tx"]][1:], [txid])
        assert_equal(sum(output["value"] for output in mined["tx"][0]["vout"]),
                     Decimal(25) + Decimal(DUST_THRESHOLD + min_fee) / COIN)


if __name__ == '__main__':
    SubDustFeePenaltyTest(__file__).main()
