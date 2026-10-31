# Roots graph fees and policy weight

Stable IDs: `policy-subdust`, `policy-carrier`, `policy-replacement`,
`policy-reuse`, `policy-dust`. Adapted from Roots `846c9de12a5e71259dc1ed829413d68f449ebfd5`
and the complete released `e93a8cca047c12ce9cab97181dd30705ed0e7e83` source,
with Jaromil and devjoinedthechat attribution and upstream Core/Knots lineage
retained. Native v32 graph and cluster algorithms remain authoritative.

The current graph work activates carrier extra weight, immutable admission dust
deficits, modified fees, configured replacement/TRUC/reuse and dynamic-dust
publication. Mining priority/serialized
limits belong to L5, and integrated read/write configuration and effective-policy
RPC reporting belong to L9. These are preservation obligations, not active
features established by argument parsing.

## Deficits, units and independent fees

For each output with value `A` satoshis and serialized size `S` bytes, the dust
threshold at the effective dust relay rate `R` sat/kvB is:

```
T = 0                             for an unspendable output
T = ceil(R * (S + 67) / 1000)      for a witness program
T = ceil(R * (S + 148) / 1000)     otherwise
D = max(0, T - A)
P = saturating_sum(D)
F = saturating_sub(saturating_add(actual_fee, operator_delta), P)
```

Thresholds and subsequent signed arithmetic saturate at their signed 64-bit
bounds. The ceiling applies separately to each output. The witness estimate
uses the source integer discount and also applies to Taproot/future witness
programs. A zero-valued spendable output contributes its entire threshold;
anchors are not exempt. Disabling `subdustfeepenalty` sets `P` to zero without
changing ordinary dust/ephemeral checks. The default is enabled.

At the fixed default 3000 sat/kvB:

| Output | Threshold | Value | Deficit |
| --- | --- | --- | --- |
| P2PKH | 546 | 545 | 1 |
| P2WPKH | 294 | 294 | 0 |
| P2TR | 330 | 0 | 330 |
| P2TR | 330 | 100 | 230 |
| Two zero-valued P2TR outputs | 330 each | 0 each | 660 total |
| OP_RETURN | 0 | 0 | 0 |

For a 200-vbyte policy size at 1000 sat/kvB minimum relay and one 100-sat P2TR
output, the fee gate requires 430 actual satoshis with no operator delta:
429 fails and 430 reaches equality. This assumes other standardness rules
permit the transaction. Paying a deficit never bypasses another local,
package, replacement, script or consensus check.

Carrier cost rounds upward to quarters of a vbyte. Entries retain actual weight
separately from carrier extra weight and apply the native sigop adjustment to
their policy weight. Admission, staged additions, chunk/replacement diagrams,
fee updates, trimming and ancestor/descendant aggregation use the same modified
fee and policy weight. Actual fees, transaction identifiers, BIP141 weight and
block rewards retain their ordinary meaning. Deficits are captured at admission.

Resident operator changes recompute the formula from the aggregate stored delta;
they do not repeatedly add to a previously saturated modified fee. This repairs
a released-source discrepancy at signed extremes and preserves the same result
after removal/re-admission. Rejected changes leave the entry, graph, operator
delta and reservations unchanged. The wallet's structural chain-limit query
also rejects an infeasible fee/weight reservation before staging it.

## Dynamic dust lifecycle

The default is `off`. Enabled startup settings initialize and schedule a
node-owned update only after the final successful chainstate load and native
fee-estimator construction. Retries cannot retain a callback to an abandoned
mempool. Shutdown stops and joins the scheduler before destroying its owners.
The initial update precedes asynchronous mempool reload, then runs every
15 minutes. Missing estimates fall back to the configured `dustrelayfee` floor.

Target mode requests the native block-policy LONG estimate at 80% success.
Mempool mode walks native score/topology order to the selected decimal-kvB
policy size and uses the boundary transaction's actual fee and policy size.
Operator deltas and dust deficits affect ordering, but do not replace that
actual fee. Multiplication uses thousandth precision, floors fractional
satoshis per kvB and saturates at `INT64_MAX` before applying the configured
floor. Zero or unavailable estimates cannot erase the floor.

