# Sub-dust effective-fee contract

**v31 candidate status: deferred.** This preserved contract describes the
accepted 30.3 source, including its documented gaps; the foundation does not
implement its Roots controls or RPC fields. Source code/test paths below refer
to `10d772eb7f4b00b5193428a0dde4c06558d6c297`, not current candidate behavior.
[The port review](../roots-v31-port-review.md) records ownership and current gates.

Stable ID: **policy-subdust**. Depends on **policy-dust** and the shared local
policy/configuration invariants in the [catalog](/doc/roots-features.md).
This worked section is split out for readability. It describes pinned Roots
`17e1d484aba2a881be03b8928fba3c0125e027a2`; implementation links are aids for
that version, not a requirement to keep a particular acceptance function or
check location.

## Observable behavior and units

When enabled, subtract the sum of output dust-threshold deficits from a
transaction's modified policy fee. Actual input/output values, transaction ID
and actual fee do not change. Paying enough to cover the deficit may satisfy
fee checks; it never overrides another standardness, script, replacement,
package or consensus check.

For each output `i`, let `A_i` be its value in integer satoshis, `R` the current
effective dust relay rate in sat/kvB, and `S_i` its serialized output size in
bytes, including value and script-length encoding:

```
T_i = 0                                      if scriptPubKey is unspendable
T_i = ceil(R * (S_i + 67) / 1000)             for a witness program
T_i = ceil(R * (S_i + 148) / 1000)            otherwise
D_i = max(0, T_i - A_i)
P   = saturating_sum(D_i)
F   = saturating_add(F_before_penalty, -P)
```

Here `F_before_penalty` includes any `prioritisetransaction` fee delta; `F` is
the effective modified fee in satoshis. The 67-byte estimate uses integer
`107 / 4 = 26` in the discounted spend estimate; it is not 67.75 rounded later.
The witness estimate also applies to Taproot and future witness programs,
without assuming a smaller Taproot satisfaction. The ceiling is per output
threshold, before subtracting its value or accumulating deficits. At rate zero
all thresholds and the penalty are zero. For supported nonnegative rates there
is no floating-point arithmetic in this calculation.

For an ordinary individually accepted non-TRUC transaction with policy virtual
size `V`, the relay fee gate requires `F >= ceil(minrelay_sat_per_kvB * V / 1000)`.
`V` is the node's policy size, including sigop/data-cost adjustments, not
necessarily the size displayed by a generic raw decoder. Rolling mempool fees
can require more. Entry modified fees and ancestor/descendant accounting carry
the penalty into package/replacement/mining fee selection; this is not a
separate coin transfer or a charge added to a mined transaction's actual fee.

Version-specific sources: dust threshold (`src/policy/policy.cpp`, source reference)
`GetDustThreshold`, fee rounding (`src/policy/feerate.cpp`, source reference) and
integer evaluation (`src/util/feefrac.h`, source reference), penalty application (`src/validation.cpp`, source reference),
entry modified fees (`src/kernel/mempool_entry.h`, source reference),
saturation (`src/util/overflow.h`, source reference) and policy size (`src/txmempool.h`, source reference).

## Worked boundaries

At the static default `R = 3000` sat/kvB:

| Output | Serialized size + spend estimate | Threshold | Value | Deficit |
| --- | --- | --- | --- | --- |
| P2PKH | 34 + 148 bytes | 546 sat | 545 sat | 1 sat |
| P2WPKH | 31 + 67 bytes | 294 sat | 294 sat | 0 sat |
| P2TR | 43 + 67 bytes | 330 sat | 0 sat | 330 sat |
| P2TR | 43 + 67 bytes | 330 sat | 100 sat | 230 sat |
| Two P2TR outputs | 110 bytes each | 330 sat each | 0 sat each | 660 sat total |
| OP_RETURN | Unspendable | 0 sat | 0 sat | 0 sat |

