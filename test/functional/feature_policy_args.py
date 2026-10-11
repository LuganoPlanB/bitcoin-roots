#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Check Roots policy grammar at startup and preserve configuration precedence."""

import json
import re

from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_node import ErrorMatch
from test_framework.util import assert_equal


class PolicyArgsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.uses_wallet = None
        self.wallet_names = []

    def setup_network(self):
        self.add_nodes(self.num_nodes)

    def run_test(self):
        node = self.nodes[0]
        config_path = node.datadir_path / "bitcoin.conf"
        base_config = config_path.read_text(encoding="utf-8")
        passed_startups = 0
        rejected_startups = 0

        def check_start(cli=None, config="", *, default_config=""):
            nonlocal passed_startups
            config_path.write_text(default_config + base_config + config, encoding="utf-8")
            self.start_node(0, extra_args=cli or [])
            assert_equal(node.getblockchaininfo()["chain"], "regtest")
            self.stop_node(0)
            passed_startups += 1

        def check_error(option, value, *, config=False, negated=False, cli=None):
            nonlocal rejected_startups
            key = ("no" if negated else "") + option
            config_path.write_text(base_config + (f"{key}={value}\n" if config else ""), encoding="utf-8")
            args = ([] if config else [f"-{key}={value}"]) + (cli or [])
            node.assert_start_raises_init_error(
                extra_args=args,
                expected_msg=re.escape(f"Invalid -{key} value: '{value}'"),
                match=ErrorMatch.PARTIAL_REGEX,
            )
            rejected_startups += 1

        self.log.info("Default policy configuration starts unchanged")
        check_start()

        self.log.info("Malformed effective values fail equally on CLI and in config")
        invalid = {
            "subdustfeepenalty": "1junk",
            "rejectparasites": "2",
            "acceptunknownwitness": "+1",
            "datacarrierfullcount": "01",
            "permitbareanchor": "false",
            "rejecttokens": "-1",
            "permitbarepubkey": "0.5",
            "permitbaredatacarrier": "junk",
            "acceptnonstddatacarrier": "1.0",
            "maxscriptsize": "12junk",
            "bytespersigopstrict": "4294967296",
            "maxtxlegacysigops": "-1",
            "minrelaycoinblocks": "9223372036854775808",
            "minrelaymaturity": "2147483648",
            "blockmaxsize": "4000001",
            "blockprioritysize": "1.5",
            "datacarriercost": "1073741823.76",
            "mempoolreplacement": "fee,junk",
            "mempooltruc": "accept,,enforce",
            "permitephemeral": "anchor,",
            "spkreuse": "garbage",
        }
        for option, value in invalid.items():
            check_error(option, value)
            check_error(option, value, config=True)

        self.log.info("Negation is checked before coercion, even when shadowed")
        for option in [*invalid, "dustdynamic"]:
            check_error(option, "junk", negated=True, cli=[f"-{option}=0"])
            check_error(option, "junk", config=True, negated=True, cli=[f"-{option}=0"])

        self.log.info("Empty integer values and CLI whitespace cannot partially parse")
        check_error("maxscriptsize", "")
        check_error("maxscriptsize", "", config=True)
        check_error("subdustfeepenalty", " 1")
        check_error("datacarriercost", "1 ")
        config_path.write_text(base_config, encoding="utf-8")
        node.assert_start_raises_init_error(
            extra_args=["-maxscriptsize"],
            expected_msg="Invalid -maxscriptsize value: ''",
            match=ErrorMatch.PARTIAL_REGEX,
        )
        rejected_startups += 1

        self.log.info("Valid boundaries, aliases, exponent precision and whitespace formatting remain supported")
        valid = {
            "subdustfeepenalty": "", "rejectparasites": "0", "acceptunknownwitness": "1",
            "datacarrierfullcount": "1", "permitbareanchor": "1", "rejecttokens": "0",
            "permitbarepubkey": "0", "permitbaredatacarrier": "0", "acceptnonstddatacarrier": "0",
            "maxscriptsize": "+1650", "bytespersigopstrict": "020", "maxtxlegacysigops": "4294967295",
            "minrelaycoinblocks": "9223372036854775807", "minrelaymaturity": "2147483647",
            "blockmaxsize": "4000000", "blockprioritysize": "9223372036854775807",
            "datacarriercost": "1.2300", "dustdynamic": "off", "mempoolreplacement": "fee+-optin",
            "mempooltruc": "accept,enforce", "permitephemeral": "0,anchor", "spkreuse": "conflict",
        }
        check_start([f"-{option}={value}" for option, value in valid.items()])
        check_start(config="".join(f"{option} = {value} \n" for option, value in valid.items()))
        for cost in ["-0", "0.01", "0.25", "0.26", "1e0", "1e-2", "1073741823.75"]:
            check_start([f"-datacarriercost={cost}"])
        check_start(["-subdustfeepenalty", "-mempoolreplacement", "-mempooltruc", "-permitephemeral", "-spkreuse"])
        check_start(["-nosubdustfeepenalty", "-nomaxscriptsize=0", "-nodatacarriercost", "-nomempoolreplacement", "-nomempooltruc", "-nopermitephemeral", "-nospkreuse"])
        check_start(["-nosubdustfeepenalty=0", "-maxscriptsize=-0"])

        self.log.info("Last CLI, first config, network section and CLI-over-config selection remain unchanged")
        check_start(["-subdustfeepenalty=junk", "-subdustfeepenalty=1"], config="subdustfeepenalty=junk\n")
        check_start(config="subdustfeepenalty=1\nsubdustfeepenalty=junk\n")
        check_start(default_config="subdustfeepenalty=junk\n", config="subdustfeepenalty=1\n")
        config_path.write_text(base_config, encoding="utf-8")
        node.assert_start_raises_init_error(extra_args=["-subdustfeepenalty=1", "-subdustfeepenalty=junk"], expected_msg="Invalid -subdustfeepenalty value: 'junk'", match=ErrorMatch.PARTIAL_REGEX)
        rejected_startups += 1
        config_path.write_text(base_config + "subdustfeepenalty=junk\nsubdustfeepenalty=1\n", encoding="utf-8")
        node.assert_start_raises_init_error(extra_args=[], expected_msg="Invalid -subdustfeepenalty value: 'junk'", match=ErrorMatch.PARTIAL_REGEX)
        rejected_startups += 1

        self.log.info("Inherited Core numeric and Boolean parsers retain permissive input")
        check_start(["-bytespersigop=20junk", "-datacarriersize=83junk", "-datacarrier=1junk", "-permitbaremultisig=2", "-mempoolfullrbf=1junk"])
        check_start(config="bytespersigop=20junk\ndatacarriersize=83junk\ndatacarrier=1junk\npermitbaremultisig=2\nmempoolfullrbf=1junk\n")

        # Only malformed dynamic modes are startup-tested. Valid non-off grammar
        # is unit-tested without constructing a mempool: production scheduler
        # wiring is an existing separate runtime gap, not a successful startup.
        config_path.write_text(base_config, encoding="utf-8")
        node.assert_start_raises_init_error(extra_args=["-dustdynamic=target:2junk"], expected_msg="Invalid mode for dustdynamic: failed to parse target block count", match=ErrorMatch.PARTIAL_REGEX)
        rejected_startups += 1

        self.log.info("JSON settings use the same effective grammar; confrw integration belongs L12")
        settings_path = node.chain_path / "settings.json"
        for value in [True, False]:
            settings_path.write_text(json.dumps({"subdustfeepenalty": value}), encoding="utf-8")
            check_start(config="subdustfeepenalty=junk\n")
        settings_path.write_text(json.dumps({"subdustfeepenalty": 2}), encoding="utf-8")
        config_path.write_text(base_config, encoding="utf-8")
        node.assert_start_raises_init_error(extra_args=[], expected_msg="Invalid -subdustfeepenalty value: '2'", match=ErrorMatch.PARTIAL_REGEX)
        rejected_startups += 1
        for value in [[], {}]:
            settings_path.write_text(json.dumps({"subdustfeepenalty": value}), encoding="utf-8")
            node.assert_start_raises_init_error(extra_args=[], expected_msg="Invalid -subdustfeepenalty value: expected a scalar policy value", match=ErrorMatch.PARTIAL_REGEX)
            rejected_startups += 1
        check_start(["-subdustfeepenalty=1"])
        settings_path.write_text("{}", encoding="utf-8")
        config_path.write_text(base_config, encoding="utf-8")
        self.log.info("Policy startup cases: %d passed starts, %d expected diagnostic rejections", passed_startups, rejected_startups)


if __name__ == '__main__':
    PolicyArgsTest(__file__).main()
