#!/usr/bin/env python3
# Copyright (c) 2015-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test p2p permission message.

Test that permissions are correctly calculated and applied
"""

from test_framework.messages import (
    SEQUENCE_FINAL,
)
from test_framework.p2p import P2PDataStore, P2PInterface
from test_framework.test_node import ErrorMatch
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    append_config,
    assert_equal,
    p2p_port,
    tor_port,
)
from test_framework.wallet import MiniWallet


class P2PPermissionsTests(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2

    def run_test(self):
        self.wallet = MiniWallet(self.nodes[0])

        self.check_tx_relay()

        self.checkpermission(
            # default permissions (no specific permissions)
            ["-whitelist=127.0.0.1"],
            # Make sure the default values in the command line documentation match the ones here
            ["relay", "noban", "mempool", "download"])

        self.checkpermission(
            # no permission (even with forcerelay)
            ["-whitelist=@127.0.0.1", "-whitelistforcerelay=1"],
            [])

        self.checkpermission(
            # relay permission removed (no specific permissions)
            ["-whitelist=127.0.0.1", "-whitelistrelay=0"],
            ["noban", "mempool", "download"])

        self.checkpermission(
            # forcerelay and relay permission added
            # Legacy parameter interaction which set whitelistrelay to true
            # if whitelistforcerelay is true
            ["-whitelist=127.0.0.1", "-whitelistforcerelay"],
            ["forcerelay", "relay", "noban", "mempool", "download"])

        self.checkpermission(["-whitelist=addr@127.0.0.1"], ["addr"])
        self.checkpermission(["-whitelist=forceinbound@127.0.0.1"], ["noban", "download", "forceinbound"])

        # Socket and range permissions are additive, including when one side
        # uses defaults. An empty explicit permission set cannot remove Addr.
        ip_port = f"127.0.0.1:{p2p_port(1)}"
        for socket_permissions, args, expected in [
            ("", [], ["relay", "noban", "mempool", "download"]),
            ("", ["-whitelistrelay=0"], ["noban", "mempool", "download"]),
            ("", ["-whitelistforcerelay=1"], ["forcerelay", "relay", "noban", "mempool", "download"]),
            ("", ["-whitelist=addr@127.0.0.1"], ["addr", "relay", "noban", "mempool", "download"]),
            ("addr@", ["-whitelist=127.0.0.1"], ["addr", "relay", "noban", "mempool", "download"]),
            ("addr@", ["-whitelist=@127.0.0.1"], ["addr"]),
            ("@", ["-whitelist=addr@127.0.0.1"], ["addr"]),
            ("forceinbound@", [], ["noban", "download", "forceinbound"]),
            ("all@", [], ["blockfilters", "bloomfilter", "noban", "forcerelay", "relay", "mempool", "download", "addr", "forceinbound"]),
        ]:
            whitebind = f"whitebind={socket_permissions}{ip_port}"
            self.nodes[1].replace_in_config([("bind=127.0.0.1\n", whitebind + "\n")])
            try:
                self.checkpermission(args, expected)
            finally:
                self.nodes[1].replace_in_config([(whitebind + "\n", "bind=127.0.0.1\n")])

        # Let's make sure permissions are merged correctly
        # For this, we need to use whitebind instead of bind
        # by modifying the configuration file.
        ip_port = "127.0.0.1:{}".format(p2p_port(1))
        self.nodes[1].replace_in_config([("bind=127.0.0.1", "whitebind=bloomfilter,forcerelay@" + ip_port)])
        # Explicitly bind the tor port to prevent collisions with the default tor port
        append_config(self.nodes[1].datadir_path, [f"bind=127.0.0.1:{tor_port(self.nodes[1].index)}=onion"])
        self.checkpermission(
            ["-whitelist=noban@127.0.0.1"],
            # Check parameter interaction forcerelay should activate relay
            ["noban", "bloomfilter", "forcerelay", "relay", "download"])
        self.nodes[1].replace_in_config([("whitebind=bloomfilter,forcerelay@" + ip_port, "bind=127.0.0.1")])
        self.nodes[1].replace_in_config([(f"bind=127.0.0.1:{tor_port(self.nodes[1].index)}=onion", "")])

        self.checkpermission(
            # legacy whitelistrelay should be ignored
            ["-whitelist=noban,mempool@127.0.0.1", "-whitelistrelay"],
            ["noban", "mempool", "download"])

        self.checkpermission(
            # legacy whitelistforcerelay should be ignored
            ["-whitelist=noban,mempool@127.0.0.1", "-whitelistforcerelay"],
            ["noban", "mempool", "download"])

        self.checkpermission(
            # missing mempool permission to be considered legacy whitelisted
            ["-whitelist=noban@127.0.0.1"],
            ["noban", "download"])

        self.checkpermission(
            # all permission added
            ["-whitelist=all@127.0.0.1"],
            [
                "blockfilters",
                "forcerelay",
                "noban",
                "mempool",
                "bloomfilter",
                "relay",
                "download",
                "addr",
                "forceinbound",
            ])

        for flag, permissions in [(["-whitelist=noban,out@127.0.0.1"], ["bloomfilter", "noban", "download"]), (["-whitelist=noban@127.0.0.1"], ["bloomfilter"])]:
            self.restart_node(0, flag)
            self.connect_nodes(0, 1)
            peerinfo = self.nodes[0].getpeerinfo()[0]
            assert_equal(peerinfo['permissions'], permissions)

        self.check_outgoing_permissions()

        self.stop_node(1)
        self.nodes[1].assert_start_raises_init_error(["-whitelist=in,out@127.0.0.1"], "Only direction was set, no permissions", match=ErrorMatch.PARTIAL_REGEX)
        self.nodes[1].assert_start_raises_init_error(["-whitelist=oopsie@127.0.0.1"], "Invalid P2P permission", match=ErrorMatch.PARTIAL_REGEX)
        self.nodes[1].assert_start_raises_init_error(["-whitelist=noban@127.0.0.1:230"], "Invalid netmask specified in", match=ErrorMatch.PARTIAL_REGEX)
        self.nodes[1].assert_start_raises_init_error(["-whitebind=noban@127.0.0.1/10"], "Cannot resolve -whitebind address", match=ErrorMatch.PARTIAL_REGEX)
        self.nodes[1].assert_start_raises_init_error(["-whitebind=noban@127.0.0.1", "-bind=127.0.0.1", "-listen=0"], "Cannot set -bind or -whitebind together with -listen=0", match=ErrorMatch.PARTIAL_REGEX)
        for permission_arg in [
            "-whitelist=blockfilters@127.0.0.1",
            f"-whitebind=blockfilters@127.0.0.1:{p2p_port(self.nodes[1].index)}",
        ]:
            self.nodes[1].assert_start_raises_init_error(
                [permission_arg],
                "Cannot grant blockfilters permission without -blockfilterindex.",
                match=ErrorMatch.PARTIAL_REGEX,
            )

    def check_tx_relay(self):
        self.log.debug("Create a connection from a forcerelay peer that rebroadcasts raw txs")
        # A test framework p2p connection is needed to send the raw transaction directly. If a full node was used, it could only
        # rebroadcast via the inv-getdata mechanism. However, even for forcerelay connections, a full node would
        # currently not request a txid that is already in the mempool.
        self.restart_node(1, extra_args=["-whitelist=forcerelay@127.0.0.1"])
        p2p_rebroadcast_wallet = self.nodes[1].add_p2p_connection(P2PDataStore())

        self.log.debug("Send a tx from the wallet initially")
        tx = self.wallet.create_self_transfer(sequence=SEQUENCE_FINAL)['tx']
        txid = tx.txid_hex

        self.log.debug("Wait until tx is in node[1]'s mempool")
        p2p_rebroadcast_wallet.send_txs_and_test([tx], self.nodes[1])

        self.log.debug("Check that node[1] will send the tx to node[0] even though it is already in the mempool")
        self.connect_nodes(1, 0)
        with self.nodes[1].assert_debug_log(["Force relaying tx {} (wtxid={}) from peer=0".format(txid, tx.wtxid_hex)]):
            p2p_rebroadcast_wallet.send_txs_and_test([tx], self.nodes[1])
            self.wait_until(lambda: txid in self.nodes[0].getrawmempool())

        self.log.debug("Check that node[1] will not send an invalid tx to node[0]")
        tx.vout[0].nValue += 1
        # add dust to cause policy rejection but no disconnection
        tx.vout.append(tx.vout[0])
        tx.vout[-1].nValue = 0
        txid = tx.txid_hex
        # Send the transaction twice. The first time, it'll be rejected by ATMP because it conflicts
        # with a mempool transaction. The second time, it'll be in the m_lazy_recent_rejects filter.
        p2p_rebroadcast_wallet.send_txs_and_test(
            [tx],
            self.nodes[1],
            success=False,
            reject_reason='{} (wtxid={}) from peer=0 was not accepted: dust'.format(txid, tx.wtxid_hex)
        )

        p2p_rebroadcast_wallet.send_txs_and_test(
            [tx],
            self.nodes[1],
            success=False,
            reject_reason='Not relaying non-mempool transaction {} (wtxid={}) from forcerelay peer=0'.format(txid, tx.wtxid_hex)
        )

    def check_outgoing_permissions(self):
        node = self.nodes[0]
        trusted = ["noban", "relay", "download", "addr"]
        for permission, expected in [
            ("noban,addr,relay,out@127.0.0.1", trusted),
            ("noban,addr,relay,in,out@127.0.0.1", trusted),
            ("noban,addr,relay,in@127.0.0.1", []),
            ("noban,addr,relay,out@192.0.2.0/24", []),
        ]:
            args = ["-peerbloomfilters=0", f"-whitelist={permission}"]
            self.log.info(f"Test outgoing permission range {permission}")
            self.restart_node(0, args)
            self.connect_nodes(0, 1)  # addnode onetry creates a MANUAL connection
            info = node.getpeerinfo()[0]
            assert_equal(info["connection_type"], "manual")
            assert_equal(info["permissions"], expected)
            node.disconnectnode(nodeid=info["id"])
            self.wait_until(lambda: not node.getpeerinfo())
            for connection_type in ["outbound-full-relay", "block-relay-only", "addr-fetch"]:
                node.add_outbound_p2p_connection(P2PInterface(), p2p_idx=0, connection_type=connection_type)
                info = node.getpeerinfo()[0]
                assert_equal(info["connection_type"], connection_type)
                assert_equal(info["permissions"], [])
                node.disconnect_p2ps()

        target = f"127.0.0.1:{p2p_port(1)}"
        for option in ["-addnode", "-connect"]:
            self.log.info(f"Test manual permission inheritance through {option}")
            self.restart_node(0, ["-peerbloomfilters=0", "-whitelist=noban,addr,relay,out@127.0.0.1", f"{option}={target}"])
            self.wait_until(lambda: len(node.getpeerinfo()) == 1 and node.getpeerinfo()[0]["version"] > 0)
            info = node.getpeerinfo()[0]
            assert_equal(info["connection_type"], "manual")
            assert_equal(info["permissions"], trusted)
            if option == "-addnode":
                assert_equal(node.getaddednodeinfo()[0]["connected"], True)
                node.addnode(target, "remove")
            self.restart_node(0, ["-peerbloomfilters=0"])

        args = ["-v2transport=1", "-peerbloomfilters=0", "-whitelist=noban,addr,relay,out@127.0.0.1"]
        self.log.info("Successful v2 connections preserve the permission boundary")
        self.restart_node(1, ["-v2transport=1"])
        self.restart_node(0, args)
        self.connect_nodes(0, 1)
        info = node.getpeerinfo()[0]
        assert_equal(info["transport_protocol_type"], "v2")
        assert_equal(info["connection_type"], "manual")
        assert_equal(info["permissions"], trusted)
        node.disconnectnode(nodeid=info["id"])
        self.wait_until(lambda: not node.getpeerinfo())
        for index, connection_type in enumerate(["outbound-full-relay", "block-relay-only", "addr-fetch"]):
            node.add_outbound_p2p_connection(P2PInterface(), p2p_idx=index + 4, connection_type=connection_type)
            info = node.getpeerinfo()[0]
            assert_equal(info["transport_protocol_type"], "v2")
            assert_equal(info["connection_type"], connection_type)
            assert_equal(info["permissions"], [])
            node.disconnect_p2ps()

        self.log.info("V2 retry preserves manual versus automatic permission boundaries")
        self.restart_node(1, ["-v2transport=0"])
        self.restart_node(0, args)
        with node.assert_debug_log(["retrying with v1"]):
            node.addnode(target, "onetry", True)
            self.wait_until(lambda: len(node.getpeerinfo()) == 1 and node.getpeerinfo()[0]["transport_protocol_type"] == "v1" and node.getpeerinfo()[0]["version"] > 0)
        info = node.getpeerinfo()[0]
        assert_equal(info["connection_type"], "manual")
        assert_equal(info["permissions"], trusted)
        node.disconnectnode(nodeid=info["id"])
        self.wait_until(lambda: not node.getpeerinfo())
        for index, connection_type in enumerate(["outbound-full-relay", "block-relay-only", "addr-fetch"]):
            with node.assert_debug_log(["retrying with v1"]):
                # Each fallback uses a fresh listener: its factory must retain
                # the same protocol instance across the two connections.
                node.add_outbound_p2p_connection(P2PInterface(), p2p_idx=index + 1, connection_type=connection_type, supports_v2_p2p=False, advertise_v2_p2p=True)
            info = node.getpeerinfo()[0]
            assert_equal(info["transport_protocol_type"], "v1")
            assert_equal(info["connection_type"], connection_type)
            assert_equal(info["permissions"], [])
            node.disconnect_p2ps()

    def checkpermission(self, args, expectedPermissions):
        self.restart_node(1, ['-peerbloomfilters=0'] + args)
        self.connect_nodes(0, 1)
        peerinfo = self.nodes[1].getpeerinfo()[0]
        # These connections have a free slot, including when ForceInbound is
        # granted. The state reports eviction, not permission possession.
        assert_equal(peerinfo['forced_inbound'], False)
        assert_equal(len(expectedPermissions), len(peerinfo['permissions']))
        for p in expectedPermissions:
            if p not in peerinfo['permissions']:
                raise AssertionError("Expected permissions %r is not granted." % p)


if __name__ == '__main__':
    P2PPermissionsTests(__file__).main()