If the measured policy size is 200 vB and minimum relay rate is 1,000 sat/kvB,
one 100-sat P2TR output requires an actual fee of at least `200 + 230 = 430`
sat when no fee delta or other gate applies. A 429-sat fee fails this gate;
430 sat reaches equality. This example assumes dust standardness is relaxed
on regtest, as in the owned test; the default standardness check may reject the
output first. The 200-vB size illustrates the formula, not the size of every
fixture. At `R = 1` sat/kvB a P2TR threshold is `ceil(110/1000) = 1` sat.

An output exactly at/above its threshold contributes nothing. Unspendable
outputs contribute nothing. A **zero-valued spendable** output contributes its
entire threshold; neither zero amount nor P2A classification is an automatic
penalty exemption. Anchor/ephemeral permission and penalty arithmetic are
separate checks in this source.

## Bounds, defaults and interactions

- `subdustfeepenalty` is a boolean, defaults to true, and is disabled with
  `-subdustfeepenalty=0` (or its equivalent boolean negation). It is not a fee
  amount or multiplier. CLI values override config; ordinary repeated CLI
  scalar values take the last value after negation processing, while config
  takes the first. Full precedence is in **runtime-config** in the catalog.
  The initialized mempool option is startup state, not a mutable RPC setting.
  See option loading (`src/node/mempool_args.cpp`, source reference) and
  boolean conversion (`src/common/args.cpp`, source reference). Accepted Boolean grammar and
  malformed-input diagnostics are specified by [policy-parsing](policy-parsing.md).
- Disabling removes this deficit subtraction only. It does not disable dust
  standardness or lower relay/mempool minimum fees. `acceptnonstdtxn=1` is
  test-chain-only and does not itself disable the penalty. `dustrelayfee=0`
  produces zero thresholds only if dynamic dust does not raise the effective
  rate. Dynamic dust changes `R` and therefore future admission penalties;
  current source does not reprice every existing entry when that rate changes.
- Deficits accumulate using signed 64-bit `CAmount` saturation: `P` stops at
  `INT64_MAX`. Subtracting that nonnegative penalty uses saturation down to
  `INT64_MIN` for modified-fee accounting; it must not wrap into a high fee.
  Valid output amounts are nonnegative and checked before this calculation.
  Saturating accumulation does **not** make arbitrarily large configured fee
  rates safe: threshold evaluation requires its result to fit in signed
  64-bit, and dynamic rate multiplication has its own arithmetic limits.
  Extreme configuration/threshold tests are an open gap.
- Standard dust/ephemeral checks can reject before or after penalty calculation.
  The normal zero-fee ephemeral requirement remains; permission does not imply
  automatic subtraction bypass. Enforced TRUC can bypass the individual relay
  floor for a package-funded parent, but package/mempool/replacement gates still
  use their own rules. Internal reorg rejection exceptions are not a public
  option to bypass arbitrary validation. Fee-delta, CPFP, RBF/TRUC and priority
  mining interactions require their own acceptance coverage.
- **Consensus invariant:** a consensus-valid block containing a transaction
  rejected locally for this effective-fee deficit must still be accepted.
  The penalty must not alter block fee/reward calculation or consensus script
  flags. This is a required acceptance case even when local admission fails.

## Acceptance evidence and gaps

mempool_subdust_fee_penalty.py (`test/functional/mempool_subdust_fee_penalty.py`, source reference)
uses regtest with `acceptnonstdtxn=1` to isolate the fee gate. Its exact cases are
`test_dust_output_increases_required_fee` (zero P2TR output, one below/equal fee),
`test_partial_dust_proportional_penalty` (100-sat output),
`test_multiple_dust_outputs_stack` (two zero outputs), `test_penalty_disabled`,
`test_op_return_not_penalized` and `test_above_dust_not_penalized`.
mempool_dust.py (`test/functional/mempool_dust.py`, source reference) `test_dust_output` separately
checks different script thresholds and exact threshold/one below;
mempool_tests.cpp (`src/test/mempool_tests.cpp`, source reference) `DynamicDustFeePublication`
checks changing published dust state.

