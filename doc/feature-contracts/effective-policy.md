# Effective mempool policy (`effective-policy`)

**v31 candidate status: deferred.** This preserved contract describes the
accepted 30.3 source, including its documented gaps; the foundation does not
implement its Roots controls or RPC fields. Source code/test paths below refer
to `10d772eb7f4b00b5193428a0dde4c06558d6c297`, not current candidate behavior.
[The port review](../roots-v31-port-review.md) records ownership and current gates.

## Scope and compatibility

The additive `getmempoolinfo.policy` object reports initialized local transaction
admission controls. It does not describe consensus validity, peer permissions,
wallet preferences, per-call bypasses, or mining-template limits. A transaction
rejected by local policy can still appear in a valid block. This RPC neither
changes policy nor adds a runtime configuration interface.

Existing top-level fields retain their names, types and meanings. In particular,
`minrelaytxfee`, `incrementalrelayfee` and `mempoolminfee` already report effective
fee rates in BTC/kvB (1,000 virtual bytes); the ordinary defaults are 0.00001000,
0.00000100 and 0.00001000 respectively. The initialized incremental rate is
100 sat/kvB even though inherited startup help says 1,000. `mempoolminfee` can
rise with mempool pressure. `maxmempool` already reports the initialized memory
limit in bytes. These fields are not duplicated inside `policy`.

All fields below are always present, with no omitted or null values. Numbers
other than `dust_relay_fee` are JSON integers. Disabled carrier admission is
distinguished from an enabled zero-byte budget by `datacarrier`. False Boolean
values disable or deny the named behavior, subject to the gates below. Consumers
must allow additional fields; changing existing types, units or enum meanings
requires a separately reviewed compatibility decision. Ordinary defaults below
assume no policy overrides; network and internal parameter interaction may
select different initialized values.

## Schema

| Field | Type / units | Default | Meaning and enforcement gate |
| --- | --- | --- | --- |
| `require_standard` | Boolean | true | Enables transaction/input/witness standardness checks; false does not disable the other admission checks below. |
| `max_script_size` | Integer / bytes | 1650 | Script policy bound under standardness; scriptSig is additionally capped at 1650 bytes. Zero is a zero-byte bound, not disabled. |
| `bytes_per_sigop` | Integer / virtual bytes per sigop | 20 | Policy virtual-size accounting parameter, including fee/size checks; zero removes this size contribution. |
| `bytes_per_sigop_strict` | Integer / equivalent bytes per sigop | 20 | Accurate sigop density bound independent of standardness; zero disables this additional density check. Fixed sigop limits still apply. |
| `max_tx_legacy_sigops` | Integer / sigops | 2500 | Legacy transaction sigop bound under standardness; zero is a zero-sigop bound. |
| `datacarrier` | Boolean | true | Whether an OP_RETURN carrier budget exists; false denies carriers under standardness/full-count checking. |
| `max_datacarrier_bytes` | Integer / bytes | 83 | Aggregate serialized carrier budget; zero when datacarrier is disabled, or when an enabled budget is explicitly zero. |
| `datacarrier_fullcount` | Boolean | true | Count recognized input/witness carriers too; the aggregate check applies independently of standardness when true. |
| `datacarrier_weight_per_byte` | Integer / weight units per detected byte | 4 | Initialized rounded-up carrier cost (four weight units per virtual byte). Extra policy weight is added only where this exceeds the byte's existing weight, never reducing consensus weight. Zero adds no extra weight. |
| `accept_nonstd_datacarrier` | Boolean | false | Permit recognized nonstandard carriers independently of standardness; does not disable the full-count aggregate budget. |
| `permit_bare_datacarrier` | Boolean | false | Permit carrier-only transactions under standardness. |
| `dust_relay_fee` | JSON number / BTC per kvB | 0.00003000 | Live rate used to calculate output dust thresholds and the sub-dust penalty; zero makes those thresholds zero. |
| `subdust_fee_penalty` | Boolean | true | Subtract accumulated output dust shortfalls from policy modified fees, independently of standardness. |
| `permit_bare_pubkey` | Boolean | false | Permit bare pubkey outputs under standardness. |
| `permit_bare_multisig` | Boolean | false | Permit bare multisig outputs under standardness; equals the existing top-level field. |
| `reject_parasites` | Boolean | true | Reject the CAT21 locktime marker under standardness. |
| `reject_tokens` | Boolean | false | Enable recognized token filters under standardness. |
| `accept_unknown_witness` | Boolean | true | Permit unknown witness output versions under standardness. |
| `permit_bare_anchor` | Boolean | true | Permit transactions with only anchor outputs under standardness. |
| `ephemeral_anchor` | Boolean | true | Permit anchor outputs under standardness. |
| `ephemeral_dust` | Boolean | false | Permit nonzero ephemeral dust under standardness, subject to other dust/package rules. |
| `ephemeral_send` | Boolean | false | Permit non-anchor dust outputs under standardness, subject to other dust/package rules. |
| `replacement` | String enum | `always` | `never`, `optin`, or `always`; resolved replacement mode, not a guarantee of replacement. Opt-in also permits non-signaling TRUC/v3 replacements. |
| `truc` | String enum | `enforce` | `reject`, `accept`, or `enforce`; resolved local v3 admission/topology policy. |
| `min_relay_coinblocks` | Integer / satoshi-blocks | 0 | Minimum sum of input value times confirmations; zero disables this age restriction. |
| `min_relay_maturity` | Integer / blocks | 0 | Minimum input confirmation age restriction; zero disables it. |
| `spk_reuse` | String enum | `allow` | `allow` or `conflict`; initialized script reuse mode for mempool conflicts, independent of standardness. |

