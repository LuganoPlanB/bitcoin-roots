# Mempool policy and compact reconstruction review

This inventory compares Roots `bfbba3d600fea7112b95ce7bc742ee144b25e6ad`
with the exact Bitcoin Core v31.1 base
`9be056a8a72b624dae9623b2f7bded92c2a21c91`. The inventory tables record source,
existing assertions and gaps at that starting point. The characterization
sections below describe the subsequently added coverage. The controlled
experiment section records bounded synthetic measurements and remaining questions;
it makes no production latency or mainnet-performance claim.
The final recommendations below retain defaults and optional controls, and
simplify the cache's operator-facing accounting description. No product
correctness defect was reproduced. An operator's filtering goal is distinct
from a resource-saving hypothesis; unmeasured costs remain unresolved.

Native arithmetic references below include `src/test/amount_tests.cpp` for
fee-rate saturation/scaling; `src/test/transaction_tests.cpp` for standardness
and reason isolation; `src/test/mempool_tests.cpp` for graph/publication/trim;
and `src/test/miner_tests.cpp` for shared template accounting. These precise
test sources supplement the functional entry points in the tables.

## Admission, retention and block validity

`MemPoolAccept::PreChecks` in [validation.cpp](../src/validation.cpp) applies
Roots local policy. Successfully admitted transactions enter the native mempool
and ordinary relay path. A transaction rejected here may still satisfy consensus.
Compact reconstruction's extra vector holds shared transaction references; it
neither admits them to the mempool nor authorizes ordinary transaction relay.
`PartiallyDownloadedBlock::InitData` matches wtxid-derived short IDs against the
mempool and extras, then normal block validation checks the reconstructed block.
Missing transactions use `getblocktxn`/`blocktxn` or full-block fallback. The same
consensus-valid block must be accepted with the cache enabled, disabled or evicted.

Core already retains rejected and replaced transactions. The current insertion
sites in [net_processing.cpp](../src/net_processing.cpp), lines 3163–3166 and
3190–3192, are inherited from the exact base: `ProcessInvalidTx` consumes
`MempoolRejectedTx`'s eligibility result, with the existing `<100000` dynamic-byte
test, and `ProcessValidTx` inserts each replaced transaction. Roots' common
insertion function adds the size test for replacements too. No cache insertion
site was added for Roots policy specifically.

[TxDownloadManagerImpl::MempoolRejectedTx](../src/node/txdownloadman_impl.cpp)
(350–499) is byte-identical to Core. Eligibility starts as `first_time_failure`;
`TX_WITNESS_STRIPPED` disables it. For new missing-input transactions with eligible
parents it suppresses an extra insertion when `TxOrphanage::HaveTx(wtxid)` is true
(399–401). Parent hashes are deduplicated separately. Reconsiderable failures
can trigger 1-parent/1-child package processing; reject filters suppress downloads
and repeated validation. These filters, orphan storage and the extra vector are
different structures. An orphan can share its allocation with an extra entry;
resolving/removing it does not erase the extra vector's reference. Eligibility
does not mean every failure is cached, and insertion does not guarantee survival.

Ordinary relay follows successful admission in `ProcessValidTx`. The inherited
`ProcessMessage` TX branch's `ForceRelay` handling can reannounce an already
resident transaction, but explicitly declines a non-mempool transaction; the
blocksonly permission exception is separate. Distinguish these from
unprivileged-peer behavior. RPC submission alone is not evidence of the P2P
rejected-transaction insertion path. Consensus code does not read Roots template
or local policy limits: `validation.cpp` from `bool CheckBlock(` to EOF and
`src/consensus/` match the exact base. This source boundary supports, but does not
replace, the existing local-rejection/valid-block tests.

## Provenance keys

Each row names the introducing v31 commit, including the final port repair
where relevant. These are commits in this repository, not imported upstream
specifications:

