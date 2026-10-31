#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Local Roots controls, exact relay-age boundaries and block validity."""

from test_framework.key import compute_xonly_pubkey
from test_framework.messages import COutPoint, CTransaction, CTxIn, CTxInWitness, CTxOut
from test_framework.script import CScript, OP_2, OP_13, OP_CHECKSIG, OP_DROP, OP_ELSE, OP_ENDIF, OP_FALSE, OP_IF, OP_NOP, OP_RETURN, OP_TRUE, taproot_construct
from test_framework.script_util import PAY_TO_ANCHOR, key_to_p2pk_script, keys_to_multisig_script, script_to_p2sh_script, script_to_p2wsh_script
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
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
        self.restart_node(0)
        self.test_paired_boundaries(wallet)

    def test_paired_boundaries(self, wallet):
        node = self.nodes[0]
        wallet.rescan_utxos()

        def result(tx):
            return node.testmempoolaccept([tx.serialize().hex()], 0)[0]

        def pair(label, tx, reason, reject_args=(), allow_args=("-acceptnonstdtxn=1",)):
            self.log.info("Paired policy/block case: %s (%s)", label, reason)
            self.restart_node(0, extra_args=list(reject_args))
            rejected = result(tx)
            assert_equal(rejected["allowed"], False)
            assert_equal(rejected["reject-reason"], reason)
            assert_raises_rpc_error(-26, reason, node.sendrawtransaction, tx.serialize().hex(), 0)
            assert tx.txid_hex not in node.getrawmempool()
            assert tx.txid_hex not in [entry["txid"] for entry in node.getblocktemplate({"rules": ["segwit"]})["transactions"]]
            self.restart_node(0, extra_args=list(allow_args))
            allowed = result(tx)
            assert allowed["allowed"], allowed
            self.restart_node(0, extra_args=list(reject_args))
            self.generateblock(node, output="raw(42)", transactions=[tx.serialize().hex()])
            self.restart_node(0)
            # Direct block construction bypasses MiniWallet's send/scan hooks.
            wallet.rescan_utxos()

        def spend_script(script, stack=(), *, taproot=False, p2sh=False, extra_leaf=False):
            wallet.rescan_utxos()
            if taproot:
                leaves = [("test", script)]
                if extra_leaf:
                    leaves.append(("other", CScript([OP_TRUE])))
                tap = taproot_construct(compute_xonly_pubkey((1).to_bytes(32, "big"))[0], leaves)
                output = tap.scriptPubKey
            else:
                output = script_to_p2sh_script(script) if p2sh else script_to_p2wsh_script(script)
            funding = wallet.send_to(from_node=node, scriptPubKey=output, amount=100000)
            self.generate(node, 1)
            tx = CTransaction()
            tx.vin = [CTxIn(COutPoint(int(funding["txid"], 16), funding["sent_vout"]))]
            tx.vout = [CTxOut(90000, wallet.get_output_script())]
            if p2sh:
                tx.vin[0].scriptSig = CScript([*stack, script])
            else:
                tx.wit.vtxinwit = [CTxInWitness()]
                tx.wit.vtxinwit[0].scriptWitness.stack = [*stack, script]
                if taproot:
                    leaf = tap.leaves["test"]
                    tx.wit.vtxinwit[0].scriptWitness.stack.append(bytes([leaf.version | tap.negflag]) + tap.internal_pubkey + leaf.merklebranch)
            return tx

        tx = wallet.create_self_transfer()["tx"]
        tx.vout[0].scriptPubKey = keys_to_multisig_script([generate_keypair()[1]], k=1)
        pair("bare multisig default/override", tx, "bare-multisig", allow_args=("-permitbaremultisig=1",))
        tx = wallet.create_self_transfer(fee_rate=0)["tx"]
        pair("default relay fee floor", tx, "min relay fee not met", allow_args=("-minrelaytxfee=0",))

        tx = wallet.create_self_transfer()["tx"]
        # Keep the non-witness size above Core's65-byte admission minimum.
        tx.vout[0] = CTxOut(0, CScript([OP_RETURN, b"carrier"]))
        pair("carrier-only default/override", tx, "bare-datacarrier", allow_args=("-permitbaredatacarrier=1",))

        tx = wallet.create_self_transfer()["tx"]
        tx.vout.append(CTxOut(0, CScript([OP_RETURN, b"carrier"])))
        pair("carrier disabled", tx, "datacarrier", ("-datacarrier=0",), ())
        tx = wallet.create_self_transfer()["tx"]
        tx.vout.append(CTxOut(0, CScript([OP_RETURN, b"x" * 80])))
        assert_equal(len(tx.vout[-1].scriptPubKey), 83)
        pair("carrier83/82-byte boundary", tx, "datacarrier", ("-datacarriersize=82",), ())
        tx = wallet.create_self_transfer()["tx"]
        pair("coin-block gate independent of maturity", tx, "bad-txns-input-immature-coinblocks", ("-minrelaycoinblocks=9223372036854775807",), ())
        tx = wallet.create_self_transfer()["tx"]
        pair("maturity gate independent of coin blocks", tx, "bad-txns-input-immature-depth", ("-minrelaymaturity=2147483647",), ())

        tx = wallet.create_self_transfer()["tx"]
        pair("output script bound", tx, "scriptpubkey-size", ("-maxscriptsize=33",), ())
        tx = wallet.create_self_transfer()["tx"]
        tx.vout[0].scriptPubKey = CScript([OP_FALSE, b"w" * 20])
        pair("spent script bound", tx, "bad-txns-input-script-size", ("-maxscriptsize=23",), ())
        # One truthy push keeps the legacy opcode count below201 while its
        # serialized redeem-script push crosses the configured scriptSig bound.
        tx = spend_script(CScript([b"x" * 253]), p2sh=True)
        pair("scriptSig/redeem serialized bound", tx, "scriptsig-size", ("-maxscriptsize=256",), ("-maxscriptsize=300",))
        tx = spend_script(CScript([OP_DROP] * 20 + [OP_TRUE]), [b"x" * 82] * 20, p2sh=True)
        pair("fixed scriptSig ceiling", tx, "scriptsig-size")

        tx = spend_script(CScript([OP_NOP] * 1700 + [OP_TRUE]), taproot=True)
        pair("default total witness bound", tx, "bad-witness-witness-size", allow_args=("-maxscriptsize=2000",))
        for taproot in (False, True):
            script = CScript([OP_DROP, OP_TRUE])
            tx = spend_script(script, [b"x" * 80], taproot=taproot)
            assert_equal(result(tx)["allowed"], True)
            tx.wit.vtxinwit[0].scriptWitness.stack[0] = b"x" * 81
            reason = "bad-witness-taproot-stackitem-size" if taproot else "bad-witness-stackitem-size"
            pair("witness argument80/81, taproot=%s" % taproot, tx, reason)
        tx = spend_script(CScript([OP_DROP] * 101 + [OP_TRUE]), [b""] * 101)
        pair("P2WSH stack count", tx, "bad-witness-stackitem-count")
        tx = spend_script(CScript([b"x" * 520, OP_DROP] * 7 + [OP_TRUE]))
        pair("native P2WSH script bound", tx, "bad-witness-script-size",
             ("-maxscriptsize=5000", "-acceptnonstddatacarrier=1", "-datacarrierfullcount=0"),
             ("-acceptnonstdtxn=1", "-acceptnonstddatacarrier=1", "-datacarrierfullcount=0"))
        tx = wallet.create_self_transfer()["tx"]
        tx.wit.vtxinwit[0].scriptWitness.stack.append(b"\x50annex")
        pair("Taproot annex", tx, "bad-witness-taproot-annex")

        # A true branch leaves one true item; the unexecuted else branch still
        # contributes sigops without matching the independent OP_FALSE/OP_IF carrier rule.
        script = CScript([OP_TRUE, OP_IF, OP_TRUE, OP_ELSE, OP_CHECKSIG, OP_CHECKSIG, OP_ENDIF])
        tx = spend_script(script, p2sh=True)
        pair("configured legacy sigops", tx, "bad-txns-input-sigops-toomany-overall", ("-maxtxlegacysigops=1",), ())
        tx = spend_script(script)
        assert_equal(result(tx)["allowed"], True)  # default density is20
        pair("strict density enabled/disabled", tx, "bad-txns-too-many-sigops", ("-bytespersigopstrict=1000",), ("-bytespersigopstrict=0",))

        tx = wallet.create_self_transfer()["tx"]
        tx.vout.append(CTxOut(1000, PAY_TO_ANCHOR))
        tx.vout[0].nValue -= 1000
        pair("anchor permission", tx, "anchor", ("-permitephemeral=0",), ())
        tx = wallet.create_self_transfer(fee_rate=0)["tx"]
        tx.vout.append(CTxOut(1, wallet.get_output_script()))
        tx.vout[0].nValue -= 1
        pair("default non-anchor dust permission", tx, "dust-nonanchor", ("-minrelaytxfee=0",), ("-permitephemeral=1", "-minrelaytxfee=0"))
        tx = wallet.create_self_transfer(fee_rate=0)["tx"]
        tx.vout += [CTxOut(1, wallet.get_output_script())] * 2
        tx.vout[0].nValue -= 2
        pair("multiple dust cap", tx, "dust", ("-permitephemeral=1", "-minrelaytxfee=0"), ("-dustrelayfee=0", "-minrelaytxfee=0"))

        funding = wallet.create_self_transfer()["tx"]
        funding.vout += [CTxOut(0, PAY_TO_ANCHOR)] * 2
        self.generateblock(node, output="raw(42)", transactions=[funding.serialize().hex()])
        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(int(funding.txid_hex, 16), index)) for index in (1, 2)]
        tx.vout = [CTxOut(0, PAY_TO_ANCHOR)]
        pair("bare anchor permission", tx, "bare-anchor", ("-permitbareanchor=0", "-minrelaytxfee=0"), ("-minrelaytxfee=0",))

        tx = wallet.create_self_transfer()["tx"]
        olga = CScript([OP_FALSE, bytes.fromhex("003e7374616d703a000000000000000000000000000000000000000000000000")])
        tx.vout += [CTxOut(1000, olga)] * 2
        tx.vout[0].nValue -= 2000
        carrier_override = ("-acceptnonstddatacarrier=1", "-datacarrierfullcount=0")
        pair("OLGA token filter", tx, "tokens-olga", (*carrier_override, "-rejecttokens=1"), carrier_override)
        tx = spend_script(CScript([OP_DROP, OP_DROP, OP_DROP, b"op", OP_DROP, OP_TRUE]), [b"one", b"two", b"three"], taproot=True, extra_leaf=True)
        pair("OPNet witness carrier", tx, "txn-datacarrier-nonstandard", (), carrier_override)


if __name__ == '__main__':
    LocalPolicyTest(__file__).main()