Publication and complete standardness snapshots share the mempool lock.
Admission sees one rate for standardness, ephemeral checks and its immutable
deficit; subsequent publication never reprices resident entries. Node/wallet
dust interfaces and prioritisation checks read the same synchronized rate.
Mempool persistence stores only the operator delta, never the already-subtracted
dust deficit. Reload and reorg admission compute a fresh deficit once at the
current rate; confirmed transactions retain native clearing of operator deltas.
Restart setting changes initialize a fresh rate. The full effective-policy RPC
object and runtime configuration remain L9-owned.

## Native storage bounds

The graph reserves the sum of absolute modified fees up to `INT64_MAX`, and
adjusted weights up to `INT32_MAX`, across resident and staged additions.
Pending removals remain reserved until commit; abort releases staged reserves.
Admission outside those budgets fails with `mempool-fee-overflow` or
`too-large-cluster`. A resident fee change outside the budget fails atomically
and the RPC returns an error. No invalid fee/weight pair enters native graph
arithmetic. Extreme fee-rate evaluation saturates before narrowing.

These global conservative bounds also reject some representable final states:
cancelling positive/negative fees, independent clusters with excessive combined
absolute fees, or replacements that fit only after removals. Per-cluster limits
alone cannot bound multi-cluster replacement diagrams. This preserves the
released adapter rather than widening native storage or adding ancestor indexes.

## Modes and RPC compatibility

The default replacement mode is Always/full RBF (`fee,-optin`). Never rejects
input conflicts; OptIn accepts signaling conflicts and non-signaling v3/TRUC
conflicts. Configured TRUC Reject/Accept/Enforce governs local v3 rejection,
inheritance/resource checks and sponsorship. Ordinary non-TRUC parents retain
their individual minimum relay floor; enforced TRUC packages can sponsor a
parent, subject to native modified-package fee and replacement rules.

`spkreuse=allow` is the default. Conflict tracking uses full scripts and sets
of all creator/spender claims, covering duplicate outputs, input/output reuse,
ancestor conflicts, staged package claims, removal, replacement and reorgs.
It does not infer script ownership or privacy. With independent inputs, these
script conflicts can replace under Never or OptIn even if the original is
non-signaling. They must still cover the original modified fees, pay the exact
incremental relay requirement and improve the native feerate diagram. Direct
prevout conflicts remain subject to the configured input-conflict RBF gate.
This is the released runtime behavior; the older catalog's broad statement that
Never disables all replacement did not describe the script-only path.

The native `vsize_bip141` and `weight` fields report actual transaction metrics;
`vsize_adjusted`, legacy `vsize`, cluster/chunk weight and ancestor sizes report
policy adjustments. The deprecated `fullrbf` field remains available only with
`-deprecatedrpc=fullrbf` and reports the initialized input-conflict Always mode;
it does not describe independent script-only replacement. Deprecated
`bip125-replaceable` retains its native signaling meaning, independently of
configured acceptance. The full effective-policy object remains L9-owned.

## Verification boundaries

Native `GraphPolicyAccounting`, `GraphPolicyMultiAncestorAccounting`,
`GraphPolicyBudgetsAndReuseClaims`, `GraphPolicyTrimUsesModifiedFees` and
`GraphPolicyFeeDeltaSaturation` cover staged/committed/aborted state, shared
ancestors, fee changes, block removal/re-admission and independent arithmetic.
The owned functional scripts are `mempool_graph_policy.py`,
`mempool_rbf_policy.py` and `mempool_subdust_fee_penalty.py`, alongside the
native package/RBF/TRUC/cluster profiles. Subdust's optional
`--test-mining-priority` assertions remain an explicit L5 gate. Arithmetic fuzz
targets are registered separately from their actual execution. Lifecycle tests
cover restart/operator persistence, floor changes, scheduler updates, missing
and warmed estimator results, reindex retry and reorg re-admission. Independent
carrier fixtures compose legacy, P2WSH and Taproot input data with OP_RETURN,
check exact adjusted fee boundaries, actual BIP141 metrics and same-tx blocks.

Local policy never changes block consensus: consensus-valid blocks containing
locally rejected carrier, dust, TRUC or reuse transactions remain acceptable.
Ordinary dust checks, parser success and native full-RBF defaults alone do not
establish equivalence to these complete contracts.