Allow flags are permissions within the active policy, not unconditional admission
guarantees. In particular `require_standard=false` bypasses the checks explicitly
gated by standardness; their initialized parameters remain visible so operators
can distinguish the gate from its subordinate settings. Full-count carrier
checks, nonstandard-carrier rejection, fee penalties, strict sigop density,
replacement/TRUC and maturity restrictions have independent gates. Existing
package, dust, fixed limits and consensus checks remain applicable.

## Provenance, precedence and consistency

Read `CTxMemPool::m_opts` for initialized mempool controls, `GetDustRelayFee()`
for the live dust rate, the initialized policy globals for script/size parameters,
and `SpkReuseMode` for reuse policy. Do not reread argument strings, reconstruct
defaults, or introduce another configuration store. Constructor normalization
matters: the dust floor is initialized from the selected dust fee. RBF/TRUC and
ephemeral token options resolve to modes/flags, not the original spellings;
carrier cost is rounded upward to integral weight units. If incremental relay
fee is increased and minrelaytxfee was not explicit, the minimum relay fee is
raised to match it.

Startup selection follows `runtime-config` and `policy-parsing`: forced settings,
CLI, read/write config, JSON settings, network config, default config, built-in
defaults; scalar CLI chooses its last value and config its first after negation
processing. Only initialized results are published. Invalid effective options
prevent startup and therefore produce no RPC response.

Build the new object inside the existing `MempoolInfoToJSON` mempool critical
section. The mempool fields and live rates share that observation with existing
top-level fields; no cross-RPC or transaction-admission snapshot is promised.
The globals and option controls above are startup-only in this implementation
and initialized before RPC warmup finishes. The live dust rate and top-level
rolling minimum are read with existing locking; there is no policy setter RPC.

Non-off `dustdynamic` currently fails startup because production scheduler wiring
is missing. Reporting a live dust rate does not imply that dynamic modes operate
in this revision. Dormant `getmempoolstats` is outside this contract. Mining
options are reconstructed and clamped by individual block assemblers, with no
single retained active instance to report here; this mempool schema deliberately
does not claim to represent their limits.

## Acceptance and dependencies

Depends on `policy-parsing`, `runtime-config`, `policy-standardness`,
`policy-carrier`, `policy-dust`, `policy-subdust`, `policy-replacement`,
`policy-coinage` and `policy-reuse`.
`rpc_effective_policy.py` specifies defaults, initialized overrides, disabled
controls, resolved aliases, precedence, invalid startup and reporting/admission
parity, including standardness-independent full carrier counting and a valid
block containing a locally rejected carrier transaction. The existing
`feature_policy_args.py`, `mempool_datacarrier.py` and
`mempool_subdust_fee_penalty.py` establish parsing and representative enforcement
before the new RPC is implemented. Keep their valid-block acceptance coverage.

Retire a version-specific reporting implementation only when a reviewed upstream
interface provides equivalent initialized values, units, gates, consistency and
acceptance evidence. Raw configuration echoing is not equivalent.
