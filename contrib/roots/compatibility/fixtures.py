# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Deterministic shared-byte fixtures; Core-derived serialization stays read-only."""

from copy import deepcopy
from decimal import Decimal
import hashlib
import importlib
from pathlib import Path
import sys
import time

FUNDING_HEIGHT = 110
GENESIS_TIME = 1296688602
SUBDUST_FLAGS = ("-acceptnonstdtxn=1", "-persistmempool=1")
SUBDUST_ROOTS_FLAGS = (*SUBDUST_FLAGS, "-subdustfeepenalty=1")


def load_framework(source):
    framework = Path(source).resolve() / "test" / "functional" / "test_framework"
    if not framework.is_dir():
        raise ValueError("explicit Roots serialization framework is missing")
    existing = sys.modules.get("test_framework")
    if existing and Path(existing.__file__).resolve().parent != framework:
        raise ValueError("another serialization framework is already loaded")
    sys.path.insert(0, str(framework.parent))
    try:
        modules = {name: importlib.import_module("test_framework." + name)
                   for name in ("messages", "blocktools", "script", "script_util", "address")}
    finally:
        sys.path.pop(0)
    provenance = []
    for name, module in sorted(sys.modules.items()):
        if name.startswith("test_framework") and getattr(module, "__file__", None):
            path = Path(module.__file__).resolve()
            if not path.is_relative_to(framework):
                raise ValueError("serialization import escaped explicit framework")
            provenance.append({"path": str(path.relative_to(Path(source).resolve())),
                               "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    return modules, provenance


def normalized_utxo(value):
    if value is None:
        return None
    return {"bestblock": value["bestblock"], "confirmations": value["confirmations"],
            "value_sats": int(Decimal(str(value["value"])) * 100000000),
            "script_hex": value["scriptPubKey"]["hex"], "coinbase": value["coinbase"]}


def compare_snapshots(snapshots, error):
    if snapshots["core"] != snapshots["roots"]:
        raise error("paired chain state diverged")
    return snapshots["core"]


def assert_block_outcomes(outcomes, valid, error):
    accepted = {label: value is None for label, value in outcomes.items()}
    if accepted != {"core": valid, "roots": valid}:
        raise error("paired block acceptance diverged from expected validity")
    if not valid and any(not isinstance(value, str) or not value for value in outcomes.values()):
        raise error("malformed invalid-block rejection result")


class FixtureRunner:
    def __init__(self, nodes, args, report, error):
        self.nodes, self.args, self.report, self.error = nodes, args, report, error
        modules, provenance = load_framework(args.roots_source)
        self.m, self.b, self.s, self.u, self.a = (modules[key] for key in
                                                ("messages", "blocktools", "script", "script_util", "address"))
        report["serialization"] = {"source_commit": args.roots_commit, "files": provenance}
        report["fixture_revision"] = {"path": "contrib/roots/compatibility/fixtures.py",
                                      "sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
        _, self.taproot = self.a.create_deterministic_address_bcrt1_p2tr_op_true()
        self.output_script = self.s.CScript([self.s.OP_1, self.taproot.output_pubkey])
        self.funding = []
        self.outputs = []
        self.blocks = {}
        self.snapshots = {}
        self.height = 0
        self.tip = None
        self.deadline = None

    def rpc(self, node, method, *params):
        return node.rpc(method, *params, deadline=self.deadline)

    def snapshot(self):
        snapshots = {}
        for node in self.nodes:
            info = self.rpc(node, "getblockchaininfo")
            utxos = {txid + ":" + str(vout): normalized_utxo(self.rpc(node, "gettxout", txid, vout, False))
                     for txid, vout in self.outputs}
            snapshots[node.label] = {"bestblockhash": info["bestblockhash"], "height": info["blocks"], "utxos": utxos}
        state = compare_snapshots(snapshots, self.error)
        if state["height"] != self.height or state["bestblockhash"] != self.tip:
            raise self.error("paired tip differs from submitted fixture chain")
        return state

    def block(self, transactions=(), invalid=False):
        coinbase = self.b.create_coinbase(self.height + 1, script_pubkey=self.output_script)
        if invalid:
            coinbase.vout[0].nValue += 1  # No fees: violates the coinbase reward limit.
        block = self.b.create_block(int(self.tip, 16), coinbase, GENESIS_TIME + (self.height + 1) * 600,
                                    txlist=transactions)
        if any(not tx.wit.is_null() for tx in transactions):
            self.b.add_witness_commitment(block)
        block.solve()
        return block

    def submit(self, block, record=None, valid=True):
        raw = block.serialize().hex()
        outcomes = {node.label: self.rpc(node, "submitblock", raw) for node in self.nodes}
        if record is not None:
            record["blocks"].append({"hash": block.hash_hex, "serialized_sha256": hashlib.sha256(bytes.fromhex(raw)).hexdigest(),
                                     "expected_valid": valid, "outcomes": outcomes})
        assert_block_outcomes(outcomes, valid, self.error)
        if valid:
            self.height += 1
            self.tip = block.hash_hex
            self.blocks[self.tip] = raw
            if record is not None:
                for tx in block.vtx[1:]:
                    self.outputs.extend((tx.txid_hex, vout) for vout in range(len(tx.vout)))
        state = self.snapshot()
        if valid:
            self.snapshots[self.tip] = deepcopy(state)
        if record is not None:
            record["states"].append(state)

    def setup(self):
        self.deadline = time.monotonic() + self.args.case_timeout
        genesis = {node.label: self.rpc(node, "getblockhash", 0) for node in self.nodes}
        if len(set(genesis.values())) != 1:
            raise self.error("regtest genesis identity diverged")
        self.tip = genesis["core"]
        chain_hasher = hashlib.sha256()
        for _ in range(FUNDING_HEIGHT):
            block = self.block()
            if self.height < 10:
                self.funding.append(block.vtx[0])
            chain_hasher.update(block.serialize())
            self.submit(block)
        self.report["funding_chain"] = {"height": self.height, "tip": self.tip,
                                        "serialized_sha256": chain_hasher.hexdigest()}

    def spend(self, coinbase_index, outputs, fee=10000):
        funding = self.funding[coinbase_index]
        tx = self.m.CTransaction()
        tx.version = 2
        tx.vin = [self.m.CTxIn(self.m.COutPoint(funding.txid_int, 0))]
        tx.vout = [self.m.CTxOut(value, script) for value, script in outputs]
        change = funding.vout[0].nValue - sum(value for value, _ in outputs) - fee
        tx.vout.insert(0, self.m.CTxOut(change, self.output_script))
        leaf = self.taproot.leaves["only-path"]
        witness = self.m.CTxInWitness()
        witness.scriptWitness.stack = [leaf.script, bytes([leaf.version | self.taproot.negflag]) + self.taproot.internal_pubkey]
        tx.wit.vtxinwit = [witness]
        return tx

    def admission(self, tx, expected, record, name, roots_reason=None):
        observations = {}
        for node in self.nodes:
            results = self.rpc(node, "testmempoolaccept", [tx.serialize().hex()])
            if not isinstance(results, list) or len(results) != 1 or type(results[0].get("allowed")) is not bool:
                raise self.error("malformed transaction admission result")
            result = results[0]
            observations[node.label] = {key: result[key] for key in ("allowed", "reject-reason", "vsize") if key in result}
        record["admission"].append({"name": name, "txid": tx.txid_hex, "serialized_sha256": hashlib.sha256(tx.serialize()).hexdigest(),
                                    "expected_allowed": expected, "observed": observations})
        if any(observations[label]["allowed"] != allowed for label, allowed in expected.items()):
            raise self.error("transaction admission differs from fixture expectation")
        if roots_reason and observations["roots"].get("reject-reason") != roots_reason:
            raise self.error("Roots rejection does not identify the tested policy")

    def ordinary(self, record):
        tx = self.spend(0, [])
        self.admission(tx, {"core": True, "roots": True}, record, "ordinary")
        self.submit(self.block([tx]), record)

    def datacarrier(self, record):
        # At 83 bytes a single carrier is standard; two individually small
        # carriers aggregate to 84 bytes and hit Roots' limit before its separate
        # multi-OP_RETURN rule. Core v30.3 permits this aggregate.
        at = self.spend(1, [(0, self.s.CScript([self.s.OP_RETURN, b"D" * 80]))])
        rejected = self.spend(1, [(0, self.s.CScript([self.s.OP_RETURN, b"D" * 40]))] * 2)
        record["boundary"] = {"limit_bytes": 83, "rejected_aggregate_bytes": 84,
                              "rejected_individual_bytes": [42, 42]}
        self.admission(at, {"core": True, "roots": True}, record, "at-83-bytes")
        self.admission(rejected, {"core": True, "roots": False}, record, "aggregate-84-bytes", "datacarrier")
        self.submit(self.block([rejected]), record)
        record["selected_block"] = self.tip

    def invalid_block(self, record):
        before = self.snapshot()
        self.submit(self.block(invalid=True), record, valid=False)
        if self.snapshot() != before:
            raise self.error("invalid block changed fixture chain state")

    def sigop(self, record):
        pubkey = bytes.fromhex("0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798")
        redeem = self.s.CScript([pubkey] + [self.s.OP_2DUP, self.s.OP_CHECKSIG, self.s.OP_DROP] * 14
                                + [self.s.OP_CHECKSIG, self.s.OP_NOT])
        p2sh = self.u.script_to_p2sh_script(redeem)
        funding = self.spend(2, [(10000, p2sh)] * 167)
        self.submit(self.block([funding]), record)
        tx = self.m.CTransaction()
        tx.version = 2
        tx.vin = [self.m.CTxIn(self.m.COutPoint(funding.txid_int, vout), self.s.CScript([b"", redeem]))
                  for vout in range(1, 168)]
        tx.vout = [self.m.CTxOut(1670000 - 100000, self.output_script)]
        at = deepcopy(tx)
        at.vin.pop()
        at.vout[0].nValue -= 10000
        record["boundary"] = {"limit_legacy_sigops": 2500, "at_legacy_sigops": 2490, "rejected_legacy_sigops": 2505}
        self.admission(at, {"core": True, "roots": True}, record, "2490-legacy-sigops")
        # Core 30.3 also has the 2500 legacy input-sigop standardness bound;
        # only Roots exposes its own diagnostic/configuration for that policy.
        self.admission(tx, {"core": False, "roots": False}, record, "2505-legacy-sigops",
                       "bad-txns-input-sigops-toomany-overall")
        self.submit(self.block([tx]), record)

    def stop_for_restart(self, node):
        node.stop()
        cleanup = deepcopy(node.cleanup_result)
        self.report["nodes"][node.label].setdefault("restart_shutdowns", []).append(cleanup)
        if (not isinstance(cleanup, dict) or cleanup.get("stopped") is not True
                or type(cleanup.get("returncode")) is not int or cleanup["returncode"] != 0):
            raise self.error(node.label + " shutdown before restart failed")

    def subdust(self, record):
        # Switch only standardness on both nodes; Roots' fee penalty stays on.
        # Earlier fixtures run under standard policy and are already recorded.
        for node in self.nodes:
            self.stop_for_restart(node)
            node.extra_flags = list(SUBDUST_ROOTS_FLAGS if node.label == "roots" else SUBDUST_FLAGS)
            node.start(deadline=self.deadline)
            self.report["nodes"][node.label].setdefault("launch_history", []).append(node.flags)
        record["configuration"] = {"name": "subdust-standardness-exception",
                                   "core_flags": list(SUBDUST_FLAGS), "roots_flags": list(SUBDUST_ROOTS_FLAGS),
                                   "roots_subdustfeepenalty": "explicitly enabled (same as default)"}
        self.snapshot()
        info = self.rpc(self.nodes[1], "getmempoolinfo")
        per_kvb = int(Decimal(str(info["minrelaytxfee"])) * self.m.COIN)
        probe = self.spend(3, [(0, self.output_script)])
        base_fee = (probe.get_vsize() * per_kvb + 999) // 1000
        threshold = 330 + base_fee
        below = self.spend(3, [(0, self.output_script)], fee=threshold - 1)
        at = self.spend(3, [(0, self.output_script)], fee=threshold)
        record["boundary"] = {"dust_threshold_sats": 330, "relay_fee_sats": base_fee,
                              "below_fee_sats": threshold - 1, "at_fee_sats": threshold}
        self.admission(below, {"core": True, "roots": False}, record, "below-penalty-threshold", "min relay fee not met")
        self.admission(at, {"core": True, "roots": True}, record, "at-penalty-threshold")
        self.submit(self.block([below]), record)
        record["selected_block"] = self.tip

    def mempools(self):
        return {node.label: sorted(self.rpc(node, "getrawmempool")) for node in self.nodes}

    def expected_state(self, saved):
        expected = deepcopy(saved)
        for txid, vout in self.outputs:
            expected["utxos"].setdefault(txid + ":" + str(vout), None)
        return expected

    def lifecycle(self, record):
        accepted = self.snapshot()
        record["transitions"] = []
        for name in ("datacarrier", "subdust"):
            case = next(case for case in self.report["cases"] if case["id"] == name)
            selected = case["selected_block"]
            header = self.rpc(self.nodes[0], "getblockheader", selected)
            parent = header["previousblockhash"]
            transition = {"fixture": name, "block": selected, "steps": []}
            record["transitions"].append(transition)
            for node in self.nodes:
                self.rpc(node, "invalidateblock", selected)
            self.tip, self.height = parent, header["height"] - 1
            disconnected = self.snapshot()
            if disconnected != self.expected_state(self.snapshots[parent]):
                raise self.error("disconnect differs from expected historical UTXOs")
            transition["steps"].append({"action": "invalidate", "state": disconnected, "mempools": self.mempools()})
            for node in self.nodes:
                self.rpc(node, "reconsiderblock", selected)
            self.tip, self.height = accepted["bestblockhash"], accepted["height"]
            reconnected = self.snapshot()
            if reconnected != accepted:
                raise self.error("reconnection changed accepted fixture state")
            transition["steps"].append({"action": "reconsider", "state": reconnected, "mempools": self.mempools()})

        control = self.spend(4, [], fee=10000)
        self.admission(control, {"core": True, "roots": True}, record, "persistence-control")
        record["control_profile"] = {"name": "compatible-p2tr-control-persistence",
                                     "fee_sats": 10000, "persistmempool": True,
                                     "core_flags": list(self.nodes[0].extra_flags),
                                     "roots_flags": list(self.nodes[1].extra_flags), "txid": control.txid_hex}
        for node in self.nodes:
            if self.rpc(node, "sendrawtransaction", control.serialize().hex()) != control.txid_hex:
                raise self.error("control transaction submission identity differs")
        before_mempools = self.mempools()
        if any(control.txid_hex not in pool for pool in before_mempools.values()):
            raise self.error("compatible control is missing before restart")
        for node in self.nodes:
            original_datadir = node.datadir
            self.stop_for_restart(node)
            node.start(deadline=self.deadline)
            if node.datadir != original_datadir:
                raise self.error("restart did not reuse owned datadir")
            self.report["nodes"][node.label]["launch_history"].append(list(node.flags))
            while not self.rpc(node, "getmempoolinfo").get("loaded", False):
                if time.monotonic() >= self.deadline:
                    raise self.error("mempool reload timed out")
                time.sleep(0.01)
        persisted = self.snapshot()
        if persisted != accepted:
            raise self.error("restart changed accepted fixture state")
        after_mempools = self.mempools()
        if any(control.txid_hex not in pool for pool in after_mempools.values()):
            raise self.error("compatible control did not persist across restart")
        record["restart"] = {"same_owned_datadirs": True, "state": persisted,
                             "mempools_before": before_mempools, "mempools_after": after_mempools,
                             "control_persisted": True}

    def run(self):
        self.setup()
        operations = {"ordinary": self.ordinary, "datacarrier": self.datacarrier,
                      "invalid-block": self.invalid_block, "sigop": self.sigop, "subdust": self.subdust,
                      "lifecycle": self.lifecycle}
        for record in self.report["cases"]:
            operation = operations.get(record["id"])
            if operation is None:
                raise self.error("required fixture implementation is missing")
            start = time.monotonic()
            self.deadline = start + self.args.case_timeout
            record.update({"status": "failed", "admission": [], "blocks": [], "states": []})
            try:
                operation(record)
                record["status"] = "passed"
            except Exception as error:
                record["error"] = str(error) if isinstance(error, self.error) else type(error).__name__
                raise
            finally:
                record["duration_seconds"] = round(time.monotonic() - start, 6)
