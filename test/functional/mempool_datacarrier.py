#!/usr/bin/env python3
# Copyright (c) 2020-2022 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test datacarrier functionality"""
from random import randbytes

from test_framework.blocktools import MAX_STANDARD_TX_WEIGHT
from test_framework.key import compute_xonly_pubkey
from test_framework.messages import (
    COutPoint,
    CTransaction,
    CTxIn,
    CTxInWitness,
    CTxOut,
)
from test_framework.script import (
    CScript,
    MAX_SCRIPT_ELEMENT_SIZE,
    OP_DROP,
    OP_RETURN,
    OP_TRUE,
    taproot_construct,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_node import TestNode
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)
from test_framework.wallet import MiniWallet


# The historical maximum, now used to test coverage
CUSTOM_DATACARRIER_ARG = 83

class DataCarrierTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.extra_args = [
            [], # default aggregate size is 83 bytes
            ["-datacarrier=0"], # no relay of datacarrier
            ["-datacarrier=1", f"-datacarriersize={CUSTOM_DATACARRIER_ARG}"],
            ["-datacarrier=1", "-datacarriersize=2"],
        ]

    def test_null_data_transaction(self, node: TestNode, data, success: bool) -> str:
        tx = self.wallet.create_self_transfer(fee_rate=0)["tx"]
        data = [] if data is None else [data]
        tx.vout.append(CTxOut(nValue=0, scriptPubKey=CScript([OP_RETURN] + data)))
        tx.vout[0].nValue -= tx.get_vsize()  # simply pay 1sat/vbyte fee

        tx_hex = tx.serialize().hex()

        if success:
            self.wallet.sendrawtransaction(from_node=node, tx_hex=tx_hex)
            assert tx.txid_hex in node.getrawmempool(True), f'{tx_hex} not in mempool'
        else:
            assert_raises_rpc_error(-26, "datacarrier", self.wallet.sendrawtransaction, from_node=node, tx_hex=tx_hex)
        return tx_hex

    def test_taproot_carrier(self):
        # Match feature_taproot's pushmaxlimit pattern without randomized input grouping.
        script = CScript([b"x" * MAX_SCRIPT_ELEMENT_SIZE, OP_DROP, OP_TRUE])
        tap = taproot_construct(compute_xonly_pubkey((1).to_bytes(32, "big"))[0], [("carrier", script)])
        funding = self.wallet.send_to(from_node=self.nodes[0], scriptPubKey=tap.scriptPubKey, amount=100000)
        self.generate(self.nodes[0], 1, sync_fun=self.no_op)
        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(int(funding["txid"], 16), funding["sent_vout"]))]
        tx.vout = [CTxOut(90000, funding["tx"].vout[0].scriptPubKey)]
        tx.wit.vtxinwit = [CTxInWitness()]
        leaf = tap.leaves["carrier"]
        control = bytes([leaf.version | tap.negflag]) + tap.internal_pubkey + leaf.merklebranch
        tx.wit.vtxinwit[0].scriptWitness.stack = [script, control]
        tx_hex = tx.serialize().hex()

        self.log.info("Testing Taproot push/OP_DROP carrier policy independently of script validity.")
        assert_raises_rpc_error(-26, "txn-datacarrier-nonstandard", self.nodes[0].sendrawtransaction, tx_hex)
        # Disabling general standardness does not disable carrier recognition.
        self.restart_node(0, extra_args=["-acceptnonstdtxn=1"])
        assert_raises_rpc_error(-26, "txn-datacarrier-nonstandard", self.nodes[0].sendrawtransaction, tx_hex)
        # Full-count disabling is separate from the nonstandard-carrier switch.
        self.restart_node(0, extra_args=["-acceptnonstddatacarrier=1", "-datacarrierfullcount=0", "-datacarriersize=0"])
        assert_equal(self.nodes[0].testmempoolaccept([tx_hex])[0]["allowed"], True)
        carrier_size = len(CScript([b"x" * MAX_SCRIPT_ELEMENT_SIZE, OP_DROP]))
        self.restart_node(0, extra_args=["-acceptnonstddatacarrier=1", f"-datacarriersize={carrier_size - 1}"])
        assert_raises_rpc_error(-26, "txn-datacarrier-exceeded", self.nodes[0].sendrawtransaction, tx_hex)
        # A local carrier rejection cannot invalidate this consensus-valid script.
        self.generateblock(self.nodes[0], output="raw(42)", transactions=[tx_hex], sync_fun=self.no_op)
        self.nodes[0].invalidateblock(self.nodes[0].getbestblockhash())
        self.restart_node(0, extra_args=["-acceptnonstddatacarrier=1", f"-datacarriersize={carrier_size}"])
        assert_equal(self.nodes[0].sendrawtransaction(tx_hex), tx.txid_hex)
        assert tx.txid_hex in self.nodes[0].getrawmempool()
        self.restart_node(0)

    def run_test(self):
        self.wallet = MiniWallet(self.nodes[0])

        # Roots rejects bare multisig by default; report the actual configured policy.

        assert_equal(self.nodes[0].getmempoolinfo()["maxdatacarriersize"], CUSTOM_DATACARRIER_ARG)
        assert_equal(self.nodes[1].getmempoolinfo()["maxdatacarriersize"], 0)
        assert_equal(self.nodes[2].getmempoolinfo()["maxdatacarriersize"], CUSTOM_DATACARRIER_ARG)
        assert_equal(self.nodes[3].getmempoolinfo()["maxdatacarriersize"], 2)


        # If it is custom set to 83, the historical value,
        # only 80 bytes are used for data (+1 for OP_RETURN, +2 for the pushdata opcodes).
        custom_size_data = randbytes(CUSTOM_DATACARRIER_ARG - 3)
        too_long_data = randbytes(CUSTOM_DATACARRIER_ARG - 2)
        one_byte = randbytes(1)
        zero_bytes = randbytes(0)

        self.log.info("Testing null data with the default 83-byte aggregate limit.")
        self.test_null_data_transaction(node=self.nodes[0], data=custom_size_data, success=True)
        rejected_tx = self.test_null_data_transaction(node=self.nodes[0], data=too_long_data, success=False)
        # Relay limits do not alter block validity.
        self.generateblock(self.nodes[0], output="raw(42)", transactions=[rejected_tx], sync_fun=self.no_op)

        self.log.info("Testing a null data transaction with -datacarrier=false.")
        self.test_null_data_transaction(node=self.nodes[1], data=custom_size_data, success=False)

        self.log.info("Testing a null data transaction with a size larger than accepted by -datacarriersize.")
        self.test_null_data_transaction(node=self.nodes[2], data=too_long_data, success=False)

        self.log.info("Testing a null data transaction with a size equal to -datacarriersize.")
        self.test_null_data_transaction(node=self.nodes[2], data=custom_size_data, success=True)

        self.log.info("Testing a null data transaction with no data.")
        self.test_null_data_transaction(node=self.nodes[0], data=None, success=True)
        self.test_null_data_transaction(node=self.nodes[1], data=None, success=False)
        self.test_null_data_transaction(node=self.nodes[2], data=None, success=True)
        self.test_null_data_transaction(node=self.nodes[3], data=None, success=True)

        self.log.info("Testing a null data transaction with zero bytes of data.")
        self.test_null_data_transaction(node=self.nodes[0], data=zero_bytes, success=True)
        self.test_null_data_transaction(node=self.nodes[1], data=zero_bytes, success=False)
        self.test_null_data_transaction(node=self.nodes[2], data=zero_bytes, success=True)
        self.test_null_data_transaction(node=self.nodes[3], data=zero_bytes, success=True)

        self.log.info("Testing a null data transaction with one byte of data.")
        self.test_null_data_transaction(node=self.nodes[0], data=one_byte, success=True)
        self.test_null_data_transaction(node=self.nodes[1], data=one_byte, success=False)
        self.test_null_data_transaction(node=self.nodes[2], data=one_byte, success=True)
        self.test_null_data_transaction(node=self.nodes[3], data=one_byte, success=False)

        self.log.info("Testing an explicit large data-carrier limit below the standard transaction weight boundary.")
        self.restart_node(0, extra_args=["-datacarriersize=100000", "-maxscriptsize=100000"])
        self.test_null_data_transaction(node=self.nodes[0], data=randbytes(MAX_STANDARD_TX_WEIGHT // 4 - 200), success=True)

        self.log.info("Testing the maximum carrier-cost setting without overflowing virtual size.")
        self.restart_node(0, extra_args=["-datacarriercost=1073741823.75"])
        tx = self.wallet.create_self_transfer(fee_rate=0)["tx"]
        tx.vout.append(CTxOut(0, CScript([OP_RETURN, b"x" * 80])))
        tx.vout[0].nValue -= tx.get_vsize()
        tx_hex = tx.serialize().hex()
        result = self.nodes[0].testmempoolaccept([tx_hex])[0]
        assert_equal(result["allowed"], False)
        assert_equal(result["reject-reason"], "min relay fee not met")
        # An extreme local fee policy still cannot invalidate a valid block.
        self.generateblock(self.nodes[0], output="raw(42)", transactions=[tx_hex], sync_fun=self.no_op)

        self.stop_node(0)
        for cost in ["-1", "1073741823.76", "1e999", "invalid"]:
            self.nodes[0].assert_start_raises_init_error(
                [f"-datacarriercost={cost}"], f"Error: Invalid -datacarriercost value: '{cost}' (expected a value from 0 to 1073741823.75 exactly representable in hundredths)")
        self.start_node(0)
        self.test_taproot_carrier()

if __name__ == '__main__':
    DataCarrierTest(__file__).main()
