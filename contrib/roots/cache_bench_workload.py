#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Copyright (C) 2026 Plan-B Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
"""Private workload entry point; launch with cache_bench.py run."""

from collections import Counter
from decimal import Decimal
import hashlib
import json
import os
from pathlib import Path
import sys
import threading
import time

sys.path.insert(0, str((FRAMEWORK := Path(os.environ.get("ROOTS_BENCH_FRAMEWORK", Path(__file__).resolve().parents[2])).resolve()) / "test/functional"))

from cache_bench import CASES, VARIANTS, case_name, check_environment, parse_metric, select_profile, write_json
from test_framework.blocktools import NORMAL_GBT_REQUEST_PARAMS, add_witness_commitment, create_block
from test_framework.messages import (
    BlockTransactions,
    CBlockHeader,
    CTxOut,
    HeaderAndShortIDs,
    msg_blocktxn,
    msg_cmpctblock,
    msg_sendcmpct,
    msg_tx,
)
from test_framework.p2p import P2PInterface, p2p_lock
from test_framework.script import CScript, OP_RETURN
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.wallet import MiniWallet


class Peer(P2PInterface):
    pass


def process_stats(pid):
    """Linux process scope, summed user/system CPU, not allocation accounting."""
    status = Path(f"/proc/{pid}/status").read_text()
    fields = dict(line.split(":", 1) for line in status.splitlines() if ":" in line)
    stat = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    return {"rss_bytes": int(fields["VmRSS"].split()[0]) * 1024,
            "lifetime_hwm_bytes": int(fields["VmHWM"].split()[0]) * 1024,
            "cpu_ns": (int(stat[11]) + int(stat[12])) * 1_000_000_000 // os.sysconf("SC_CLK_TCK")}


def check_reconstruction(metric, count, missing, *, root_reject, overlap, flags, offered_count):
    """Assert observable controls without assuming the historical 32768 slots."""
    assert_equal(metric["available"], count - len(missing))
    if root_reject:
        if overlap == 0 or "-blockreconstructionextratxn=0" in flags:
            assert_equal(len(missing), offered_count)
        elif overlap == 1 and metric["occupied"] >= offered_count:
            # Full hits are required only when the observed population actually
            # fits. Current 100-slot defaults cannot retain 128 distinct offers.
            assert_equal(len(missing), 0)


class MemorySampler:
    def __init__(self, pid):
        self.pid = pid
        self.stop = threading.Event()
        self.samples = []
        self.errors = []
        self.thread = threading.Thread(target=self.collect, daemon=True)

    def collect(self):
        while not self.stop.is_set():
            try:
                self.samples.append(process_stats(self.pid)["rss_bytes"])
            except OSError as error:
                self.errors.append(str(error))
            self.stop.wait(.005)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.stop.set()
        self.thread.join()


class CacheWorkload(BitcoinTestFramework):
    def set_test_params(self):
        check_environment()
        self.profile = select_profile((FRAMEWORK / "CMakeLists.txt").read_text(), os.environ.get("ROOTS_BENCH_PROFILE"))
        kind = os.environ["ROOTS_BENCH_KIND"]
        flags = json.loads(os.environ["ROOTS_BENCH_FLAGS"])
        if (kind, flags) not in VARIANTS.values() or not 2 <= int(os.environ["ROOTS_BENCH_COUNT"]) <= 1000:
            raise ValueError("Only declared bounded workload controls are allowed")
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.noban_tx_relay = False
        self.bind_to_localhost_only = True
        self.extra_args = [["-acceptnonstdtxn=0", "-persistmempool=0", "-dbcache=64", "-maxconnections=16",
                            "-connect=0", "-dnsseed=0", "-fixedseeds=0", "-discover=0", "-natpmp=0",
                            "-mocktime=1700000000",
                            *json.loads(os.environ["ROOTS_BENCH_FLAGS"])]]

    def make_transactions(self, kind, n):
        transactions, offered = [], []
        parents = []
        for i in range(n):
            utxo = self.wallet.get_utxo(confirmed_only=True)
            # Replacements and parents use the same well-funded confirmed input.
            info = self.wallet.create_self_transfer(utxo_to_spend=utxo)
            tx = info["tx"]
            if kind == "accepted":
                offered.append(tx)
            elif kind == "replaced":
                replacement = self.wallet.create_self_transfer(utxo_to_spend=utxo, fee_rate=Decimal("0.006"))["tx"]
                offered.extend((tx, replacement))
            elif kind == "orphan":
                parent = tx
                tx = self.wallet.create_self_transfer(utxo_to_spend=info["new_utxo"], fee_rate=0)["tx"]
                parents.append(parent)
                offered.append(tx)
            else:
                # CAT21 is a specific Roots gate. Core admits this same fixture;
                # record that mismatch instead of calling its mempool hits cache hits.
                tx.nLockTime = 21
                if kind == "mixed":
                    tx.vout.append(CTxOut(0, CScript([OP_RETURN, b"x" * (20 if i % 2 == 0 else 900)])))
                if kind in ("churn", "adversarial"):
                    # Four unique offers per input, fixed count/duration. Churn
                    # alternates resident candidates and fresh reject-filter keys.
                    from copy import deepcopy
                    for sequence in range(4):
                        unique = deepcopy(tx)
                        unique.vin[0].nSequence = 0xfffffffa - sequence
                        if kind == "adversarial":
                            unique.vout.append(CTxOut(0, CScript([OP_RETURN, b"a" * 60])))
                        offered.append(unique)
                    tx = offered[-1]
                else:
                    offered.append(tx)
            transactions.append(tx)
        return transactions, offered, parents

    def run_case(self, kind, overlap, reverse):
        node = self.nodes[0]
        # Clear mempool, orphanage, reject filters and extra cache between cells.
        self.restart_node(0)
        node.setmocktime(1700000000 + self.case_index * 60)
        self.case_index += 1
        assert_equal(node.getblockchaininfo()["initialblockdownload"], False)
        self.wallet = MiniWallet(node)
        self.wallet.rescan_utxos(include_mempool=False)
        peer = node.add_p2p_connection(Peer())
        peer.send_and_ping(msg_sendcmpct(announce=True, version=2))
        n = int(os.environ["ROOTS_BENCH_COUNT"])
        txs, offers, parents = self.make_transactions(kind, n)
        # Preflight every candidate against the initial state, outside timing.
        # Replacement alternatives and orphan children have separate denominators.
        preflight = [node.testmempoolaccept([tx.serialize().hex()])[0] for tx in offers]
        reasons = Counter(result.get("reject-reason", "allowed") for result in preflight)
        if kind == "rejected" and os.environ["ROOTS_BENCH_KIND"] == "roots" and "-rejectparasites=0" not in self.extra_args[0]:
            assert_equal(sum(result["allowed"] for result in preflight), 0)
            assert all("parasite" in reason.lower() or "cat21" in reason.lower() for reason in reasons), reasons
        if kind in ("accepted", "replaced"):
            assert all(result["allowed"] for result in preflight), reasons
        if kind == "orphan":
            assert all(result.get("reject-reason") == "missing-inputs" for result in preflight), reasons
        # Choose which known block transactions the sender offers. The half/zero
        # matrix uses rejected-only independent transactions, so ordering is valid.
        if kind == "rejected":
            offers = offers[:int(n * overlap)]
        if reverse:
            offers.reverse()
        before = process_stats(node.process.pid)
        started = time.perf_counter_ns()
        with MemorySampler(node.process.pid) as sampler:
            for tx in offers:
                peer.send_without_ping(msg_tx(tx))
            peer.sync_with_ping()
            if kind == "orphan":
                self.wait_until(lambda: len(node.getorphantxs()) == n)
            after_load = process_stats(node.process.pid)
            load_wall = time.perf_counter_ns() - started
            resident = node.getrawmempool()
            orphans = node.getorphantxs()
            if kind == "accepted":
                assert_equal(set(resident), {tx.txid_hex for tx in txs})
            if kind == "replaced":
                assert_equal(set(resident), {tx.txid_hex for tx in offers[1::2]})
            if kind == "orphan":
                assert_equal(len(orphans), n)
            # Parent(s) are supplied in the block, leaving orphan+cache shared
            # ownership observable immediately before InitData.
            block = create_block(tmpl=node.getblocktemplate(NORMAL_GBT_REQUEST_PARAMS))
            # Leave fees unclaimed so differing mempool templates cannot produce
            # an excessive coinbase or different serialized workload provenance.
            block.vtx[0].vout[0].nValue = 0
            block.vtx = block.vtx[:1] + parents + txs
            add_witness_commitment(block)
            block.solve()
            compact = HeaderAndShortIDs()
            compact.initialize_from_block(block, nonce=560031, use_witness=True)
            message = msg_cmpctblock(compact.to_p2p())
            peer.last_message.pop("getblocktxn", None)
            before_request_count = peer.message_count["getblocktxn"]
            setup_request_bytes = 0
            if self.profile == 'v32.1':
                # Native v32 ignores unsolicited compact blocks from peers it
                # has not selected for high-bandwidth announcements. Use the
                # same explicit in-flight request as the current cache fixture;
                # no peer permission or production admission rule is changed.
                peers = node.getpeerinfo()
                assert_equal(len(peers), 1)
                node.submitheader(CBlockHeader(block).serialize().hex())
                peer.last_message.pop('getdata', None)
                assert_equal(node.getblockfrompeer(block.hash_hex, peers[0]['id']), {})
                peer.wait_for_getdata([block.hash_int])
                setup_request_bytes = len(peer.last_message['getdata'].serialize()) + 24
            steady = process_stats(node.process.pid)
            block_started = time.perf_counter_ns()
            peer.send_and_ping(message)
            with p2p_lock:
                request = peer.last_message.get("getblocktxn")
            missing = request.block_txn_request.to_absolute() if request else []
            wire = len(message.serialize()) + 24
            response_bytes = request_bytes = 0
            if request:
                request_bytes = len(request.serialize()) + 24
                response = msg_blocktxn()
                response.block_transactions = BlockTransactions(block.hash_int, [block.vtx[i] for i in missing])
                response_bytes = len(response.serialize()) + 24
                peer.send_and_ping(response)
            self.wait_until(lambda: node.getbestblockhash() == block.hash_hex)
            block_wall = time.perf_counter_ns() - block_started
            final = process_stats(node.process.pid)
        if sampler.errors or not sampler.samples:
            raise RuntimeError(f"RSS capture incomplete: {sampler.errors}")
        log = node.datadir_path / "regtest/debug.log"
        metrics = [parse_metric(line) for line in log.read_text().splitlines() if "ISSUE56 block=" + block.hash_hex in line]
        assert_equal(len(metrics), 1)
        metric = metrics[0]
        count = len(block.vtx) - 1
        check_reconstruction(metric, count, missing, root_reject=(kind == "rejected"
            and os.environ["ROOTS_BENCH_KIND"] == "roots" and "-rejectparasites=0" not in self.extra_args[0]),
            overlap=overlap, flags=self.extra_args[0], offered_count=n)
        fixture = b"".join(tx.serialize() for tx in parents + txs)
        row = {"case": case_name(kind, overlap, reverse),
               "kind": kind, "overlap_offered": overlap, "reverse": reverse,
               "fixture_sha256": hashlib.sha256(fixture).hexdigest(),
               "offered": len(offers), "preflight_candidates": len(preflight), "preflight_reasons": dict(reasons),
               "resident_after_load": len(resident), "orphans_after_load": len(orphans),
               "block_transactions": count, "requested_missing": len(missing),
               "cache_hits": metric["extra_hits"], "mempool_hits": metric["available"] - metric["extra_hits"],
               "roundtrips": peer.message_count["getblocktxn"] - before_request_count,
               "compact_wire_bytes": wire, "request_wire_bytes": request_bytes,
               "setup_getdata_wire_bytes": setup_request_bytes,
               "response_wire_bytes": response_bytes, "block_wire_bytes": wire + request_bytes + response_bytes,
               "offer_wire_bytes": sum(len(msg_tx(tx).serialize()) + 24 for tx in offers),
               "full_block_wire_bytes": len(block.serialize()) + 24,
               "load_wall_ns": load_wall, "load_process_cpu_ns": after_load["cpu_ns"] - before["cpu_ns"],
               "insertion_cpu_ns_per_offer": metric["insert_cpu_ns"] / len(offers) if offers else None,
               "insertion_cpu_ns_per_call": metric["insert_cpu_ns"] / metric["insert_calls"] if metric["insert_calls"] else None,
               "insertion_cpu_ns_per_store": metric["stored_cpu_ns"] / metric["stored_calls"] if metric["stored_calls"] else None,
               "insertion_wall_ns_per_store": metric["stored_wall_ns"] / metric["stored_calls"] if metric["stored_calls"] else None,
               "block_accept_wall_ns": block_wall, "block_process_cpu_ns": final["cpu_ns"] - steady["cpu_ns"],
               "baseline_rss_bytes": before["rss_bytes"], "steady_rss_bytes": steady["rss_bytes"],
               "sampled_peak_rss_bytes": max(sampler.samples), "rss_samples": len(sampler.samples),
               "lifetime_hwm_bytes": final["lifetime_hwm_bytes"], "block_accepted": True, **metric}
        self.rows.append(row)
        write_json(os.environ["ROOTS_BENCH_RESULT"], self.rows)
        node.disconnect_p2ps()

    def run_test(self):
        self.rows = []
        self.case_index = 0
        node = self.nodes[0]
        node.setmocktime(1700000000)
        wallet = MiniWallet(node)
        self.generate(wallet, int(os.environ["ROOTS_BENCH_COUNT"]) + 105)
        for kind, overlap, reverse in CASES:
            self.log.info("Workload cell %s", case_name(kind, overlap, reverse))
            self.run_case(kind, overlap, reverse)


if __name__ == "__main__":
    CacheWorkload(__file__).main()
