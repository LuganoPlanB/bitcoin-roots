#!/usr/bin/env python3
# Copyright (c) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Check initialized policy reporting and representative admission parity."""

import json

from decimal import Decimal

from test_framework.messages import COIN, CTxOut
from test_framework.script import CScript, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.wallet import MiniWallet


DEFAULT_POLICY = {
    "require_standard": True,
    "max_script_size": 1650,
    "bytes_per_sigop": 20,
    "bytes_per_sigop_strict": 20,
    "max_tx_legacy_sigops": 2500,
    "datacarrier": True,
    "max_datacarrier_bytes": 83,
    "datacarrier_fullcount": True,
    "datacarrier_weight_per_byte": 4,
    "accept_nonstd_datacarrier": False,
    "permit_bare_datacarrier": False,
    "dust_relay_fee": Decimal("0.00003000"),
    "subdust_fee_penalty": True,
    "permit_bare_pubkey": False,
    "permit_bare_multisig": False,
    "reject_parasites": True,
    "reject_tokens": False,
    "accept_unknown_witness": True,
    "permit_bare_anchor": True,
    "ephemeral_anchor": True,
    "ephemeral_dust": False,
    "ephemeral_send": False,
    "replacement": "always",
    "truc": "enforce",
    "min_relay_coinblocks": 0,
    "min_relay_maturity": 0,
    "spk_reuse": "allow",
}


class EffectivePolicyTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.uses_wallet = None
        self.wallet_names = []

    def check_policy(self, overrides=None, *, min_relay_fee=Decimal("0.00001000"), incremental_fee=Decimal("0.00000100")):
        expected = DEFAULT_POLICY | (overrides or {})
        info = self.nodes[0].getmempoolinfo()
        assert_equal(info["minrelaytxfee"], min_relay_fee)
        assert_equal(info["incrementalrelayfee"], incremental_fee)
        assert_equal(info["mempoolminfee"], min_relay_fee)
        assert_equal(info["fullrbf"], expected["replacement"] == "always")
        assert_equal(info["permitbaremultisig"], expected["permit_bare_multisig"])
        assert_equal(info["maxdatacarriersize"], expected["max_datacarrier_bytes"])
        policy = info["policy"]
        # Permit future additive fields, but require every defined field and its type.
        for key, value in expected.items():
            assert_equal(policy[key], value)
            assert_equal(type(policy[key]), type(value))

    def run_test(self):
        node = self.nodes[0]
        assert_raises_rpc_error(-32601, "Method not found", node.getmempoolstats)
        self.log.info("Checking ordinary initialized defaults")
        self.check_policy()
        help_text = node.help("getmempoolinfo")
        assert "not consensus rules or mining limits" in help_text
        for field in DEFAULT_POLICY:
            assert f'"{field}"' in help_text
        assert "satoshi-blocks" in help_text
        # Exercise the CLI help entry point too, rather than only the RPC client.
        assert_equal(node.cli.help("getmempoolinfo"), help_text.rstrip("\n"))

        self.log.info("Checking initialized overrides, rounding and resolved token modes")
        self.restart_node(0, extra_args=[
            "-acceptnonstdtxn=1", "-maxscriptsize=2000", "-bytespersigop=25",
            "-bytespersigopstrict=30", "-maxtxlegacysigops=3000", "-datacarriersize=100",
            "-datacarrierfullcount=0", "-datacarriercost=0.26", "-acceptnonstddatacarrier=1",
            "-permitbaredatacarrier=1", "-dustrelayfee=0.00004", "-subdustfeepenalty=0",
            "-permitbarepubkey=1", "-permitbaremultisig=1", "-rejectparasites=0",
            "-rejecttokens=1", "-acceptunknownwitness=0", "-permitbareanchor=0",
            "-permitephemeral=dust", "-mempoolreplacement=optin", "-mempooltruc=accept",
            "-minrelaycoinblocks=123456", "-minrelaymaturity=2", "-spkreuse=conflict",
            "-incrementalrelayfee=0.00002",
        ])
        self.check_policy({
            "require_standard": False, "max_script_size": 2000, "bytes_per_sigop": 25,
            "bytes_per_sigop_strict": 30, "max_tx_legacy_sigops": 3000,
            "max_datacarrier_bytes": 100, "datacarrier_fullcount": False,
            "datacarrier_weight_per_byte": 2, "accept_nonstd_datacarrier": True,
            "permit_bare_datacarrier": True, "dust_relay_fee": Decimal("0.00004000"),
            "subdust_fee_penalty": False, "permit_bare_pubkey": True,
            "permit_bare_multisig": True, "reject_parasites": False, "reject_tokens": True,
            "accept_unknown_witness": False, "permit_bare_anchor": False,
            "ephemeral_dust": True, "ephemeral_send": True, "replacement": "optin",
            "truc": "accept", "min_relay_coinblocks": 123456, "min_relay_maturity": 2,
            "spk_reuse": "conflict",
        }, min_relay_fee=Decimal("0.00002000"), incremental_fee=Decimal("0.00002000"))

        self.log.info("Checking disabled controls and enabled zero-byte carrier budget")
        self.restart_node(0, extra_args=[
            "-datacarrier=0", "-datacarriersize=999", "-datacarriercost=0",
            "-bytespersigop=0", "-bytespersigopstrict=0", "-maxscriptsize=0",
            "-maxtxlegacysigops=0", "-dustrelayfee=0", "-subdustfeepenalty=0",
            "-permitephemeral=0", "-mempoolreplacement=0", "-mempooltruc=reject",
        ])
        self.check_policy({
            "datacarrier": False, "max_datacarrier_bytes": 0,
            "datacarrier_weight_per_byte": 0, "bytes_per_sigop": 0,
            "bytes_per_sigop_strict": 0, "max_script_size": 0, "max_tx_legacy_sigops": 0,
            "dust_relay_fee": Decimal("0.00000000"), "subdust_fee_penalty": False,
            "ephemeral_anchor": False, "replacement": "never", "truc": "reject",
        })
        self.restart_node(0, extra_args=["-datacarrier=1", "-datacarriersize=0"])
        self.check_policy({"max_datacarrier_bytes": 0})

        self.log.info("Checking first config / last CLI selection and explicit fee override")
        config_path = node.datadir_path / "bitcoin.conf"
        base_config = config_path.read_text(encoding="utf-8")
        self.stop_node(0)
        config_path.write_text(base_config + "datacarriersize=90\ndatacarriersize=91\nsubdustfeepenalty=0\n", encoding="utf-8")
        self.start_node(0)
        self.check_policy({"max_datacarrier_bytes": 90, "subdust_fee_penalty": False})
        self.restart_node(0, extra_args=["-datacarriersize=92", "-datacarriersize=93", "-subdustfeepenalty=1", "-incrementalrelayfee=0.00002", "-minrelaytxfee=0.00001"])
        self.check_policy({"max_datacarrier_bytes": 93}, incremental_fee=Decimal("0.00002000"))
        self.stop_node(0)
        config_path.write_text(base_config, encoding="utf-8")

        self.log.info("Read/write config overrides JSON and network config; CLI remains highest")
        rw_path = node.chain_path / "bitcoin_rw.conf"
        settings_path = node.chain_path / "settings.json"
        settings_path.write_text(json.dumps({"datacarriersize": "94", "subdustfeepenalty": False}), encoding="utf-8")
        config_path.write_text(base_config + "datacarriersize=95\n", encoding="utf-8")
        rw_path.write_text("datacarriersize=96\ndatacarriersize=97\nsubdustfeepenalty=1\n", encoding="utf-8")
        self.start_node(0)
        self.check_policy({"max_datacarrier_bytes": 96})
        self.restart_node(0, extra_args=["-datacarriersize=98", "-datacarriersize=99", "-subdustfeepenalty=0"])
        self.check_policy({"max_datacarrier_bytes": 99, "subdust_fee_penalty": False})
        alternate_path = node.chain_path / "alternate.conf"
        alternate_path.write_text("subdustfeepenalty=0\n", encoding="utf-8")
        self.restart_node(0, extra_args=["-confrw=alternate.conf"])
        self.check_policy({"max_datacarrier_bytes": 94, "subdust_fee_penalty": False})
        self.restart_node(0, extra_args=[f"-confrw={alternate_path}"])
        self.check_policy({"max_datacarrier_bytes": 94, "subdust_fee_penalty": False})
        self.stop_node(0)
        alternate_path.unlink()
        rw_path.unlink()
        settings_path.write_text("{}", encoding="utf-8")
        config_path.write_text(base_config, encoding="utf-8")

        self.log.info("Invalid effective options prevent startup and cannot be reported")
        node.assert_start_raises_init_error(extra_args=["-subdustfeepenalty=1junk"], expected_msg="Error: Invalid -subdustfeepenalty value: '1junk' (expected empty, 0, or 1)")
        node.assert_start_raises_init_error(extra_args=["-datacarriercost=0.001"], expected_msg="Error: Invalid -datacarriercost value: '0.001' (expected a value from 0 to 1073741823.75 exactly representable in hundredths)")
        self.start_node(0)
        self.check_policy()
        self.log.info("Dynamic dust without an estimate reports the configured floor")
        self.restart_node(0, extra_args=["-dustrelayfee=0.00004", "-dustdynamic=mempool:1"])
        self.check_policy({"dust_relay_fee": Decimal("0.00004000")})
        self.restart_node(0)
        self.test_carrier_admission()
        self.test_subdust_admission()

    def test_carrier_admission(self):
        self.log.info("Reported carrier budget controls admission, independently of standardness")
        node = self.nodes[0]
        self.restart_node(0, extra_args=["-datacarriersize=5"])
        self.check_policy({"max_datacarrier_bytes": 5})
        wallet = MiniWallet(node)
        budget = node.getmempoolinfo()["policy"]["max_datacarrier_bytes"]

        def carrier_tx(payload_size):
            tx = wallet.create_self_transfer(fee_rate=Decimal("0.0001"))["tx"]
            tx.vout.append(CTxOut(0, CScript([OP_RETURN, b"x" * payload_size])))
            wallet.sign_tx(tx)
            return tx.serialize().hex()

        # Small direct pushes have one OP_RETURN and one push opcode overhead.
        exact_hex = carrier_tx(budget - 2)
        over_hex = carrier_tx(budget - 1)
        assert_equal(node.testmempoolaccept([exact_hex])[0]["allowed"], True)
        rejection = node.testmempoolaccept([over_hex])[0]
        assert_equal(rejection["allowed"], False)
        assert_equal(rejection["reject-reason"], "datacarrier")

        self.restart_node(0, extra_args=["-acceptnonstdtxn=1", "-datacarriersize=5"])
        self.check_policy({"require_standard": False, "max_datacarrier_bytes": 5})
        rejection = node.testmempoolaccept([over_hex])[0]
        assert_equal(rejection["allowed"], False)
        assert_equal(rejection["reject-reason"], "txn-datacarrier-exceeded")

        self.restart_node(0, extra_args=["-datacarrier=0"])
        self.check_policy({"datacarrier": False, "max_datacarrier_bytes": 0})
        assert_equal(node.testmempoolaccept([exact_hex])[0]["allowed"], False)
        # Local rejection does not prevent consensus-valid block acceptance.
        self.generateblock(node, output=wallet.get_address(), transactions=[over_hex])

    def test_subdust_admission(self):
        self.log.info("Reported live dust rate and penalty flag determine the effective fee")
        node = self.nodes[0]
        self.restart_node(0, extra_args=["-acceptnonstdtxn=1", "-dustrelayfee=0.00006"])
        self.check_policy({"require_standard": False, "dust_relay_fee": Decimal("0.00006000")})
        wallet = MiniWallet(node)
        utxo = wallet.get_utxo()
        info = node.getmempoolinfo()
        assert_equal(info["policy"]["subdust_fee_penalty"], True)
        # MiniWallet's P2TR output is 43 bytes, with a 67-vbyte dust spend estimate.
        p2tr_dust_size = 43 + 67
        dust_threshold = int(info["policy"]["dust_relay_fee"] * COIN) * p2tr_dust_size // 1000

        def dust_tx(fee):
            tx = wallet.create_self_transfer(utxo_to_spend=utxo, fee_rate=0)["tx"]
            tx.vout[0].nValue -= fee
            tx.vout.append(CTxOut(0, wallet.get_output_script()))
            wallet.sign_tx(tx)
            return tx

        min_relay_per_kvb = int(info["minrelaytxfee"] * COIN)
        probe = dust_tx(0)
        min_fee = (min_relay_per_kvb * probe.get_vsize() + 999) // 1000
        low_hex = dust_tx(min_fee + dust_threshold - 1).serialize().hex()
        high_hex = dust_tx(min_fee + dust_threshold).serialize().hex()
        rejection = node.testmempoolaccept([low_hex])[0]
        assert_equal(rejection["allowed"], False)
        assert_equal(rejection["reject-reason"], "min relay fee not met")
        assert_equal(node.testmempoolaccept([high_hex])[0]["allowed"], True)

        self.restart_node(0, extra_args=["-acceptnonstdtxn=1", "-dustrelayfee=0.00006", "-subdustfeepenalty=0"])
        self.check_policy({"require_standard": False, "dust_relay_fee": Decimal("0.00006000"), "subdust_fee_penalty": False})
        assert_equal(node.testmempoolaccept([low_hex])[0]["allowed"], True)
        assert_equal(node.testmempoolaccept([dust_tx(min_fee).serialize().hex()])[0]["allowed"], True)
        assert_equal(node.testmempoolaccept([dust_tx(min_fee - 1).serialize().hex()])[0]["allowed"], False)


if __name__ == '__main__':
    EffectivePolicyTest(__file__).main()