- **P**: `e5445b7c96193c7ec141705aac5ba96651e0ee30`, scoped parsing and local
  checks. Knots-derived selected Roots policy is carried through the historical
  `1b73fe3d21`/`ec9db369f5` source overlays; the historical source/author ledger
  is in [the port review](roots-v31-port-review.md#complete-source-to-theme-accounting).
- **G**: `846c9de12a5e71259dc1ed829413d68f449ebfd5`, native graph integration,
  replacement/reuse, synchronized dynamic dust, overflow/lifetime and reload
  repairs. Source RBF corrections include `aa27c9117a`, `48f3b3bbe8`,
  `a6f35e915c`; publication lineage includes `784b2af17a`, `f74cc0433b`,
  `73c97862e2`; subdust/interactions include `fc0dc945d0`, `10d772eb7f`.
- **M**: `0fbff64239358c69a266705b189770b0f5570ff3`, priority/residual mining
  adaptation, including source ancestor-accounting corrections.
- **N**: `2cd5097c909d1ab2f8bf27eb0df97b24b6c0807e`, cache budgets and
  fee-filter controls. The replacement-entry exclusion, allocation bound and
  pre-add eviction are v31 port repairs, not inherited Core cache behavior.
- **R**: `44c15f2cde7d36f2733814d6ab68ce148f1a7079`, initialized policy reporting,
  immutable per-pool options and read/write configuration integration.

Knots lineage does not establish a current implementation's benefit or permit
retiring it. The exact-base delta and current callers establish the rows below.
The source ledger retains Jaromil/devjoinedthechat and Core/Knots attribution.

## Modification inventory

Paths below are relative to `src/` unless prefixed `test/`. **Measure** means
retain the operator contract and quantify its cost; **retain** means preserve an
inherited mechanism or compatibility repair. None means performance has passed.
Tests are existing coverage to inspect/run, not a claim that every mode is tested.
The status column records the inventory-stage assessment; the final per-row
recommendations and remaining cost questions appear at the end of this review.

| ID / current source and provenance | Core equivalent; Roots default and optional modes | Purpose and admission / eviction / relay / retention effects | Cost hypothesis; existing evidence and remaining gap | Preliminary status |
| --- | --- | --- | --- | --- |
| A1 `node/mempool_args.cpp`: `ValidateRootsPolicyOptions`, `ParseDataCarrierCost`, `ParseDustDynamicOpt`, `ApplyPermitEphemeralOption`; P,G | Core getters remain outside this strict grammar. Roots checks scalar types, Boolean/integer bounds and every token, fixed-point carrier cost and thousandths dust multiplier; CLI/config/JSON/confrw precedence remains observable. | Reject invalid startup settings before policy initialization; preserve diagnostics, token precedence and units. No transaction retention effect. | Startup parsing/maintenance cost; `policy_args_tests.cpp`, `feature_policy_args.py`, `rpc_effective_policy.py`. Complete combinations are not implied by parser tests. | Retain grammar; measure only if material. |
| A2 `policy/policy.h`: `DEFAULT_MIN_RELAY_TX_FEE`; P | Core 100 sat/kvB; Roots 1000 sat/kvB, overridable by `minrelaytxfee`. Incremental replacement fee remains Core's 100 sat/kvB. | Higher individual admission/relay floor; not a block-validity or reconstruction floor. Ordinary non-TRUC parents retain this floor in packages (G). | Changes admitted population and fee-filter inputs; `mempool_subdust_fee_penalty.py`, package/TRUC cases, `rpc_effective_policy.py`. Isolate rate and sponsorship when comparing populations. | Measure. |
| A3 `policy/settings.*`, `policy.cpp`: `g_script_size_policy_limit`, `IsStandardTx`, `AreInputsStandard`, `IsWitnessStandard`; P,G | Roots `maxscriptsize=1650` adds output/prevout/redeem/serialized-witness bounds; scriptSig uses the minimum of this and Core's 1650. Existing Core P2WSH/tapscript stack bounds remain. | Local script/witness resource and form restriction; overloads preserve native callers while production uses initialized standardness. G moves global definition to common policy for standalone-wallet linking. | Extra scans/rejection work versus excluded script work; transaction units, `mempool_roots_local_policy.py`, `feature_segwit.py`, `feature_taproot.py`. Joint override matrix incomplete. | Measure bounds; retain linkage repair. |
| A4 `validation.cpp`: `GetAccurateTransactionSigOpCost`; `policy.cpp`: `CheckLegacyPolicySigops`; P,G | Core equivalent-size factor 20 remains. Roots adds `bytespersigopstrict=20` (zero disables), `maxtxlegacysigops=2500`; the native total sigop cap remains. | Accurate input/witness density gate and potentially executed legacy-sigop gate, solely local. Early total-cost and int32-vsize checks prevent unsafe graph arithmetic. | Additional input/script traversal versus excluded validation work; `mempool_sigoplimit.py`, transaction/mempool units. Measure each gate independently, including zero/large overrides. | Measure gates; retain arithmetic checks. |
| A5 `policy.cpp`: `IsStandardTx`, bare output modes; P | Core permits bare multisig by default and bare pubkeys. Roots `permitbarepubkey=0`, `permitbaremultisig=0`; each can be enabled. | Operator choice of relay forms; rejection can make an eligible received transaction available only through extras. No ownership/privacy inference. | Cheap classification; excluded populations affect all later timings. Transaction units and local-policy valid-block fixtures; exhaustive combined modes missing. | Measure without assuming savings. |
| A6 `policy.cpp`: unknown-witness checks; P | Roots `acceptunknownwitness=1` for outputs; disabling adds rejection. Spending unknown witness versions retains local rejection, with precise reasons. Core has native unknown-witness spending policy. | Explicit output acceptance control; no change to future witness consensus. | Classification cost; `mempool_roots_local_policy.py`, transaction/witness fixtures. Separate inherited spending gate from configurable output gate. | Retain default; measure opt-in restriction. |
| A7 `policy.cpp`: CAT21 locktime filter; P | No Core CAT21 marker gate; Roots `rejectparasites=1`, disable with 0. | Operator marker exclusion, not a consensus locktime rule or demonstrated resource safeguard. | Constant comparison; local-policy rejection/valid-block fixtures. No measured aggregate resource effect. | Measure population effects. |
| A8 `policy.cpp`: `reject_tokens`, `OlgaBytes`; P | No Core token marker filter; Roots `rejecttokens=0`, optional 1 recognizes selected markers. | Local classifier; not a complete token detector, consensus prohibition or privacy rule. | Script recognition scans and false-negative/coverage limits; transaction/carrier/local-policy tests. Measure supported patterns separately. | Measure opt-in mode. |
| A9 `policy.cpp`: `IsStandardTx`, dust/anchor/bare-carrier modes; P | Core ephemeral-dust machinery retained; Roots defaults anchor allowed, ephemeral send/dust false, bare anchor allowed, bare carrier false. `permitephemeral` tokens and individual bare controls select modes. Fixed dust floor remains 3000 sat/kvB. | Additional local dust/form gates before inherited ephemeral package/spend checks. No extra block restriction. | Threshold/classification work and rejected population; `mempool_dust.py`, local-policy, subdust, ephemeral/TRUC tests. Full interaction matrix is not established. | Measure each independent switch. |
| A10 `policy/policy.h`, `policy.cpp`: OP_RETURN budget / `IsStandardTx`; P | Core default carrier budget 100000 vbytes; Roots 83 aggregate script bytes and one OP_RETURN, with `datacarrier`/`datacarriersize` controls. Multiple outputs still reject. | Limit output-carrier relay; `acceptnonstdtxn` changes standardness enforcement, not the independent full-count/nonstandard-carrier gates. | Classification and changed population; `mempool_datacarrier.py`, transaction units. Help's multiple-output wording conflicts with current rejection. | Measure; retain actual contract pending decision. |
| A11 `policy.cpp`: `ScriptDatacarrierBytes`, `GetScriptForTransactionInput`, `DatacarrierBytes`; `PreChecks`; P | No Core equivalent full input/witness recognition. Roots `datacarrierfullcount=1`, `acceptnonstddatacarrier=0`; optional full-count off/nonstandard permission. Includes selected conditional/no-op, Olga and OpNet patterns. | Aggregate recognized input/witness/output data and reject nonstandard carrier independently of general standardness. Recognition is heuristic, not all arbitrary data. | Input extraction, EvalScript/copies and byte scans even on rejects; carrier/taproot/transaction tests. Other recognized patterns and combined switches need bounded workloads. | Measure. |
| A12 `policy.cpp`: `CalculateExtraTxWeight`; `kernel/mempool_entry.h`; G | Core uses native sigop-adjusted weight. Roots `datacarriercost=1` vbyte/data byte (4 weight units), supports 0–1073741823.75 in hundredths rounded upward to integral weight units. Only positive excess over consensus weighting is added. | Raises policy vsize/graph weight; changes fees, eviction ordering, replacement and templates, never serialized weight or block validity. Output data costs extra only above 4 units; discounted witness data can cost extra at the default. | Recognition plus persistent extra weight; `MempoolExtraWeightBoundary`, `GraphPolicyAccounting`, subdust carrier interactions. Do not model default as charging every byte twice. | Measure. |
| A13 `validation.cpp`: dust deficit; `ChangeSet::StageAddition`, entry modified fee; G | No Core deficit. Roots `subdustfeepenalty=1`, optional 0; script-specific positive deficits summed with saturation using admission's live dust snapshot. | Actual fee unchanged; modified fee = saturating(actual fee + operator delta − deficit). Graph/package/RBF/trim/template fee comparisons use modified values. Existing entries are not repriced when dust changes. | Per-output thresholds and accounting; subdust functional matrix and graph/feerate units. Isolate standardness allowances so fixtures actually reach penalty computation. | Measure. |
| A14 `txmempool.*`: absolute modified-fee / weight budgets, `PrioritiseTransaction`; `policy/feerate.*`; G | Core native TxGraph unchanged. Roots bounds resident + staged absolute fees by INT64_MAX and adjusted weights by INT32_MAX; saturates fee-rate results/fee deltas and rejects impossible updates atomically. | Protect native graph subset arithmetic. Pending removals remain reserved until commit; some representable final graphs can conservatively reject. RPC prioritization reports failure. | Scalar bookkeeping on mutation; `GraphPolicyBudgetsAndReuseClaims`, graph accounting and feerate units. Conservative rejection is documented behavior, not exact unbounded equivalence. | Retain repair; measure overhead. |
| A15 `txmempool.*`: synchronized live dust, `EstimateDustFromMempool`; `init.cpp`: scheduled update; G | Core fixed dust. Roots `dustdynamic=off`; `[multiplier*]target:blocks` or `mempool:kvB`, multiplier 3 in thousandths. Immediate then 15-minute updates; initialized long-horizon estimator uses 0.8; missing estimates use configured floor. | Changes new admission thresholds/deficits and live reporting, not resident deficits. Node-owned callback starts only after final successful chainstate load; shutdown joins before destruction. | Optional graph-ordered traversal/estimation and lock contention; `DynamicDustPublication`, `mempool_dynamic_dust.py` covers estimator/floor/update/retry/restart/extremes. Measure modes separately with scheduler phase fixed. | Retain lifecycle/scaling repair; measure modes. |
| A16 `node/mempool_persist.cpp`: `DumpMempool`; G | Core dumps operator deltas. Roots must exclude admission deficit from persisted delta, then recompute one deficit on reload. | Prevents double penalty after restart; actual fee, configured floor and operator delta preserved. | Copy/serialization cost; dynamic-dust restart and subdust tests. Old format/previous-release availability is a separate coverage limit. | Retain repair. |
| A17 `validation.cpp`: RBF mode gates; `node/mempool_args.cpp`; G | Core unconditional full RBF; Roots defaults Always (`fee,-optin`), supports Never/OptIn and `mempoolfullrbf` precedence. OptIn accepts signaling or v3 originals. | Restricts replacement permission, preserving native cluster limits/fee diagrams. Never also disables sibling eviction. Replaced entries can remain in reconstruction cache via inherited insertion. | Small gates plus different churn; `mempool_rbf_policy.py`, RBF/package/sibling/TRUC tests. Default full RBF equivalence does not retire configured modes. | Measure modes; retain native economic checks. |
| A18 `validation.cpp`: single/package TRUC gates; G | Core enforced TRUC; Roots default Enforce, alternatives Reject/Accept and aliases resolved by parser. | Reject disallows v3 even on reorg; Accept skips TRUC-specific limits, not general mempool/consensus checks. Enforce preserves sponsorship and native TRUC checks. | Different graph/populations, not assumed savings; `mempool_truc.py`, graph-policy and subdust package cases. Complete RBF×TRUC matrix remains a question. | Measure opt-in modes. |
| A19 `validation.cpp`: relay maturity / coin-block accumulation; P | No Core relay age gate. Roots both zero (disabled), optional nonnegative `minrelaymaturity`, `minrelaycoinblocks`. Depth at next block, unconfirmed depth zero; capped integer accumulation. | Reject insufficient confirmed depth or summed satoshi-block age, separately from consensus coinbase maturity. Reorg admission's inherited bypass behavior must be distinguished from fresh admission. | Input lookup/arithmetic even with configured zero; `mempool_roots_local_policy.py` enabled/disabled/exact bounds/tip reorg/valid blocks. Mining priority is a separate use of coin age. | Measure enabled and disabled costs. |
| A20 `validation.cpp`, `txmempool.*`, entry `m_script_claims`; G,R | No Core reuse index. Roots `spkreuse=allow`; conflict mode tracks full scripts with sets of all creators/spenders. | Reject twins, input/output reuse, ancestor/staged-package conflicts; independent resident conflicts can undergo native replacement. Reorg/removal keeps all remaining claims. Not address ownership or a privacy score. | Per-entry script copies/maps plus pool maps/sets, admission lookup and removal. `GraphPolicyBudgetsAndReuseClaims`, `mempool_graph_policy.py` covers modes/replacement/reorg/multiple claims. Measure bytes and mutation time with reuse allow/conflict independently. | Measure; retain collision/staging repair. |
| A21 `CTxMemPool::DynamicMemoryUsage`, entry `DynamicMemoryUsage`; G | Native estimate includes graph/containers. Roots adds script-claim maps, scripts and creator/spender sets to the estimate used by `LimitMempoolSize`/`TrimToSize`. `maxmempool=300` decimal MB remains inherited. | Policy indexes consume admission/eviction budget. Estimation is not RSS; graph trimming is inherited but ranks Roots modified fees/weights. | Accounting traversal and metadata; `GraphPolicyTrimUsesModifiedFees`, pool tests. Shared allocations/allocator overhead need separate measurement. | Retain accounting; measure eviction/population. |
| A22 `node/miner.*`, policy template constants, `MakeBlockBuilder`; M | Core native chunks, protocol-max weight and 1 sat/kvB floor. Roots 1200000 weight, 1000 sat/kvB, 100000-byte priority area; size-only/weight-only/both and priority=0 supported. | Local template selection only. Priority uses active UTXOs; residual graph excludes prefix fees/weights, preserves dependency order and actual rewards. This row covers shared accounting, not a proposal to change mining preferences. | Nonempty priority prefix builds O(mempool) temporary residual graph plus cluster work. `miner_tests.cpp`, `mining_roots_template.py`, subdust rewards/interactions. Keep priority disabled as a control. | Measure shared cost; retain reward/resource repairs. |
| A23 `rpc/mempool.cpp`: initialized `policy`, `fullrbf`; `node/interfaces.cpp`; R,G | Core lacks Roots policy object; Roots reports effective initialized options/live dust with units and enforcement gates, fullrbf means Always. | Operator observability of admission; queries do not reconfigure policy or expose cache metrics. Dust consumers use synchronized getters. | JSON allocation/lock time; `rpc_effective_policy.py`, mempool publication units. RPC reporting cost is separate from per-transaction cost. | Retain interface. |
| A24 `policy.cpp`/`.h`: `MaybeReject_`, reason-prefix/ignore-reason overloads; P | Core returns fixed reasons/Booleans. Roots adds detailed reasons and an ignore set with empty default. Production admission passes no ignore set; nonempty sets are test seams, not an operator bypass. | Preserves independently testable local rejection contracts and native entry points. Does not change script consensus. | Empty-set lookups/string construction; transaction tests exercise individual gates. No production configuration exposes ignore sets. | Retain test seam; measure only within standardness cost. |
| C1 `net_processing.h`: cache count; N | Core 100; Roots 32768; explicit count=100 is a control, 0 disables. Negative count clamps through inherited option parsing. | More potential rejected/replaced/orphan references, independent of mempool admission/expiry/size. | Slot allocation and lookup work, plus increased shared-reference lifetime; `p2p_compactblocks_extratxs.py` count/wrap/one/invalid tests. Occupancy is not allocated count. | Measure. |
| C2 `node/peerman_args.cpp`, `AddToCompactExtraTransactions`; N | Core no byte budget; Roots 10000000 charged bytes, fixed-point decimal MB with four decimals (100-byte increments); malformed retains default, negative/zero disables. | Pre-add oldest-first pressure eviction; count overwrite also applies. Slots = min(count, budget / sizeof(slot)); zero slots returns. Budget independently bounds slot bytes and charged dynamic transaction bytes, not their sum. | Usage traversal per attempt/eviction; allocation ≤ configured budget separately. Size eviction/exact-approximate boundaries/extreme-count tests exist. Mixed sizes, sparse slots and exact native usage still need focused characterization. | Measure; retain overflow/allocation repair. |
| C3 `AddToCompactExtraTransactions`: per-entry exclusion; N | Core `<100000` check only on rejected insertion site. Roots common function also excludes replacements at usage ≥100000 or usage > budget. | Rejects oversized cache entries without changing mempool or block validity. At exactly budget an entry is eligible if below individual limit. | Avoids long-lived large references; replacement threshold has no dedicated exact-boundary test identified. | Retain compatibility repair; characterize threshold. |
| C4 `blockencodings.cpp`: `InitData`; inherited | Identical Core algorithm, Roots supplies potentially larger/sparse extra vector. Wtxid/short-ID matching and collision handling retained. | Cache hit avoids missing-transaction request; neither admission nor validation bypass. Loop can exit after all required transactions found. | Unsuccessful search can scan allocated slots including empty ones. Benchmark zero/full overlap, ordering, sparse occupancy and misses; existing compactblocks units/functional and configured bench are starting points. Larger-vector cost is a hypothesis. | Retain algorithm; measure input-size effect. |
| C5 `core_memusage.h`: overload choice; inherited helper, N caller | `RecursiveDynamicUsage(*tx)` charges input/output vectors/scripts/witness backing storage. Shared-pointer overload additionally charges transaction/shared allocation; caller uses the former. | Cache counter excludes object/shared allocation and slot storage. Repeated references may count the same underlying dynamic bytes repeatedly; RSS may persist after eviction through another owner. | Do not call 10 MB a strict total-cache or RSS ceiling. Native accounting/ownership and RSS must be reported separately. Existing functional sizes are empirical approximations. | Clarify wording after characterization. |
| F1 `MaybeSendFeefilter`, constructor rounder, `send_feefilter`; N | Core sends filters; Roots defaults on, optional `feefilter=0`. Bucket seed 100 sat/kvB if initialized incremental fee <1000, otherwise 1000; Core seed is 100. | Advertises fee expectations to peers; does not admit, evict or delete extras. Same low-incremental default seed as exact Core, additional high profile/control differs. | Message frequency/quantization and received population; `p2p_feefilter.py` both profiles and IBD exit. Hold fixed in cache experiments. | Retain default; measure alternate profile. |

## Changed-hunk coverage boundary

The following partition accounts for every changed production hunk in the
issue-relevant files against the exact base. Includes, declarations, overloads
and assertions travel with their consumers; they are not additional features.
An unchanged file has been checked directly, not inferred from historical prose.

| Current path / changed symbols or region | Rows / disposition |
| --- | --- |
| `policy/policy.cpp`: dust exemption, standardness, carrier helpers, input/witness checks and reason wrappers, extraction/extra weight | A3–A12; inherited stack/consensus checks retained under local reason wrappers. |
| `policy/policy.h`, `policy/settings.*`: constants, option snapshot and declarations | A2–A15, A22, A24; unused ancestor/descendant constants have no restored legacy admission index or active control claim. |
| `policy/feerate.*` | A14–A15; saturation/scaling. Other files under `policy/` have no exact-base delta. |
| `kernel/mempool_options.h`, `kernel/mempool_entry.h` | A9–A21; no changed kernel consensus chain code. |
| `validation.cpp`: accurate sigop helper and `MemPoolAccept` precheck/single/package branches | A3–A20. No hunks after local mempool acceptance; `CheckBlock` to EOF is identical. |
| `txmempool.*`: constructor/live dust, add/remove/check, conflict lookup, fee updates, memory estimate, ChangeSet and builder accessor | A12–A22. Native graph fee/weight inputs change; `txgraph.cpp`/`.h` themselves are identical to Core. |
| `node/mempool_args.*`: validators/parsers and initialized options | A1–A20; native cluster options remain authoritative. |
| `node/mempool_persist.cpp` | A16. |
| `node/miner.*` | A22; full template preference review excluded except shared accounting and consensus separation. |
| `node/interfaces.cpp` | A15/A23 getter/reporting integration; added `confrw` access is configuration plumbing outside policy cost comparison. |
| `node/peerman_args.cpp` | C1–C3, F1. |
| `net_processing.cpp`/`.h` | C1–C5/F1; blockfilter permission advertisement, last-block-announcement statistics and duplicate manual-fetch safeguard are unrelated peer controls, explicitly excluded. |
| `init.cpp`: help/defaults, validation, globals, post-load scheduling | A1–A23/C1–C3/F1. `confrw`, blockfilter permission validation, startup forceinbound/Bloom/clearnet setup and networking options are unrelated configuration/peer controls, excluded. |
| `rpc/mempool.cpp` | A23 and safe prioritization error from A14. |
| `node/eviction.*` | Inbound connection eviction, not transaction mempool eviction; excluded. |
| `node/txdownload*`, `node/txorphanage.*`, `blockencodings.*`, `core_memusage.h` | Exact-base identical. Eligibility/orphan dedup, cache consumers and usage helpers traced above. Orphan resource accounting is inherited, not a Roots policy delta. |

The relevant test delta consists of the assertions named per row, new Roots
policy/cache tests and adaptations of Core fixtures to Roots' defaults. Explicit
carrier/script/fee/template allowances in inherited package, RBF, TRUC, cluster,
mining, wallet and large-block fixtures preserve their upstream scenario; they
do not demonstrate unrestricted production admission. Test/fuzz oracle changes
are not product policy. Dormant stats, GUI/wallet controls, release/CI machinery
and unrelated peer controls are outside this issue's cost inventory.

## Existing evidence limits and documentation conflicts

This subsection records the inventory-stage gaps; the characterization sections
below supply the subsequently added cache assertions.

The current `p2p_compactblocks_extratxs.py` already covers dust, low fee,
oversized OP_RETURN and nonstandard-script rejection with mempool absence and
cache-hit missing indices, as well as count/byte disabling, wraparound, one
slot, malformed/negative inputs, tiny budgets and large entries. It is not an
absent suite. Its `send_compact_block` helper (approximately 174–231) returns
missing indices without completing a cache-miss request by sending `blocktxn`.
No-relay observation, a policy-specific reason assertion, mixed-size native byte
boundaries, replacement threshold and orphan shared-lifetime cases still need
focused coverage. Extend observable gaps without discarding existing cases.

Historical [feature-catalog](roots-features.md) statements that dynamic dust is
unwired and that script reuse has no dedicated functional coverage are superseded
by the current node-owned scheduler and `mempool_graph_policy.py`. Earlier
parsing-only/acceptance-pending text describes earlier stages. Its v30.3 direct
base sentence is historical; this review uses the exact v31.1 object above.
The initial cache help's total-memory wording overstated C2/C5 accounting; its
corrected description now names charged dynamic storage, separate overhead,
shared ownership, independent budgets and both zero-valued disable controls.
Only help/documentation changed; no cache behavior or default changed. The help
about multiple OP_RETURN outputs still conflicts with A10. Incremental-fee help says 1000 sat/kvB
while initialized default is 100, equal to the exact Core base. Preserve actual
defaults and controls until an explicitly reviewed correction.

No row is declared redundant from a similar upstream feature name. Native
TxGraph, orphan/download handling and compact reconstruction are reused upstream
mechanisms; the surrounding defaults, accounting inputs and opt-in policies
remain separate. The following oracles define subsequent characterization and
measurement; there is no numerical resource-saving conclusion here.

## Observable contracts and paired controls

These are requirements for fixtures and measurements, not newly executed test
results. A named current assertion protects only its stated case. **Missing**
identifies coverage to add or a measurement that has not been made; it does not
authorize changing a product contract.

Use fixed transactions/seeds and isolated regtest nodes. For each policy pair,
keep cache, fee-filter, scheduler phase, compiler/profile/allocator, peer
permissions, fee deltas and chain state fixed. Change the named setting alone;
document prerequisite allowances needed to reach the intended gate. Assert the
actual rejection reason so an earlier policy failure cannot masquerade as the
target workload. Run ordinary accepted traffic alongside each special class.

Measure offered, rejected, admitted, replaced and resident counts separately.
Report CPU per offered transaction as well as per successful mutation, charged
bytes and steady/peak RSS separately, and include allocator/measurement scope.
Comparing differently populated mempools without those denominators cannot
establish algorithmic cost. Test acceptance with `testmempoolaccept` first, but
use actual peer TX reception for cache-insertion and no-relay assertions.

In cache comparisons hold Roots policy fixed. Pair Roots defaults with count
100 and disabled caching, then explicit small count/byte sweeps; compare exact
Core defaults separately and identify every policy mismatch. For performance
data predeclare scale and repetitions, warm up, retain failed/skipped samples
and at least five paired measured repetitions, and report dispersion. Neither
one fast run nor a difference within noise justifies removing an operator mode.

| ID | Observable contract and paired workload/control | Current assertion or explicitly missing evidence |
| --- | --- | --- |
| A1 | Equivalent valid CLI/config/JSON/confrw settings initialize equal effective policy; malformed types/tokens/bounds fail with the named diagnostic. Pair omitted/default with explicit default, then valid/invalid grammar and precedence cases; measure startup time, not transaction admission CPU. | `policy_args_tests.cpp`, `feature_policy_args.py` and `rpc_effective_policy.py` assert grammar/precedence/reporting. Missing: measured parse cost and exhaustive cross-option combinations. Do not apply Roots strictness to inherited Core options. |
| A2 | At fixed vsize, actual/modified fee and delta, one satoshi below the configured individual floor rejects; at the rounded floor it passes that gate. Pair `minrelaytxfee=0.00001000` with `0.00000100` and zero, keeping incremental fee constant; run ordinary parents and enforced-TRUC sponsorship separately. | Subdust `test_threshold_and_low_rate_boundaries`/`test_package_and_truc`, package/TRUC fixtures and effective-policy reporting cover selected boundaries. Missing: matched per-offer CPU and admitted-population measurements across the three floors. |
| A3 | ScriptSig, output/prevout/redeem-script and serialized-witness size gates honor their initialized limits without changing consensus. Pair sizes at limit and one above; vary `maxscriptsize` alone and keep inherited stack caps satisfied. | `transaction_tests.cpp`, local-policy, segwit and taproot fixtures contain script/witness assertions; valid-block cases establish local separation. Missing: complete per-surface override matrix and matched byte-scan timings. |
| A4 | Each legacy count/strict density gate rejects its own over-bound transaction; zero strict factor disables only that gate. Pair strict 20/0 and legacy 2500/explicit alternate independently, at exact/one-over boundaries; preserve the native total cap and equivalent-size factor. | `mempool_sigoplimit.py` methods `test_sigops_limit`, `test_sigops_package`, `test_legacy_sigops_stdness` plus transaction units. Missing: per-input traversal measurements and full strict×legacy×equivalent-factor interaction matrix. |
| A5 | Bare pubkey/multisig candidates differ in local acceptance when only the corresponding permit switch changes; valid blocks accept either. Pair each default 0 with 1 separately using equal fee/shape and a standard-script control. | Transaction and `mempool_roots_local_policy.py` reject/valid-block assertions. Missing: matched timings and all combinations with dust/carrier gates. |
| A6 | An unknown-version output reaches the configured output gate, while spending it retains the separate local input rule. Pair `acceptunknownwitness=1/0` using the same consensus-valid output; measure output classification and spending paths separately. | Transaction/witness tests and local-policy valid-block case. Missing: complete output-versus-input version matrix with measured cost; an enabled output switch does not promise standard unknown-version spends. |
| A7 | CAT21-marker transaction rejects with the marker gate enabled and is eligible under disabled gate when other policy passes; both blocks validate. Pair `rejectparasites=1/0` with marker/nonmarker transactions differing only in locktime and compatible finality. | `mempool_roots_local_policy.py` marker/reason/block assertions. Missing: measured filter overhead and identified real-world population/benefit data. |
| A8 | Only supported token-marker patterns are subject to the opt-in filter. Pair `rejecttokens=0/1`, marker/nonmarker controls and explicit carrier allowances, and assert the token reason rather than an earlier carrier rejection. | Transaction/carrier/local-policy tests cover selected patterns and valid blocks. Missing: coverage/timing for every recognized pattern and false-negative characterization; do not infer all tokens are recognized. |
| A9 | Each ephemeral/bare-output switch changes only its specified local permission; threshold, exact/nonzero dust and package-spend conditions remain distinct. Pair default with each `anchor`, `send`, `dust`/negative token and bare-anchor/bare-carrier Boolean separately; assert reason and test a valid block. | `mempool_dust.py`, local-policy and subdust `test_anchor_and_standardness`, `test_threshold_and_low_rate_boundaries`; native ephemeral tests. Missing: complete combined-token/prerequisite matrix and measurements. |
| A10 | Aggregate OP_RETURN budget is 83 by default; multiple outputs remain locally rejected even with a larger budget. Pair exact-budget/one-over single output, carrier enabled/disabled and explicit budget changes; independently probe two outputs with adequate budget. | `mempool_datacarrier.py` budget/multiple-output/valid-block assertions and transaction units. Missing: measured classification/population effects; help wording does not supply a contrary acceptance oracle. |
| A11 | Full-count and nonstandard-carrier gates act independently of `require_standard`. Pair each recognized input/witness/output pattern with one switch changed (`datacarrierfullcount=1/0` or `acceptnonstddatacarrier=0/1`); hold cost and aggregate budget constant. | `mempool_datacarrier.py::test_taproot_carrier`, transaction detection units and local-policy independent-gate assertions. Missing: other pattern/full-count/fractional-cost combinations and scan/copy costs. |
| A12 | Extra weight never reduces actual weight; policy-vsize/graph effects match recognized bytes and rounded configured weight units. Pair carrier cost 1/0 and a fractional/above-1 value while keeping identical scripts, accepted population and sigop factor; inspect RPC actual weight versus policy vsize and graph/trim/template order. | `MempoolExtraWeightBoundary`, `GraphPolicyAccounting`, subdust `test_carrier_cost_interaction` and miner residual boundaries. Missing: paired CPU/RSS and broad input-pattern fractional rounding. Use native units, not serialized byte size as a dynamic-memory proxy. |
| A13 | Modified fee reflects one admission-time deficit and operator delta, with saturating additions in their actual order; actual fees/rewards stay fixed. Pair `subdustfeepenalty=1/0` with no-dust, partial-deficit, multiple-output and exempt outputs, then a nonzero delta/replacement/package case. | Subdust's twelve invoked test methods cover arithmetic, exemptions, fees, packages and mining; graph units check inputs. Missing: paired accounting/eviction measurements. Explicit ephemeral/standardness allowances must let the candidate reach the penalty. |
| A14 | Aggregate absolute-fee/weight reservations reject excess safely; aborted staging releases reservations and a failed resident delta changes nothing. Pair representable near-boundary native graph operations with one-over proposals, testing commit/abort and pending removals; measure ordinary scalar overhead separately from deliberate extremes. | `GraphPolicyAccounting`, `GraphPolicyBudgetsAndReuseClaims` and fee-rate units. Missing: independent resource timings; no claim that every mathematically representable final graph must pass the conservative reservation rule. |
| A15 | New admission/reporting sees one synchronized live dust rate; absent estimates use the floor, changes do not reprice resident deficits, and retry/shutdown cannot retain a dead owner. Pair off/mempool/target modes with fixed estimator contents and update phase, zero/available estimate, floor/clamped/extreme multiplier. Measure scheduled CPU and lock latency separately from admission. | `DynamicDustPublication`, `mempool_dynamic_dust.py` run_test covers initialization/update/floor/estimator/reindex/reward cases; native/fuzz arithmetic definitions identify saturation checks. Missing: benchmark noise/phase controls and concurrent tail-latency data. |
| A16 | Reload retains operator delta and applies exactly one freshly calculated deficit. Pair equivalent transactions before/after restart at a fixed rate, then a changed live rate, inspecting base/modified fee separately. Measure dump/load time and bytes, keeping cache state outside persistence assumptions. | Dynamic-dust restart checks and subdust fee-delta tests. Missing: measured dump/load cost and available previous-release compatibility runs; restarting clears the extra cache, so reconstruction comparisons must refill it. |
| A17 | Never rejects replacement; OptIn accepts signaling or v3 originals subject to economics; Always preserves native economic gates. Pair identical conflict sequences under Never/OptIn/Always and signaling/non-signaling originals; assert mempool replacement plus ordinary relay and cached originals independently. | `mempool_rbf_policy.py::POLICIES`/`check_replacement`, package/RBF/TRUC regressions. Missing: mode-matched churn costs and exact replaced-cache threshold coverage (C3). |
| A18 | Reject disallows v3; Accept skips TRUC-specific restrictions only; Enforce applies single/package/sibling rules. Pair identical valid v3/non-v3 packages across all three modes with fixed RBF, relay floor and cluster limits, then test reorg admission separately. | `mempool_graph_policy.py::run_test`, `mempool_truc.py` replacement/reorg/package cases and subdust `test_package_and_truc`. Missing: complete RBF×TRUC cross-product and measured graph-cost comparison. |
| A19 | Fresh admission compares next-height maturity and bounded satoshi-block sum exactly; disabled bounds admit the same otherwise-valid fixture and consensus-valid blocks ignore local age. Pair zero/nonzero settings separately, exact/one-below bounds, confirmed/unconfirmed inputs and a tip reorg. | `mempool_roots_local_policy.py` enabled/disabled/extreme/reorg/valid-block assertions. Missing: per-input CPU at both disabled/enabled defaults and broad multi-input shapes; do not infer priority cache use. |
| A20 | Allow avoids policy conflicts; conflict mode rejects twins/change/ancestor/staged reuse and permits eligible independent script-conflict replacement. Pair `spkreuse=allow/conflict` with identical scripts and fee sequence; test multiple claims and removal/reorg before probing reuse again. | `GraphPolicyBudgetsAndReuseClaims`, `mempool_graph_policy.py::run_test` replacement/reorg/removal assertions. Missing: script-length/claim-count scaling and measured allow-mode bookkeeping. Aliases resolve through A1; do not introduce an ownership oracle. |
| A21 | Reported mempool usage includes per-entry and pool script-claim containers; trim evicts using native graph order with Roots modified fees/weights. Pair equal accepted populations under allow/conflict, then a fixed small maxmempool with controlled deficits; compare charged usage, unique ownership and RSS separately. | `GraphPolicyTrimUsesModifiedFees`, native pool accounting assertions. Missing: measured container/allocator overhead and eviction under sustained mixed policy churn; cache bytes must not be added to getmempoolinfo's mempool budget. |
| A22 | Template contains each dependency-ordered transaction once, respects local resource limits, and rewards actual fees despite modified selection. Pair priority 100000/0 with identical admitted graph and explicit identical size/weight/minfee limits; measure residual graph allocation only after a nonempty prefix. | `miner_tests.cpp` residual/shared-descendant/delta boundaries, `mining_roots_template.py`, subdust `test_mining_and_block_acceptance`. Missing: O(mempool) residual-graph scaling measurement; this review does not require changing local template preferences. |
| A23 | Effective-policy fields match initialized enforcement, units and live dust; `fullrbf` reflects Always, not every possible v3 replacement. Pair omitted/explicit defaults, overrides and disabled modes with admission probes; measure RPC polling separately from policy processing. | `rpc_effective_policy.py` report/admission parity and concurrent `DynamicDustPublication`. Missing: quantified polling/lock costs; use existing interfaces without adding permanent cache metrics. |
| A24 | Default empty ignore set preserves production rejection; a test-only named ignore suppresses that reason while allowing later gates to run. Pair native overload/default calls with precisely selected nonempty test sets; production validation supplies none. | `transaction_tests.cpp` per-reason isolation and current validation call sites. Missing: independent overhead measurement; no user-facing ignore option is promised. |
| C1 | Cache count bounds occupied references, count 0 yields misses, one slot overwrites, and ring wrap replaces prior occupants. Pair count 32768/100/0 and small counts at fixed bytes/policy; send the same unique rejected/replaced traffic and probe absolute missing indices. | Extra-tx functional count/wrap/one/disabled methods. Missing: duplicate-attempt and mixed eviction sequences; complete each cache-miss block with requested transactions and assert final tip. |
| C2 | At every completed insertion, summed charged transaction bytes ≤ budget; slot bytes are bounded independently. Exact-budget entry is eligible below C3 threshold; one-over skips; pressure removes oldest references before insertion. Pair fixed count with zero/exact/one-below native-usage budgets, mixed sizes and multiple evictions across wraparound. | Extra-tx size-zero/eviction/boundary/tiny/extreme-count/large-entry methods. Missing: deterministic native exact-byte oracle, mixed-size ordering and sparse-slot probes; empirical serialized-size discovery is insufficient. |
| C3 | Usage 99999 is eligible when budget permits; usage 100000 is excluded on both rejected and replaced paths, independent of serialized size. Pair native usage immediately below/at threshold with equal count/budget, and assert admitted replacement removes the original before probing it. | Common source gate and existing large-rejected-entry case; missing dedicated native-accounting replacement-boundary fixture and exact attainable-size mapping. If allocator increments make 99999 unattainable, use nearest representable below/at-or-above values and state them. |
| C4 | Hits reduce requested missing indices; misses still accept the identical valid block after `blocktxn`. Pair zero/full/partial overlap, ascending/reversed cached order, dense/sparse slots and collision fixtures at fixed block/mempool contents; measure InitData CPU, requested count, round trips, wire bytes and acceptance latency. | Native blockencodings tests, `p2p_compactblocks.py`, existing extras hit/miss index assertions. Missing in extras helper: final miss-path acceptance and measured slot-scan scaling. Do not label zero-hit synthetic rates representative of mainnet. |
| C5 | Counter equals the sum of `RecursiveDynamicUsage(*entry)` for occupied slots, including repeated references; object/shared allocation and slots are separate. Pair distinct allocations versus shared orphan/mempool/cache references, then erase owners one at a time; measure charged, unique and RSS bytes independently. | Native usage overloads establish the equation; existing functional boundary discovery is approximate. Missing: exact charge/ownership fixture and allocator/RSS measurement; eviction need not immediately reduce RSS. |
| F1 | Filters on/off and initialized incremental-fee profiles produce their configured wire behavior; cache settings alone do not change fee-filter seed. Pair `feefilter=1/0` and incremental fee below/at 1000 separately; synchronize IBD exit and peer receipt, holding cache and offered transactions fixed. | `p2p_feefilter.py` quantization/profile/IBD assertions. Missing: measured message/received-population costs; do not attribute a Core/Roots policy-floor population difference to caching. |

For policy-specific cache characterization, use an ordinary unprivileged sender
and an independent unprivileged observer with announcements synchronized. Assert
the intended local rejection reason and mempool absence, wait through the test's
defined relay observation window, then send a compact block containing the same
consensus-valid transaction. Cache-hit and disabled/evicted miss runs must both
finish with the block accepted; the miss run must supply requested transactions.
Run low-fee, replacement and orphan classes separately. Record known shared
references so retained RSS is not mistaken for unique cache allocation. The
permission exceptions described above require separate fixtures and cannot
substitute for the no-ordinary-relay assertion.

Retiring a row requires a separate equality proof for observable behavior,
defaults, optional modes, errors/units and interactions against the exact base,
plus current tests. At present only the explicitly identified underlying Core
mechanisms are unchanged; no Roots control is classified as redundant. Every
unmeasured question in these tables remains unanswered rather than a benefit or
harm claim.

## Cache lifecycle characterization

The native `compact_extra_transactions_tests` fixture calls the unchanged
production insertion routine through explicitly named unit-test methods. It
checks zero count/bytes, no allocatable slots, one slot, an exact native charge
and one byte below that charge, and an extreme count bounded by slot storage.
The native options accept individual bytes; the public size option retains its
100-byte granularity. These are separate boundaries.

Mixed-size tests fill four slots, then insert an entry requiring multiple
evictions across wraparound. They check the remaining references and actual
`PartiallyDownloadedBlock` availability after each insertion, including sparse
slots and the next two insertions. Expected oldest-first results are explicit;
serialized size is never used as a dynamic-storage oracle. The P2P suite retains
all its previous count/size cases and adds duplicate rejected attempts, actual
replacement, and duplicate orphan reception followed by parent resolution.
The resolved low-fee child leaves the orphanage and remains outside the mempool;
its reference still supplies compact reconstruction.

The native threshold test constructs the closest attainable charges below and
at-or-above 100000 bytes for its script shape. It exercises both the common
insertion gate and the production replaced-transaction insertion loop; these
structural fixtures do not claim consensus validity or actual mempool replacement.
The separate P2P replacement test first proves that the original left the mempool,
then reconstructs and accepts a block containing it. Neither test equates a
serialized byte count with native allocation.

Raw insertion can hold the same shared reference twice and charges it twice.
Network duplicate suppression is a separate eligibility rule, covered by the
P2P rejected/orphan cases. Dropping cache ownership does not destroy an allocation
while another shared owner remains. The pointer-overload usage includes an
allocation charge omitted by the cache's dereferenced overload. These assertions
describe ownership and charged storage, not allocator RSS or immediate release
to the operating system. No cache default, policy, RPC or production insertion
behavior is changed by these fixtures.

## Policy rejection, relay and completed reconstruction

`test_policy_cache_relay_separation` in
`test/functional/p2p_compactblocks_extratxs.py` funds three nodes on the same
chain, then restarts them without inter-node links. It offers the identical
transaction with final locktime 21 to each ordinary P2P sender. The otherwise
ordinary, adequately funded transaction is rejected specifically with Roots'
`parasite-cat21` reason, remains outside every mempool, and is separate from
the suite's low-fee, replaced and orphan cases.

Each node has an independent observer. The fixture disables Bloom-filter
permission for this case and asserts that every sender and observer has an
empty permission list. Three fresh accepted transactions provide positive
announcement controls over three relay epochs, each advanced by 60 seconds of
mock time. Each observer receives each control INV; synchronized checks record
neither the rejected txid nor its wtxid. This is bounded ordinary-relay evidence,
not a claim about every possible future timing or privileged peer behavior.

Count limits 2, 0 and 1 receive the same rejected transaction and one additional
reject. This leaves the target cached, disabled or evicted respectively. The
fixture sends identical compact-block bytes to all three nodes. The cached
node requests nothing; the disabled and evicted nodes request absolute index 1.
Both miss paths supply that requested transaction in `blocktxn`, and all three
nodes finish with the same accepted block hash and empty mempools. The block
also confirms the three positive controls, which were available from each
mempool independently of the rejected transaction's cache entry. The older
size/count helpers retain their non-mutating request probes so that they can
recheck the same transaction population after eviction; the dedicated case
establishes complete consensus-valid hit and miss paths.

For reproduction, after the documented `build-issue56` build, run:

```sh
python3 test/functional/p2p_compactblocks_extratxs.py \
  --configfile=build-issue56/test/config.ini \
  --tmpdir=/tmp/issue56-cache-contract
build-issue56/bin/test_bitcoin \
  --run_test=compact_extra_transactions_tests --report_level=detailed
```

Use a fresh disposable temporary directory per functional run. The complete
cache script, including all earlier cases, passes on the local Linux build;
the native focused suite passes three cases and 129 assertions. Native charges
nearest the exclusion threshold for this fixture are 99984 and 100000 bytes.
These results establish the assertions above, not RSS, bandwidth or CPU costs.
Full applicable suite acceptance remains a separate gate. Python lint is
unexercised because `lief` is unavailable; existing include-lint findings in
unchanged Qt/oracle/fuzz files remain unrelated limitations.

The inherited `ForceRelay` branch in `PeerManagerImpl::ProcessMessage` may
reannounce an already resident transaction to support gateway use. It explicitly
declines a non-mempool transaction; the blocksonly relay-permission exception is
also separate. Neither permission is granted to this fixture's peers, and no
privileged-peer result is inferred. Cache retention changes neither admission
nor block validation.

## Controlled cache experiment and bounded dispositions

The Linux-only [offline harness](../contrib/roots/cache-bench.md) compares the
exact Core base above with Roots product code at
`fefd98420907870a60c103720a9589bba16b2e5f`. Identical temporary instrumentation
is applied only to isolated source exports; the normal production tree and all
defaults remain unchanged. Both builds use the same compiler, RelWithDebInfo
profile and system allocator, with fixed `-j 2`. Builds finish before node runs.

The predeclared `paired-128` experiment uses seed 560031, 128 independent input
fixtures per class, one warmup pair and five measured pairs. Each pair includes
all eight documented variants and twelve cells, in a fixed-seed shuffled variant
order. Churn and adversarial cells offer four unique alternatives per input.
Every cell restarts the node, uses an explicit disposable regtest datadir, then
requires the resulting block to validate after supplying every missing
transaction. No public traffic or representative mainnet rate is assumed.

The cache controls hold Roots policy fixed: defaults, count 100, disabled,
count 1, 10,000 charged bytes, and count 8 with 1,000 charged bytes. The separate
`rejectparasites=0` variant changes that one policy only. Core's policy differs:
it admits the CAT21 fixtures, so a Core reconstruction hit can be a mempool hit
where Roots uses the extra cache. Equal wire outcomes do not establish equal
admission, memory ownership or operator policy. The ordinary, sufficiently
funded accepted workload provides a matching admitted-population control.

Raw results distinguish offered transactions, initial-state preflight candidates
and reasons, final resident/orphan counts, attempted cache calls, actual slot
stores, block transactions, cache/mempool hits and requested missing transactions.
Preflight is non-mutating and precedes the timed load. In particular, four
alternatives can each pass preflight against an initially empty mempool while
only one remains after conflicting offers. Actual validation outcomes for every
individual conflicting offer are not event-instrumented; no measured aggregate
rejection/admission rate is inferred from those preflight results.

Metric units and scope are defined in the harness document. Charged dynamic
bytes, native slot storage, distinct object pointers, external shared references
and process RSS remain separate quantities. VmRSS is sampled every 5 ms; its
observed peak is a lower bound, while VmHWM also includes startup. Process CPU
uses host clock ticks, so a zero delta can mean work below the tick resolution.
Successful-store CPU has a separate denominator from excluded/disabled calls.

The temporary whole-vector ownership snapshot precedes the InitData timer and
warms the vector. Its work and default functional debug logging affect overall
latency; snapshot work itself scales with allocated slots. There is no quiet,
uninstrumented paired control. Even a large end-to-end timing delta therefore
cannot establish a production latency difference, and InitData results describe
the instrumented, warmed inputs only. Five samples per cell support median,
range and sample standard deviation; p95 remains unavailable rather than pooling
different workloads into an artificial tail distribution. The zero-offer
overlap cell has an empty cache, so fully populated unrelated-cache zero-hit
lookup cost remains a separate unanswered question.

### Collected samples and reconstruction outcomes

Collection ran from `2026-10-25T12:08:17.514079Z` through
`2026-10-25T12:16:53.927031Z`. All eight warmup variant runs and forty measured
variant runs completed with exit 0: 96 warmup cells and 480 measured cells,
576 accepted blocks total. No failed, skipped or partial sample was discarded;
there were no failed samples. Serialized fixtures match across all variants
and repetitions for each of the twelve cases. Every row satisfies the audited
hit/miss denominator, wire-sum, ownership/slot/charge-bound and accepted-tip
checks, including every disabled-cache miss path.

Reproduce from the repository root after the harness build/smoke instructions:

```sh
python3 contrib/roots/cache_bench.py run --work /absolute/disposable/bench-builds \
  --name paired-128 --count 128 --seed 560031 --warmup 1 --repetitions 5
```

Original evidence is retained under the experiment work directory. These
identifiers bind the local collection, rather than promising identical timings
on another machine:

| Artifact relative to the work directory | SHA256 |
| --- | --- |
| `builds.json` — GCC 14.2.0, commits, flags, two binary digests, temporary patches and build logs | `3ca0e56eaa20376f20b4d4647bab4c199d2d47b71240711c3946b1278e87da89` |
| `paired-128/design.json` — original UTC declaration, exact flags, counts, seed and harness digests | `4b78206e150edc0e5c4ebebb90bf5e232a0671f49571081a9902ef21c06d69d9` |
| `paired-128/runs.json` — all 48 commands, exits, original UTC times, binary identities and raw result/log paths | `6bda1bc5396d854d850eab0c714a27d3790c45ca82faffa2b616c8c21f87face` |
| `paired-128/summary.json` — every measured metric/case/variant, n, median, range and sample standard deviation | `fb1678912fa098b1a3ac203baa45df6356b2546cd75e0297b7cba9a502412c05` |

The workspace evidence file `l3.2-analysis.json` adds complete row audits,
denominator/reason sets, original raw-file digests and differences formed within
each paired repetition before summarization. Its SHA256 is
`9d3d6612bb54f643077755f8529bee1e14f9bbc473bea3343a10f67575d92fc8`.
Raw per-variant JSON files and logs retain every sample, including warmup;
the aggregate does not replace them.

The full-overlap, forward CAT21 probe produces the following exact values in
all five measured samples (zero dispersion for these structural quantities).
Fallback bytes include serialized v1 `getblocktxn` and `blocktxn` messages plus
their 24-byte headers; compact-block bytes are additional and held separately.

| Variant | Mempool residents / hits | Extra hits | Missing / roundtrips | Charged dynamic bytes | Allocated slot bytes | Fallback wire bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Core default | 128 / 128 | 0 | 0 / 0 | 0 | 0 | 0 |
| Roots default | 0 / 0 | 128 | 0 / 0 | 45,056 | 1,572,864 | 0 |
| Roots count100 | 0 / 0 | 100 | 28 / 1 | 35,200 | 4,800 | 3,866 |
| Roots disabled | 0 / 0 | 0 | 128 / 1 | 0 | 0 | 17,266 |
| Roots count1 | 0 / 0 | 1 | 127 / 1 | 352 | 48 | 17,132 |
| Roots bytes10k | 0 / 0 | 28 | 100 / 1 | 9,856 | 9,984 | 13,514 |
| Roots count8-bytes1k | 0 / 0 | 2 | 126 / 1 | 704 | 384 | 16,998 |
| Roots CAT21 allowed | 128 / 128 | 0 | 0 / 0 | 0 | 0 | 0 |

Thus the default cache avoids one transaction-fallback roundtrip and 17,266
wire bytes against disabled caching in this fixture; count 100 leaves one
roundtrip and 3,866 bytes. Default versus count 100 allocates 1,568,064 more
slot bytes and retains 9,856 more charged bytes at this observation point.
The slot allocation is separate from the charged-byte budget. Neither quantity
includes transaction objects/control blocks or proves incremental RSS. The A7
control instead admits the same 128 transactions to the mempool; its zero extra
charge is not evidence of zero transaction memory or equal relay behavior.

All seven full-offer forward classes are covered below. Triplets are in the
order **Roots default / count100 / disabled**. These counts and wire totals
also agree in all five measured repetitions. Residents are measured before
the block; for replacements, the block contains the removed originals. Orphan
blocks additionally contain 128 unoffered parents, so they have 256 transactions.

| Class | Peer TX offers | Residents | Cache hits | Requested missing | Fallback wire bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Accepted | 128 | 128 / 128 / 128 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| CAT21 rejects | 128 | 0 / 0 / 0 | 128 / 100 / 0 | 0 / 28 / 128 | 0 / 3,866 / 17,266 |
| Replaced originals | 256 | 128 / 128 / 128 | 128 / 100 / 0 | 0 / 28 / 128 | 0 / 3,866 / 17,266 |
| Orphans | 128 | 0 / 0 / 0 | 128 / 100 / 0 | 128 / 156 / 256 | 17,266 / 21,018 / 34,422 |
| Mixed sizes | 128 | 0 / 0 / 0 | 128 / 100 / 0 | 0 / 28 / 128 | 0 / 17,110 / 77,810 |
| Four-offer churn | 512 | 0 / 0 / 0 | 128 / 25 / 0 | 0 / 103 / 128 | 0 / 13,916 / 17,266 |
| Unique adversarial rejects | 512 | 0 / 0 / 0 | 128 / 25 / 0 | 0 / 103 / 128 | 0 / 21,229 / 26,354 |

Every nonzero missing count above triggers exactly one getblocktxn roundtrip;
accepted ordinary transactions reconstruct from 128 mempool hits. Orphan
snapshots contain 128 distinct cached child objects with 45,056 unique dynamic
bytes and 128 external references under defaults. That demonstrates shared
retention, without identifying the owners or attributing unique RSS. Network
deduplication means these fixtures do not create duplicate cache references to
one object; L2 separately establishes the raw-insertion double-charge contract.

### Absolute instrumented costs and dispersion

The following values describe the full-overlap forward CAT21 cell, with five
measured samples each. Each entry is **median [min–max]; sample SD**. Store CPU
is the mean per successful store within a run, then summarized across runs;
it includes initial vector allocation and overwrites, not a steady-state-only
insertion cost. Disabled caching has no successful-store denominator.

| Roots configuration | CPU per successful store, microseconds | InitData CPU, microseconds | Accepted-tip wall time, milliseconds |
| --- | --- | --- | --- |
| Default | 4.816 [4.166–4.972]; 0.361 | 33.973 [33.764–34.645]; 0.344 | 51.268 [51.053–51.489]; 0.168 |
| Count100 | 0.593 [0.579–0.802]; 0.097 | 34.214 [33.242–35.186]; 0.762 | 102.524 [102.158–102.660]; 0.196 |
| Disabled | Undefined: zero stores | 27.882 [27.532–30.628]; 1.265 | 103.271 [102.555–103.312]; 0.387 |

Default's total insertion thread CPU has median 616,503 ns
[533,266–636,389], SD 46,222 ns; count100 has 75,909 ns
[74,068–102,599], SD 12,396 ns. Disabled attempts still consume measured timer/
function work: 69,966 ns [69,410–76,615], SD 3,089 ns. All three receive 128
offers and reach 128 insertion calls, separating zero stores from zero work.
The whole-process load CPU medians are 10/0/10 ms, with ranges 0–10/0–10/0–20 ms
respectively. The host exposes 100 CPU clock ticks per second (10 ms resolution),
so these process measurements cannot isolate small policy-filter CPU costs.

| Roots configuration | Steady process RSS, MiB | Sampled phase peak RSS, MiB |
| --- | --- | --- |
| Default | 55.324 [55.246–55.984]; 0.307 | 55.738 [55.621–56.547]; 0.388 |
| Count100 | 53.719 [53.645–54.543]; 0.377 | 54.234 [54.090–55.055]; 0.396 |
| Disabled | 53.625 [53.379–53.852]; 0.177 | 54.133 [53.953–54.367]; 0.156 |

These RSS values include the whole node, allocator, logging and mempool state;
the observed differences are not unique cache-memory measurements. Full raw
rows also retain baseline RSS, sampled-peak counts, lifetime VmHWM, distinct
charges, external references, thread wall times and process CPU deltas for
every variant/cell. The paired audit retains all corresponding differences.

At half offered overlap, defaults and count100 both reconstruct 64 transactions
from extras and request 64. Under the warmed instrumentation, default InitData
CPU is 893.697 microseconds [807.075–940.886], SD 53.469, while count100 is
25.047 [21.370–36.168], SD 6.207. Count100 minus default formed within each
pair has median −871.305 microseconds [−915.839 to −785.705], SD 53.940.
This is a bounded observation of the allocated-vector scan with missing
transactions, not a production latency estimate or evidence that the same
workload frequency occurs on mainnet. Forward/reverse and other cells remain
separate in the summary; no tail percentile is claimed from five samples.

The approximately 51/102 ms accepted-tip wall measurements also include the
framework's 50 ms P2P wait polling interval and the extra ping synchronization
after fallback. They do not estimate a production network roundtrip. Measured
wire and roundtrip savings are exact for these messages; extrapolated latency,
unique RSS savings, representative rates and a default recommendation remain
unsupported. Retain the current defaults and independent operator controls;
the data exposes a synthetic retention/slot/scan tradeoff without resolving
the real-world choice.

### Final per-modification dispositions and remaining measurement boundary

The source inventory and existing characterization remain applicable. The
following final recommendations preserve every optional mode; an unanswered cost
question does not establish benefit, harm or redundancy. Each row's source,
operator purpose, cost mechanism and existing test references remain in the
[modification inventory](#modification-inventory). Those purposes describe the
control an operator receives, rather than a demonstrated resource saving. The
table below adds the measured disposition and its unresolved cost boundary.
CAT21 measurements do not supply answers for the other policy classifiers.
No implementation retirement or default change is proposed by this review.

| Row | Final recommendation from available evidence | Bounded unanswered question |
| --- | --- | --- |
| A1 | Retain initialized grammar and error contracts. | No startup parsing benchmark: CLI/config/JSON/confrw precedence, malformed combinations and parse CPU remain unmeasured. |
| A2 | Retain configurable relay floor; ordinary fixtures pay above both defaults. | No isolated 1000/100/0 sat/kvB floor or ordinary-parent versus TRUC sponsorship pair; population changes cannot be called cache cost. |
| A3 | Retain script bounds and linkage repair. | Mixed output sizes do not isolate `maxscriptsize`; output, prevout, redeem-script and witness boundary timings under alternate limits remain unmeasured. |
| A4 | Retain strict/legacy sigop controls and arithmetic guards. | No strict 20/0 or legacy 2500/alternate pair, nor strict × legacy × equivalent-size traversal comparison. |
| A5 | Retain both optional bare-output permissions. | Bare pubkey and multisig 0/1 controls were not exercised independently; classification and excluded-population costs remain unanswered. |
| A6 | Retain the unknown-witness output control and inherited input rule. | No output-version 1/0 permission pair or separate unknown-version spend measurement. |
| A7 | Retain CAT21 default and disable control; this is the only independent policy pair. | Synthetic population/reconstruction effects do not establish real-world prevalence, resource savings or operator benefit; filter-only CPU is not isolated. |
| A8 | Retain opt-in token classifier. | The unique-reject workload is not a token-recognition matrix; `rejecttokens=0/1`, each supported pattern and recognition/false-negative costs remain unmeasured. |
| A9 | Retain ephemeral and bare-anchor/carrier modes. | No independent anchor/send/dust token, bare-anchor or bare-carrier switch matrix with standardness/package prerequisites held fixed. |
| A10 | Retain actual aggregate carrier budget/multiple-output contract. | Mixed scripts also carry CAT21 markers; they do not isolate exact-83/one-over, carrier 0/1, alternate budget or multiple-output classification costs. |
| A11 | Retain full-count and nonstandard-carrier controls. | No independent `datacarrierfullcount=1/0` or `acceptnonstddatacarrier=0/1` pair for each input/witness/output pattern. |
| A12 | Retain extra-weight accounting and rounding contract. | No carrier-cost 1/0/fractional/above-1 pair at identical accepted population; graph weight, trimming and template-order costs remain unanswered. |
| A13 | Retain optional subdust penalty and actual-fee separation. | No penalty 1/0 pair with partial/multiple deficits, exemptions, operator delta and replacement/package interactions. |
| A14 | Retain saturation and atomic graph-budget repairs. | These small fixtures do not approach fee/weight representability boundaries or isolate mutation overhead and conservative rejection costs. |
| A15 | Retain synchronized lifecycle repair and both opt-in dust estimators. | Dynamic dust remains off; target-block and mempool-kvB modes, scheduler phase, estimator/floor paths and lock contention have no matched timing data. |
| A16 | Retain deficit-excluding persistence repair. | Persistence is disabled in this experiment; dump/load bytes and CPU, repricing after a dust change and previous-release compatibility remain unmeasured. |
| A17 | Retain configurable RBF and native economic gates. | Real default-Always replacements exercise retained originals, but Never/OptIn, signaling/v3 alternatives and mode-matched churn costs remain unmeasured. |
| A18 | Retain Reject/Accept/Enforce TRUC modes. | Ordinary version-2 fixtures do not measure v3 packages, sibling eviction, reorg admission or the RBF × TRUC mode matrix. |
| A19 | Retain disabled-default maturity/coin-block controls. | No zero/nonzero independent gate pair, per-input scaling, exact-boundary or reorg cost measurement. |
| A20 | Retain reuse allow/conflict modes and staging/collision repairs. | Reuse remains allow; claim-count/script-length scaling, conflict lookups and index allocation versus allow have no independent control. |
| A21 | Retain explicit index accounting within the inherited mempool budget. | No small-`maxmempool` trimming run, equal-population allow/conflict accounting pair or unique allocator attribution; cache charge is not mempool usage. |
| A22 | Retain mining accounting/reward repairs and template preferences. | Template RPC is setup, not a miner benchmark; priority 100000/0, nonempty residual-graph scaling and fixed weight/size/minfee controls remain unmeasured. |
| A23 | Retain effective-policy reporting interface. | RPC setup/observation time is not isolated JSON/lock/polling cost; no concurrent reporting control was run. |
| A24 | Retain empty-default reason/test seam. | No independent ignore-set/string-allocation cost measurement; nonempty test sets remain unavailable to production operators. |
| C1 | Retain defaults while comparing count controls. | Slot/retention/reconstruction tradeoffs are synthetic; a default change needs representative input evidence and an uninstrumented latency control. |
| C2 | Retain independent charged-byte semantics and overflow/allocation repair. | The small-budget sweep measures pressure, not every exact native threshold; L2 assertions supply boundary contracts, not allocator savings. |
| C3 | Retain common per-entry exclusion repair. | All benchmark entries are below the 100000-byte exclusion; nearest-threshold and replacement behavior comes from L2 characterization, without a threshold timing comparison. |
| C4 | Retain inherited reconstruction algorithm. | Hit/miss wire outcomes are observable, but warmed InitData timings do not isolate production lookup cost; unrelated full-cache zero hits, collisions and network-delay distributions remain unmeasured. |
| C5 | Simplify operator wording to charged storage and shared ownership; retain budget semantics. Implemented in help, feature documentation and generated manuals/config. | Distinct native charges and orphan sharing are observable, but shared-pointer counts do not identify owners or establish unique incremental cache RSS/allocator release. |
| F1 | Retain fee-filter default and alternate profile/control. | No filter 1/0 or low/high incremental-fee profile pair, quantization/message-frequency audit or received-population cost measurement; cache tests do not answer these. |

## Independent resource budgets

The reconstruction extras are only one owner of transaction data. Disabling
them does not disable the inherited orphanage, transaction-download bookkeeping,
reject filters or accepted mempool. These structures also exist in the exact
Core comparator and have separate purposes and accounting:

| Structure and current source | Budget and lifetime | Consequence for this review |
| --- | --- | --- |
| Extra reconstruction references: [insertion](../src/net_processing.cpp), [option parsing](../src/node/peerman_args.cpp) | Default count 32,768 and 10,000,000 charged dynamic bytes. Slots have a separate bound of `min(count, byte_budget / sizeof(slot))`; dynamic charges exclude transaction objects/control blocks and slot storage. Ring overwrite and byte pressure drop cache references; either zero control disables stores. | This is independent of `maxmempool`, orphan weight and filter capacity. Shared owners can keep allocations alive after eviction; charged bytes are not unique process memory. |
| Missing-input orphanage: [interface/defaults](../src/node/txorphanage.h), [accounting/eviction](../src/node/txorphanage.cpp) | Weight is a memory-work proxy, not allocator bytes. Reserved usage is 404,000 weight units per tracked orphanage peer; global allowance is that value times `max(tracked_peer_count, 1)`. The separate global latency score defaults to 3,000, accounting for announcements and inputs. Global pressure triggers eviction from over-budget peers; peer/block resolution also removes announcements. | No fixed 10 MB or `maxmempool` allowance applies here. Global transaction weight is deduplicated; per-peer usage counts each announcer's transaction. An orphan and an extra entry can share the same transaction allocation while having different charges and lifetimes. |
| Recent-reject and reconsiderable-reject filters: [download manager](../src/node/txdownloadman_impl.h) | Two lazily allocated rolling Bloom filters, each configured for 120,000 elements and false-positive probability 0.000001; both reset on a chain-tip change. They store probabilistic hash membership, including package hashes where appropriate, rather than transaction bodies. | Suppressing repeated download/validation is distinct from retaining reconstruction data. Turning off extras does not eliminate these filters, their false-positive behavior or download-manager state. These parameters are not a measured cache-memory saving. |
| Accepted mempool: [options](../src/kernel/mempool_options.h), [parsing](../src/node/mempool_args.cpp), [accounting/trim](../src/txmempool.cpp) | `maxmempool` defaults to 300 decimal MB; blocksonly startup supplies 5 MB when not explicitly overridden. Native graph/container estimates include Roots script-claim indexes, and native trimming/expiry removes accepted entries. This is an accounting budget, not process RSS. | Admission, replacement and eviction operate on accepted transactions with Roots policy fees/weights. Extra-cache charges are not added to `getmempoolinfo` usage, and changing the extra budget does not resize this budget. |

The orphanage's tracked-peer count is its own accounting population, not an
assumption that every connected peer has an orphan. Its latency score and weight
proxy do not describe the extra vector's recursive dynamic-byte charge. Summing
these estimates or shared ownership counts would not produce total process RSS.
The experiment restarts each cell to clear these states; it is not an independent
orphanage/filter/mempool capacity or adversarial steady-state benchmark.

## Final correction and verification scope

The confirmed correction is operator wording for C2/C5. The candidate help now
identifies decimal MB, charged dynamic storage, separate slot/object/control-block
overhead, shared lifetime, independence from `maxmempool`, and both zero-valued
disable controls. The [feature contract](roots-features.md#peer-controls-and-current-connection-health)
describes the accounting equation. No parsing, default, cache insertion,
eligibility, policy acceptance or consensus behavior changed, so no affected
benchmark pair needs recollection. No code removal or redesign follows from the
synthetic timing results.

Candidate daemon and Qt help generated all seven manuals and the example config
twice with the repository generators, isolated disposable regtest wrappers,
offscreen Qt and a fixed `SOURCE_DATE_EPOCH`. All eight outputs matched across
runs. Only the two cache descriptions changed in `bitcoind.1`, `bitcoin-qt.1`
and `bitcoin.conf`; ports, other defaults and profile-dependent options were
unchanged. No older binary/help input or manual edit was used. The installed
Linux Qt 6.8.2 profile has wallet/QR/DBus enabled, IPC/ZMQ/USDT disabled;
the headless candidate build was expanded for Qt help, without installing a
dependency. The release identity is the existing `v31.1-roots.2` setting;
no version text was normalized. Qt's C-to-UTF-8 locale advisory did not affect
repeatability.

The final gate covers full native units, cache/compact reconstruction, orphan
ownership, initialized policy parsing, replacement/package/TRUC/cluster behavior,
maintenance and applicable static/documentation checks. These provide regression
assurance around shared startup/help code; the final change has no policy
implementation delta. Run from the repository root with fresh disposable
directories, after all candidate builds have finished:

```sh
build-issue56/bin/test_bitcoin --report_level=detailed -- \
  -testdatadir=/tmp/issue56-final-native
python3 build-issue56/test/functional/test_runner.py \
  p2p_compactblocks.py p2p_compactblocks_extratxs.py \
  p2p_orphan_handling.py rpc_orphans.py feature_policy_args.py feature_rbf.py \
  mempool_rbf_policy.py mempool_package_rbf.py mempool_truc.py \
  mempool_cluster.py mempool_graph_policy.py \
  --jobs=2 --tmpdir=/tmp/issue56-final-functional
python3 -m unittest discover -s ci/test
python3 contrib/roots/test_cache_bench.py
python3 test/lint/check-doc.py
python3 test/lint/lint-tests.py
python3 test/lint/lint-python.py
python3 test/lint/lint-includes.py
git diff --check
```

The final local run passed 780 native cases plus one case with an external-script
assets warning; six disabled helper cases were skipped. All 26,769,724 assertions
passed. All eleven selected functional scripts, 216 maintenance tests and seven
harness contract tests passed. The data/document audit rechecked all 576 raw
rows, 96 statistics groups, 183 displayed metric cells and all 30 dispositions
without changing the original collection receipts. Argument-documentation,
test-suite naming and whitespace checks passed. Python lint exited after skipping
because `lief` is absent. Include lint failed on the same three unchanged
baseline findings: duplicate `QTimer` in `src/qt/test/wallettests.cpp` and the
Boost multiprecision includes in `src/test/fuzz/tx_pool.cpp` and
`src/test/roots_policy_oracle_tests.cpp`. These failures are retained, not fixed
through unrelated edits or counted as passing lint.

This remains local Linux evidence. External script assets, optional/hosted
platforms, new sanitizer/fuzz campaigns and production/mainnet performance are
not established. The existing missing-`lief` Python-lint skip and three unchanged
include-lint findings must remain explicit rather than being reported as passes.
Independent milestone acceptance and the terminal whole-branch review remain
separate gates; this document does not assert either has already occurred.
