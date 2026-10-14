#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit.
"""Roots permission, transport and Tor-bind resource boundaries."""
from queue import Empty

from test_framework.messages import NODE_BLOOM, NODE_COMPACT_FILTERS, msg_getcfilters
from test_framework.p2p import P2PInterface
from test_framework.socks5 import Socks5Command, Socks5Configuration, Socks5Server
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, p2p_port


class RootsPeerControlsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]
        self.log.info("Fee-filter quantization retains source incremental-fee profiles")
        for incremental, expected in [('0.00000100', 9936506), ('0.00001000', 9170997)]:
            self.restart_node(0, [f'-incrementalrelayfee={incremental}'])
            peer = node.add_p2p_connection(P2PInterface())
            peer.wait_until(lambda: 'feefilter' in peer.last_message)
            assert_equal(peer.last_message['feefilter'].feerate, expected)
            node.disconnect_p2ps()
        self.restart_node(0)

        self.log.info("Default bloom service is loopback-only; explicit disable removes it")
        peer = node.add_p2p_connection(P2PInterface())
        assert 'bloomfilter' in node.getpeerinfo()[0]['permissions']
        assert int(node.getnetworkinfo()['localservices'], 16) & NODE_BLOOM == 0
        assert peer.last_message['version'].nServices & NODE_BLOOM
        node.disconnect_p2ps()
        self.restart_node(0, ['-peerbloomfilters=0'])
        peer = node.add_p2p_connection(P2PInterface())
        assert_equal(node.getpeerinfo()[0]['permissions'], [])
        assert peer.last_message['version'].nServices & NODE_BLOOM == 0
        node.disconnect_p2ps()

        self.log.info("Explicit compact filter permission serves native BIP157 without global advertisement")
        self.restart_node(0, ['-blockfilterindex=1', '-whitelist=cfilters@127.0.0.1'])
        peer = node.add_p2p_connection(P2PInterface())
        assert peer.last_message['version'].nServices & NODE_COMPACT_FILTERS
        assert int(node.getnetworkinfo()['localservices'], 16) & NODE_COMPACT_FILTERS == 0
        peer.send_and_ping(msg_getcfilters(filter_type=0, start_height=0, stop_hash=int(node.getbestblockhash(), 16)))
        peer.wait_until(lambda: 'cfilter' in peer.last_message)
        node.disconnect_p2ps()

        self.log.info("Onion bind does not inherit address whitelist or loopback bloom permission")
        port = p2p_port(2)
        self.restart_node(0, [f'-bind=127.0.0.1:{port}=onion', '-whitelist=all@127.0.0.1'])
        node.add_p2p_connection(P2PInterface(), dstport=port)
        assert_equal(node.getpeerinfo()[0]['permissions'], [])
        node.disconnect_p2ps()
        # First normal bind is the Tor target when no explicit onion bind exists.
        self.restart_node(0, ['-listenonion=1', '-whitelist=all@127.0.0.1'])
        node.add_p2p_connection(P2PInterface())
        assert_equal(node.getpeerinfo()[0]['permissions'], [])
        node.disconnect_p2ps()

        self.log.info("Forceinbound frees a protected slot but cannot evict a noban connection")
        self.restart_node(0, ['-listenonion=0', '-maxconnections=12', f'-whitebind=forceinbound@127.0.0.1:{port}'])
        ordinary = node.add_p2p_connection(P2PInterface())
        node.add_p2p_connection(P2PInterface(), dstport=port)
        ordinary.wait_for_disconnect()
        assert 'noban' in node.getpeerinfo()[0]['permissions']
        assert_equal(node.getpeerinfo()[0]['forced_inbound'], True)
        blocked = node.add_p2p_connection(P2PInterface(), dstport=port, wait_for_verack=False, expect_success=False)
        blocked.wait_for_disconnect()
        assert_equal(len(node.getpeerinfo()), 1)
        node.disconnect_p2ps()

        self.log.info("Duplicate CJDNS manual addresses are rejected regardless of port")
        self.restart_node(0, ['-cjdnsreachable=1', '-listenonion=0'])
        node.setnetworkactive(False)
        node.addnode('[fc00::1]:1234', 'add')
        assert_raises_rpc_error(-23, "Node already added", node.addnode, '[fc00::1]:1235', 'add')
        node.addnode('[fc00::1]:1234', 'remove')

        self.log.info("Clearnet v2-only suppresses explicit v1 outbound and v1 fallback")
        conf = Socks5Configuration()
        conf.addr = ('127.0.0.1', 0)
        conf.unauth = True
        conf.auth = True
        conf.destinations_factory = lambda address, port: {'actual_to_addr': '127.0.0.1', 'actual_to_port': p2p_port(1)}
        proxy = Socks5Server(conf)

        def next_proxy_command():
            # Forwarders can report an expected closed socket after a deliberate
            # disconnect. It is not another SOCKS request or a transport retry.
            while True:
                item = proxy.queue.get(timeout=5 * self.options.timeout_factor)
                if isinstance(item, (BrokenPipeError, ConnectionResetError)):
                    continue
                assert isinstance(item, Socks5Command), item
                return item

        proxy.start()
        try:
            self.restart_node(1, ['-v2transport=1'])
            self.restart_node(0, ['-v2transport=1', '-v2onlyclearnet=1', '-listenonion=0', f'-proxy=127.0.0.1:{conf.addr[1]}'])
            target = '1.1.1.1:8333'  # The test proxy redirects all traffic to our isolated node.
            with node.assert_debug_log(['outbound v1 disabled on clearnet']):
                node.addnode(target, 'onetry', False)
            assert proxy.queue.empty()
            assert_equal(node.getpeerinfo(), [])
            with node.assert_debug_log(['SOCKS5 sending username/password authentication'], unexpected_msgs=['SOCKS5 sending proxy authentication']):
                node.addnode(target, 'onetry', True)
                self.wait_until(lambda: len(node.getpeerinfo()) == 1 and node.getpeerinfo()[0]['transport_protocol_type'] == 'v2')
            next_proxy_command()
            node.disconnectnode(nodeid=node.getpeerinfo()[0]['id'])
            self.wait_until(lambda: not node.getpeerinfo())
            self.restart_node(1, ['-v2transport=0'])
            with node.assert_debug_log(['trying v2 connection'], unexpected_msgs=['retrying with v1']):
                node.addnode(target, 'onetry', True)
                next_proxy_command()
                self.wait_until(lambda: not node.getpeerinfo())
            # No second proxy connection is made for a v1 retry. Ignore only
            # expected forwarder close errors; every extra request still fails.
            while True:
                try:
                    item = proxy.queue.get_nowait()
                except Empty:
                    break
                assert isinstance(item, (BrokenPipeError, ConnectionResetError)), item
        finally:
            proxy.stop()



if __name__ == '__main__':
    RootsPeerControlsTest(__file__).main()
