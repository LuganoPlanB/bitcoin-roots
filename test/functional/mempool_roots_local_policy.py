#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Local Roots controls, exact relay-age boundaries and block validity."""

from test_framework.messages import CTxOut
from test_framework.script import CScript, OP_2, OP_13, OP_RETURN
from test_framework.script_util import key_to_p2pk_script
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet
from test_framework.wallet_util import generate_keypair


class LocalPolicyTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.wallet_names = []

    def run_test(self):
        node = self.nodes[0]
        wallet = MiniWallet(node)

        def result(tx):
            return node.testmempoolaccept([tx.serialize().hex()])[0]

        def rejected(tx, reason):
            res = result(tx)
            assert_equal(res["allowed"], False)
            assert_equal(res["reject-reason"], reason)

        tx = wallet.create_self_transfer()["tx"]
        tx.nLockTime = 21
        rejected(tx, "parasite-cat21")
        self.restart_node(0, extra_args=["-acceptnonstdtxn=1"])
        assert_equal(result(tx)["allowed"], True)
        self.restart_node(0, extra_args=["-rejectparasites=0"])
        assert_equal(result(tx)["allowed"], True)
        self.restart_node(0)
        # The default local marker restriction must never affect block validity.
        self.generateblock(node, output="raw(42)", transactions=[tx.serialize().hex()])

        tx = wallet.create_self_transfer()["tx"]
        tx.vout[0].scriptPubKey = key_to_p2pk_script(generate_keypair()[1])
        rejected(tx, "bare-pubkey")
        self.restart_node(0, extra_args=["-permitbarepubkey=1"])
        assert_equal(result(tx)["allowed"], True)
        self.restart_node(0)
        rejected(tx, "bare-pubkey")
        self.generateblock(node, output="raw(42)", transactions=[tx.serialize().hex()])

        tx = wallet.create_self_transfer()["tx"]
        tx.vout[0].scriptPubKey = CScript([OP_2, b"w" * 32])
        assert_equal(result(tx)["allowed"], True)
        self.restart_node(0, extra_args=["-acceptunknownwitness=0"])
        rejected(tx, "scriptpubkey-unknown-witnessversion")
        self.generateblock(node, output="raw(42)", transactions=[tx.serialize().hex()])
        self.restart_node(0)

        tx = wallet.create_self_transfer()["tx"]
        tx.vout.append(CTxOut(0, CScript([OP_RETURN, OP_13, b"token"])))
        self.restart_node(0, extra_args=["-rejecttokens=1"])
        rejected(tx, "tokens-runes")
        self.restart_node(0)
        assert_equal(result(tx)["allowed"], True)
        self.restart_node(0, extra_args=["-rejecttokens=1"])
        self.generateblock(node, output="raw(42)", transactions=[tx.serialize().hex()])
        self.restart_node(0)
        tx = wallet.create_self_transfer()["tx"]
        tx.vout.extend([CTxOut(0, CScript([OP_RETURN])), CTxOut(0, CScript([OP_RETURN]))])
        rejected(tx, "multi-op-return")
        self.generateblock(node, output="raw(42)", transactions=[tx.serialize().hex()])

        self.log.info("Ephemeral send/dust flags preserve their local exceptions")
        tx = wallet.create_self_transfer(fee_rate=0)["tx"]
        tx.vout.append(CTxOut(1, tx.vout[0].scriptPubKey))
        tx.vout[0].nValue -= 1
        rejected(tx, "dust-nonanchor")
        # These local exceptions are independent of L8's deferred deficit penalty.
        self.restart_node(0, extra_args=["-permitephemeral=send", "-minrelaytxfee=0"])
        assert_equal(result(tx)["allowed"], True)
        self.restart_node(0, extra_args=["-permitephemeral=send,-dust", "-minrelaytxfee=0"])
        rejected(tx, "dust-nonzero")
        self.generateblock(node, output="raw(42)", transactions=[tx.serialize().hex()])
        self.restart_node(0)

        self.log.info("Relay maturity and coin-block gates are independent of block maturity")
        parent = wallet.send_self_transfer(from_node=node)
        child = wallet.create_self_transfer(utxo_to_spend=parent["new_utxo"])
        self.restart_node(0, extra_args=["-minrelaymaturity=1"])
        rejected(child["tx"], "bad-txns-input-immature-depth")
        self.restart_node(0, extra_args=["-minrelaycoinblocks=1"])
        rejected(child["tx"], "bad-txns-input-immature-coinblocks")
        self.restart_node(0, extra_args=["-minrelaycoinblocks=0", "-minrelaymaturity=0"])
        assert_equal(result(child["tx"])["allowed"], True)
        self.generate(node, 1)
        value = parent["tx"].vout[0].nValue
        self.restart_node(0, extra_args=[f"-minrelaycoinblocks={value}", "-minrelaymaturity=1"])
        assert_equal(result(child["tx"])["allowed"], True)
        self.restart_node(0, extra_args=[f"-minrelaycoinblocks={value + 1}"])
        rejected(child["tx"], "bad-txns-input-immature-coinblocks")
        self.restart_node(0, extra_args=["-minrelaymaturity=2"])
        rejected(child["tx"], "bad-txns-input-immature-depth")
        self.generate(node, 1)
        assert_equal(result(child["tx"])["allowed"], True)
        # Tip changes must affect fresh local admission, without a stale age cache.
        last_block = node.getbestblockhash()
        node.invalidateblock(last_block)
        rejected(child["tx"], "bad-txns-input-immature-depth")
        node.reconsiderblock(last_block)
        assert_equal(result(child["tx"])["allowed"], True)
        self.restart_node(0, extra_args=["-minrelaycoinblocks=9223372036854775807"])
        rejected(child["tx"], "bad-txns-input-immature-coinblocks")
        self.restart_node(0, extra_args=["-minrelaymaturity=2147483647", "-minrelaycoinblocks=9223372036854775807"])
        rejected(child["tx"], "bad-txns-input-immature-depth")
        self.generateblock(node, output="raw(42)", transactions=[child["hex"]])


if __name__ == '__main__':
    LocalPolicyTest(__file__).main()