The same sub-dust test also owns these interaction acceptance cases:

- `test_threshold_and_low_rate_boundaries`: exact threshold/one below, per-output
  ceiling at one sat/kvB, and zero dust rate with the penalty still enabled.
- `test_anchor_and_standardness`: zero-valued P2A contributes 240 sat at the
  default rate; paying more cannot bypass standard dust/ephemeral checks, and
  disabling the penalty retains ordinary dust rejection.
- `test_carrier_cost_interaction`: carrier cost `1.01` rounds upward to five
  weight units/byte; exact fee/one below, base versus modified fee and policy
  size, zero cost, and full-count budget enforcement enabled/disabled.
- `test_fee_delta_and_replacement`: pre-admission and resident fee deltas,
  an RBF rejection despite a higher actual fee, replacement with sufficient
  modified fees, and unchanged actual fees.
- `test_package_and_truc`: ordinary v2 retains the individual relay floor;
  enforced v3 sponsorship pays the parent's negative modified fee, including
  the exact package minimum/one below and ancestor/descendant accounting.
  `mempooltruc=accept` retains the individual floor. A consensus-valid block
  containing the locally rejected v2 parent and its child is accepted.
- `test_mining_and_block_acceptance`: explicit valid block containing a locally
  rejected sub-dust transaction; template exclusion/inclusion across its fee
  floor via a one-sat delta; actual transaction fees and coinbase reward remain
  unchanged. Penalty disabling and an enabled priority area have separate
  template inclusion cases.

Repeated/config precedence is covered by `feature_policy_args.py` and
`rpc_effective_policy.py`, as specified in [policy-parsing](policy-parsing.md).
These cases isolate fees on regtest using nonstandard admission where needed;
they do not establish permission to bypass standard ephemeral spending rules.

The `subdust_fee_penalty` target in
tx_pool.cpp (`src/test/fuzz/tx_pool.cpp`, source reference) drives production admission with the
penalty enabled and disabled. An independent wide-integer oracle checks output
thresholds and expected acceptance, and admitted entry base/modified/ancestor/
descendant fees. Inputs include arbitrary nonnegative signed-64-bit dust rates,
up to sixteen small P2TR/P2WPKH/P2A/P2PKH/OP_RETURN outputs plus change, and
positive/negative fee deltas bounded by `MAX_MONEY`. Seeded cases exercise
accumulated deficits exceeding `INT64_MAX` and subtraction reaching `INT64_MIN`:
these must reject without wrapping into a high fee. Output sizes are deliberately
bounded so each individual threshold fits in signed 64-bit; the target does not
promise arbitrary configured rate/script combinations are safe. See the
[scoped fuzz procedure](policy-parsing.md#scoped-fuzz-evidence).

**Unresolved preservation gates:** extreme configured thresholds beyond the
target's representable size/rate domain, dynamic dust repricing/startup, interactions with
rolling mempool pressure, package replacement/sibling eviction with dust,
reorg accounting, and the complete ephemeral exception/fee-delta matrix.
The bounded arithmetic campaign does not establish full admission safety at
extreme rates or prove saturated ancestor accounting for arbitrary packages.
Dynamic modes retain the known scheduler startup failure recorded in
`policy-parsing`. Existing dust/TRUC/package/reorg suites cover their ordinary
cases, but cannot be presented as proof of these combined cases. A port must
retain these gates until focused coverage or an explicit compatibility decision
resolves them.

Retirement requires upstream equivalence of this formula, rounding, effective
fee accounting, defaults/disable behavior and configuration interactions,
with the owned tests and valid-block invariant passing. An upstream dust
standardness check alone is not this fee penalty. Apply the catalog's shared
retirement rule and retain the unresolved-gap record until tested.
