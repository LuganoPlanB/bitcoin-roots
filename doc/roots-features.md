# Bitcoin Roots v31 preservation contracts

This unpublished candidate starts directly from authenticated Core v31.1.
[The port review](roots-v31-port-review.md) pins the exact source and new base,
accounts for every source commit, and records thematic ownership and gaps.

## Availability in this candidate

The foundation carries the contract documents and CMake support for selecting
Qt 5/6 and optional system LevelDB/libsecp256k1. Embedded libraries remain the
default. Native crypto probes, descriptor-wallet/SQLite boundaries, external
signer support and QR/DBus/IPC build conditions use the v31 implementations.
These upstream build conditions are not evidence that a missing optional
library or platform was tested. See [portability profiles](roots-build-portability.md).

The source contracts below are preservation requirements, **not a claim that
Roots behavior has been ported**. L7 implements scoped parsing and local transaction checks. L8.1 adds native graph
accounting and replacement/TRUC/reuse controls; L8.2 repairs dynamic dust lifecycle.
Priority/template
mining to L9; peer controls/health to L10; sweep, coin-control and per-send
RBF extensions to L11; resource safeguards to L12.2. L13.1 carries Roots
identity/icons, font-aware QR-PNG export and conditional Windows taskbar
progress. L13.2 carries Git-native source/export/signing safeguards and the
fast PR/development versus nightly/manual verification split; hosted/platform
coverage remains subject to the explicitly recorded gaps. L11.1 adds database-discovery/write-access safeguards, Unix signer descriptor
closure, wallet-derived per-send RBF defaults and prepared-transaction/PSBT
signaling. L11.2 adds transient-key sweep with cancellation/final authorization,
shared UTXO scanning and wallet-derived coin-control facts. L12.1 implements
read/write config and initialized effective-policy reporting, preserves native
monotonic uptime, and retains statistics model/schema source dormant.
Production does not expose `getmempoolstats` or collect samples.

The narrow monotonic `uptime` RPC sub-aspect has reviewed upstream
equivalence; this does not retire the remaining operator-stats contract.

MiniUPnPc dependency discovery is retained, but its UPnP runtime and depends
recipe integration belong to the peer theme. The legacy `ENABLE_TOR_SUBPROCESS` declaration has no dedicated
launcher consumer; it is not an available feature. Windows taskbar progress is enabled by `WITH_TASKBAR_PROGRESS` only for
Windows GUI builds. Its runtime remains an untested platform gap locally.
Passing an undeclared Tor-subprocess option to CMake does not implement it. No intended behavior is retired by a
missing dependency.

Unless explicitly marked as candidate evidence, code and test references in the
contract body identify paths at pinned source
`10d772eb7f4b00b5193428a0dde4c06558d6c297`; they are rendered as source
references rather than links into this candidate when their implementation
has not been carried. Historical test results are not current candidate passes.

## Preservation contracts

These stable IDs describe behavior to retain when moving between Core versions.
The source checked for this collection is Roots
`17e1d484aba2a881be03b8928fba3c0125e027a2`, based on authenticated Core v30.3
commit `49faec4f87f5cd19c88db01a82e5c68b087c8227`. Source and test links are
version-specific aids; neither a function name nor the location of an admission
check is a preservation requirement. The contracts do not authorize a change in
consensus or a new feature. The user explicitly authorized the bounded L8
dynamic-dust startup/lifetime, missing-estimate fallback and safe-scaling repair;
other documented gaps remain preservation limitations.

**Acceptance rule for every ID:** preserve its observable results, defaults,
supported configuration, units, errors and boundaries. Tests named below are
acceptance evidence for the cases they actually exercise, not proof of all
configurations. A **gap** names an unproved case or an implementation/documentation
conflict. Existing non-features and the wallet authorization boundary still apply.

**Retirement rule for every ID:** remove a Roots implementation only after a
reviewed upstream-equivalence comparison demonstrates the same behavior and
defaults for the listed acceptance cases and dependencies. Re-run the tests on
the new base and account for remaining gaps. A similar upstream option name,
RPC or feature is insufficient. Maintenance may retire when the new base makes
it unnecessary and its relevant validation still passes. Dormant support cannot
be represented as an adopted working feature; enabling it requires a separate
compatibility decision.

### Local policy and mining

The L7 candidate implements `policy-parsing`, `policy-standardness`, carrier
recognition/budgets within `policy-carrier`, fixed thresholds and ephemeral
exceptions within `policy-dust`, and relay gates within `policy-coinage`.
Focused candidate acceptance passed in
[`policy_args_tests.cpp`](../src/test/policy_args_tests.cpp),
[`transaction_tests.cpp`](../src/test/transaction_tests.cpp),
[`feature_policy_args.py`](../test/functional/feature_policy_args.py),
[`mempool_datacarrier.py`](../test/functional/mempool_datacarrier.py),
[`mempool_dust.py`](../test/functional/mempool_dust.py),
[`mempool_sigoplimit.py`](../test/functional/mempool_sigoplimit.py), and
[`mempool_roots_local_policy.py`](../test/functional/mempool_roots_local_policy.py).
The last test covers enabled/disabled relay age, unconfirmed age, exact
coin-block boundaries, extreme configured bounds, local filter/ephemeral
switches and consensus-valid blocks for CAT21 and relay-age rejections.
L7.2 adds tip-reorg age checks, carrier full-count/standardness independence,
and consensus-valid blocks for local marker, pubkey, witness-version, token,
multiple-OP_RETURN, dust and relay-age rejections. The complete native unit
suite and applicable package/replacement/TRUC/cluster/mining functional suites
are candidate verification gates, with explicit local allowances in resource
fixtures. The v31
[`policy_args` fuzz target](../src/test/fuzz/policy_args.cpp) retains L5's grammar
oracles; [`transaction`](../src/test/fuzz/transaction.cpp) also exercises carrier
recognition. Bounded Clang 19 ASan/UBSan/libFuzzer campaigns executed these and
native coins-view policy callers with reproducible seeds. Final L7 review remains
pending; a bounded campaign is not exhaustive coverage.
Source acceptance and gaps below remain historical evidence; L8 owns carrier
extra weight, dynamic dust, subdust, replacement/TRUC and reuse, and L9 owns
mining. Explicitly supplied staged options warn that they are not enforced.

- **policy-parsing:** the [policy input compatibility contract](feature-contracts/policy-parsing.md)
  specifies scoped grammar, effective defaults, diagnostics, migration decisions
  and baseline characterization for Roots-added policy controls. Parser and
  isolated startup regressions enforce the scoped strict behavior; inherited
  Core option parsing remains outside that scope.
- **effective-policy:** the [effective mempool policy contract](feature-contracts/effective-policy.md)
  defines an additive `getmempoolinfo.policy` object, initialized-state provenance,
  units, enforcement gates and compatibility. RPC functional assertions cover
  defaults, overrides, disabled settings, precedence and carrier/sub-dust
  reporting/admission parity without changing local policy or consensus rules.
- **policy-standardness:** configurable local transaction, input and witness
  bounds preserve rejection reasons without changing script consensus. Defaults
  include 1,650-byte script policy, 20 equivalent bytes per sigop and 20 strict
  bytes per sigop, 2,500 legacy sigops, bare pubkey/multisig disabled, unknown
  witness outputs allowed, CAT21 locktime-marker rejection enabled and token
  filters disabled. Ephemeral defaults permit anchors, but not arbitrary dust
  or sends; bare anchors are allowed. See policy settings (`src/policy/settings.h`, source reference),
  standardness options and constants (`src/policy/policy.h`, source reference),
  checks (`src/policy/policy.cpp`, source reference) and admission (`src/validation.cpp`, source reference).
  Acceptance: mempool_sigoplimit.py (`test/functional/mempool_sigoplimit.py`, source reference)
  `test_sigops_limit`, `test_sigops_package`, `test_legacy_sigops_stdness` and
  transaction_tests.cpp (`src/test/transaction_tests.cpp`, source reference). Gap: a complete
  option/ephemeral/filter interaction matrix and explicit valid-block cases for
  each local rejection are not supplied by these tests.
- **policy-carrier:** default `datacarrier=1`, aggregate carrier budget 83 bytes,
  full counting enabled, nonstandard carriers and bare-carrier transactions
  disabled. Recognized input/witness carriers also count with full counting;
  permitting nonstandard transactions alone does not disable these checks.
  `datacarriercost` defaults to 1 virtual byte per detected data byte, accepts
  fixed-point values with two decimal places from 0 through 1,073,741,823.75,
  and rounds the requested cost upward to integral weight units (quarters of a
  vbyte). Extra weight only raises policy size. See
  startup parsing (`src/init.cpp`, source reference), carrier detection (`src/policy/policy.cpp`, source reference)
  and admission (`src/validation.cpp`, source reference). Acceptance:
  mempool_datacarrier.py (`test/functional/mempool_datacarrier.py`, source reference) `run_test`
  covers budget boundaries, disabling, extreme cost/invalid inputs and valid
  blocks containing locally rejected transactions;
  mempool_tests.cpp (`src/test/mempool_tests.cpp`, source reference) `MempoolExtraWeightBoundary`
  covers size arithmetic. Conflict: current help says multiple outputs are
  allowed, but standardness rejects more than one OP_RETURN output as
  `multi-op-return`. Preserve the actual rejection until explicitly decided;
  help wording is not authority to widen admission. `test_taproot_carrier` in
  `mempool_datacarrier.py` independently checks a maximum-size push/OP_DROP
  tapscript: default nonstandard-carrier rejection, aggregate budget rejection,
  admission at the exact carrier budget when permitted, and valid-block acceptance.
  mempool_subdust_fee_penalty.py (`test/functional/mempool_subdust_fee_penalty.py`, source reference)
  `test_carrier_cost_interaction` covers output-carrier fractional cost rounding,
  zero cost, full-count disabling and composition with the dust deficit.
  Gap: other recognized input/witness carrier patterns and their fractional
  cost/full-count interactions.
  The inherited feature_taproot.py (`test/functional/feature_taproot.py`, source reference)
  fixture `tapscript/pushmaxlimit` expects admission under that test's explicit
  nonstandard-carrier allowance and enlarged budget, subject to its existing
  witness-size checks. The incorrect unconditional nonstandard marker was
  removed to match this configuration; the oversized push still fails consensus.
- **policy-dust:** thresholds use the effective dust relay rate; the floor is
  3,000 sat/kvB (`dustrelayfee=0.00003000` BTC/kvB). `dustdynamic=off` is the
  default. Dynamic mode is `[multiplier*]target:blocks` or
  `[multiplier*]mempool:kvB`; multiplier defaults to 3, is stored in thousandths
  and must be positive. Target is at least two blocks and no more than the
  estimator's longest tracked target; mempool position is at least 1 kvB.
  Dynamic estimates are multiplied, converted to integral sat/kvB and clamped
  to the configured floor. The scheduler updates the rate every 15 minutes;
  missing estimates fall back to the floor. The published rate
  and standardness snapshot must agree under concurrent access. See
  parser (`src/node/mempool_args.cpp`, source reference), publication (`src/txmempool.cpp`, source reference) and
  threshold calculation (`src/policy/policy.cpp`, source reference). Acceptance:
  mempool_dust.py (`test/functional/mempool_dust.py`, source reference) `test_dust_output` and
  `test_dustrelay` cover script-dependent thresholds, exact threshold/one below,
  and zero-rate behavior; mempool_tests.cpp (`src/test/mempool_tests.cpp`, source reference)
  `DynamicDustFeePublication` covers changing snapshots. Gap: estimator-target
  mode without an estimator is accepted by the parser; it is not evidence of a
  working estimate. Extreme dynamic multiplier arithmetic needs separate coverage.
  The source also aborts non-off startup because scheduler wiring is missing.
  L8's explicitly authorized repair must add node-owned scheduling after successful
  initialization, safe retry/shutdown lifetime, unavailable-estimator floor fallback
  and overflow-safe scaling, with regression coverage. L7 carries syntax only;
  this repaired runtime is not yet implemented.
- **policy-subdust:** apply a configurable deficit penalty to modified fees,
  using script-specific dust thresholds, without changing the actual fee or
  consensus validity. Enabled by default. The
  [worked contract](/doc/feature-contracts/subdust.md) defines units, rounding,
  saturation, exemptions, precedence, interactions, acceptance cases and gaps.
- **policy-replacement:** default full RBF (`mempoolreplacement=fee,-optin`)
  and enforced TRUC (`mempooltruc=enforce`). `Never` disables all replacement;
  `OptIn` replaces directly signaling originals or v3 originals, including
  non-signaling v3; full RBF still requires fee/resource replacement checks.
  TRUC can instead be rejected or accepted without enforcing TRUC-specific
  limits. `getmempoolinfo.fullrbf` means configured full RBF, so false does not
  mean v3 replacements are disabled. Explicit `optin` wins over
  `mempoolfullrbf=1`; explicit replacement disabling also wins and warns on
  that contradiction. See selection (`src/node/mempool_args.cpp`, source reference),
  admission/replacement (`src/validation.cpp`, source reference) and RPC (`src/rpc/mempool.cpp`, source reference).
  Acceptance: mempool_rbf_policy.py (`test/functional/mempool_rbf_policy.py`, source reference)
  `POLICIES`/`check_replacement` and
  mempool_truc.py (`test/functional/mempool_truc.py`, source reference) `test_truc_replacement`,
  `test_truc_reorg`, `test_minrelay_in_package_combos`, plus package/sibling
  cases. Gap: all TRUC-mode/replacement-mode combinations and conflicting option
  inputs. Current initialized incremental relay fee is 100 sat/kvB, while
  startup help advertises 1,000; this is a recorded conflict, not a promised
  change to the effective default.
- **policy-coinage:** minimum relay maturity and coin-block age both default to
  zero (disabled). Maturity counts depth at the next block height; coin-block
  age sums input satoshis times blocks since each coin's height. Unconfirmed
  inputs add no confirmed age. These are additional local gates, never changes
  to coinbase consensus maturity. See coin age (`src/policy/coin_age_priority.cpp`, source reference)
  and admission (`src/validation.cpp`, source reference). Acceptance: priority accounting cases in
  miner_tests.cpp (`src/test/miner_tests.cpp`, source reference) `CreateNewBlock_validity` cover the
  shared age cache. Historical source gap: dedicated enabled/disabled relay
  maturity/coin-block boundaries and valid-block fixtures. The candidate's
  `mempool_roots_local_policy.py` supplies those cases and tip-reorg checks;
  it does not port L9's priority cache or mining interactions.
- **policy-reuse:** `spkreuse=allow` defaults to no extra restriction. The
  documented alternative `conflict` uses mempool scriptPubKey creation/spend
  tracking to reject reuse or consider policy conflicts for replacement. This is a local
  script rule, not a privacy score, address-ownership inference or consensus
  prohibition. See mode selection (`src/init.cpp`, source reference), admission (`src/validation.cpp`, source reference)
  and tracking (`src/txmempool.cpp`, source reference). Gap: no dedicated functional acceptance
  test for these modes, reorg tracking and replacement interactions is identified;
  generic mempool tests are not a substitute.
- **mining-template:** with neither size nor weight option specified, the
  effective local limit is 1,200,000 weight units with no additional size cap
  below the 4,000,000-byte protocol bound; minimum fee is 1,000 sat/kvB and
  priority area is 100,000 serialized bytes. An explicit size-only option leaves
  weight unrestricted up to consensus maximum, and a weight-only option leaves
  size unrestricted; setting both enforces both. Priority area can be zero. Transactions
  selected by priority and their fee-selected descendants must have accurate
  ancestor weight/sigop accounting and appear at most once in dependency order.
  See option normalization/assembly (`src/node/miner.cpp`, source reference) and
  defaults (`src/policy/policy.h`, source reference). Acceptance:
  miner_tests.cpp (`src/test/miner_tests.cpp`, source reference) `CreateNewBlock_validity` priority
  ancestor boundary cases and
  mining_prioritisetransaction.py (`test/functional/mining_prioritisetransaction.py`, source reference)
  `run_test`. `test_mining_and_block_acceptance` in
  mempool_subdust_fee_penalty.py (`test/functional/mempool_subdust_fee_penalty.py`, source reference)
  covers modified-fee selection with priority enabled/disabled, penalty
  disabling, unchanged actual fee rewards and a locally rejected valid-block
  fixture. Conflict: inherited help advertised a 300,000-byte size default, while
  assembler options (`src/node/miner.h`, source reference) leave size at the protocol maximum when
  unset; `policy-parsing` corrected that help while preserving the interaction.
  Candidate L9.1 uses parent-ready coin-age selection from the active UTXO set
  and a temporary native residual TxGraph after any priority prefix. Native
  policy weights/modified fees govern remaining chunks; actual fees fund rewards.
  [`miner_tests.cpp`](../src/test/miner_tests.cpp) covers source priority parents,
  shared descendants, dependency waiting, duplicate exclusion, tie ordering and
  exact remaining size/weight/fee boundaries. The new
  [`mining_roots_template.py`](../test/functional/mining_roots_template.py)
  checks enabled/disabled priority, size-only/weight-only/both limits, reserve
  clamping and acceptance of a larger valid block under a smaller local cap.
  Joint policy combinations and the full gate remain L9.2. Integrated `confrw`
  remains L12.


### Peer controls and current-connection health

- **peer-controls:** retain Core's P2P protocol and inherited orphan/upload/
  whitelist controls. Roots additionally bounds compact reconstruction extras
  by count (default 32,768) and charged transaction dynamic storage (10 decimal
  MB by default, independent of `maxmempool`), excluding oversized
  individual extras; provides `forceinbound` (implies `noban` and `download`) and explicit
  compact-filter permissions; permits subnet disconnect; and can disable
  outbound v1 on clearnet (`v2onlyclearnet`, default false). Default fee filters
  are enabled; quantization uses a 100 sat/kvB seed below 1,000 sat/kvB
  incremental fee, and a 1,000 sat/kvB seed otherwise. Bloom service is not globally advertised by default, with
  implicit loopback permission for inbound and manual connections when not
  explicitly disabled. Automatic outbound connections, including full-relay,
  block-relay-only, address-fetch, feeler and private-broadcast, receive no
  address-based whitelist permissions or implicit loopback Bloom permission.
  Outgoing whitelist ranges apply only to manual connections (`addnode` and
  `connect`); incoming grants retain their direction and Tor-bind boundaries.
  Retain inactivity
  limits, protected outbound/manual peers, bounded address scoring/cache behavior,
  duplicate manual-connection checks and block-fetch safeguards. Tor binds must
  not accidentally grant clearnet whitelist permission; SOCKS credentials must
  not be logged. Bare `whitelist` and `whitebind` grant
  `download,noban,mempool,relay`, subject to `whitelistrelay` and
  `whitelistforcerelay`. Address privileges require explicit `addr` or `all`:
  these bypass GETADDR response caching and the address-processing token bucket,
  while retaining message-size/validity limits, inbound-only GETADDR and one
  response per connection. Socket and range permissions merge additively.
  The RPC permissions list reports `forceinbound` for explicit grants and `all`;
  ordinary `noban` does not imply it. `forced_inbound` separately reports whether
  accepting this connection required freeing a slot, so it remains false when
  a privileged peer connects to an available slot. Eviction protections remain
  unchanged. `getblockfrompeer` requires an existing block header, as already
  established in the v31 port. A rejected duplicate request to the same peer
  preserves its in-flight tracking; requesting from a different peer replaces
  the tracked request. Existing prune-mode safeguards remain in force.
  See [networking](/src/net.cpp),
  [processing](/src/net_processing.cpp), [permissions](/src/net_permissions.cpp)
  and [proxy](/src/netbase.cpp). Acceptance:
  [p2p_compactblocks_extratxs.py](/test/functional/p2p_compactblocks_extratxs.py),
  [p2p_permissions.py](/test/functional/p2p_permissions.py),
  [p2p_disconnect_ban.py](/test/functional/p2p_disconnect_ban.py),
  [p2p_eviction.py](/test/functional/p2p_eviction.py),
  [p2p_filter.py](/test/functional/p2p_filter.py) and
  [p2p_invalid_messages.py](/test/functional/p2p_invalid_messages.py), each
  `run_test`. Candidate boundaries additionally use
  [p2p_roots_controls.py](/test/functional/p2p_roots_controls.py),
  [p2p_feefilter.py](/test/functional/p2p_feefilter.py) and
  [rpc_getblockfrompeer.py](/test/functional/rpc_getblockfrompeer.py).
  `blockreconstructionextratxnsize` uses decimal MB (1 MB = 1,000,000 bytes),
  with four decimal places (100-byte increments); negative values disable and
  malformed values retain the default, matching source behavior. Setting either
  `blockreconstructionextratxn=0` or `blockreconstructionextratxnsize=0` disables
  this extra cache without changing mempool policy or valid-block acceptance.
  The charge is the sum of `RecursiveDynamicUsage(*tx)` for occupied slots,
  using the native allocation estimate for transaction input/output vectors
  and their script/witness dynamic storage.
  Slot metadata, transaction objects and shared-pointer control blocks are
  separate overhead, so the setting is neither total cache memory nor process
  RSS. The allocated slot count is bounded by the smaller of the count limit
  and the byte budget divided by the native slot size; this is an additional
  bound, not a deduction from the charged transaction budget. References share
  transaction ownership with other holders, including the orphanage. Each
  occupied slot is charged even if it refers to the same object; eviction drops
  that slot's ownership and charge, while another owner can keep the allocation
  alive. Deallocation need not immediately reduce process RSS. See the
  [cache review](mempool-policy-review.md#cache-lifecycle-characterization) for
  the measured boundaries and limitations. Oversized replacements are excluded
  as well as rejects.
  Clearnet uses the source routable IPv4/IPv6 classification: loopback and
  non-routable addresses remain eligible for v1. Tests redirect routable targets
  through a local SOCKS server. Gap: exhaustive combinations and non-Linux
  platform measurements; current-connection CPU reporting belongs to peer-health.
- **peer-health:** sorting, selection and current connection facts use the
  existing peer table/detail pane. Missing state resets to unavailable, never
  another peer's stale values. Persistent table indexes resolve the current row
  after storage refresh. Node RPC adds last block announcement, forced-inbound
  state and optional CPU load in per mille of connection duration. CPU load is
  omitted when not measured or duration is non-positive; failed/backwards CPU-clock
  readings are ignored. Measurement uses one scalar per current connection, with
  no history retention or content/activity feed. See [peer RPC](../src/rpc/net.cpp),
  [peer table](../src/qt/peertablemodel.cpp) and
  [details](../src/qt/rpcconsole.cpp). Acceptance:
  [apptests](../src/qt/test/apptests.cpp) tests empty refresh, unavailable reset and
  an optional live regtest sorting/selection/refresh profile;
  [rpc_net.py](../test/functional/rpc_net.py) covers current RPC fields and
  zero/backwards duration. The deprecated `misbehavior_score` field is removed
  from RPC output/help and node statistics; internal discouragement and
  disconnection enforcement remains unchanged. Native v31 starting-height RPC
  deprecation is retained.
  The live GUI profile captures Peers table/detail/unavailable at 1280×800 and
  1920×1080 with 100%/150% application font scale. Gap: non-Linux visual/CPU
  measurements and exhaustive timing races.

### Wallet authorization, signing and spend controls

The v31.1-roots.3 candidate additionally provides the opt-in `exportaddresskey`
wallet RPC for one locally held P2PKH or native P2WPKH spending key. It requires
normal wallet authorization and returns only the leaf scalar and compression
flag. See [the wallet manual](wallet-privacy.md#export-a-spending-key-for-one-address)
for the exact encoding, supported wallets, secret-handling and ancestor-xpub
risks, and Quietproof's native-P2WPKH limitation. This addition is
not a claim of availability in an earlier published release.

- **wallet-foundation:** descriptor/SQLite wallets and optional external signing
  are inherited Core foundations. Roots rejects unwritable database directories,
  avoids recursively scanning node database directories and symlink/reparse
  points, and closes unrelated child descriptors for signer commands. Preserve
  normal wallet locking and error reporting; automatic anti-fee-sniping
  locktimes avoid the CAT21 marker reserved by local policy. Do not introduce another database,
  backup/flush promise or Berkeley DB build option. [See database](../src/wallet/db.cpp),
  [SQLite](../src/wallet/sqlite.cpp), [filesystem helpers](../src/util/fs_helpers.cpp)
  and signer [child invocation](../src/common/run_command.cpp). Acceptance:
  [wallet_multiwallet.py](../test/functional/wallet_multiwallet.py),
  [wallet_signer.py](../test/functional/wallet_signer.py) and
  [wallet_tests.cpp](../src/wallet/test/wallet_tests.cpp). Candidate evidence also
  checks inherited Unix descriptors, directory write
  probes, skipped node databases and symlink traversal. Unix signer closure keeps
  standard streams and the exec-error pipe, closes actual inherited descriptors
  even above a lowered soft limit, and fails before exec if safe closure fails.
  Linux uses `close_range` or raw `/proc/self/fd` enumeration; other Unix builds
  use native `closefrom` when available. Gap: non-Linux fallback runtime, Windows
  reparse-point/permission cases and inherited handle closure; the Unix
  close-descriptor implementation does not prove Windows handle isolation.
- **wallet-rbf:** the per-send GUI choice starts from the wallet's `walletrbf`
  preference (Core default true), explicitly overrides it for the prepared
  transaction, and shows its resulting signaling in confirmation and saved
  PSBT. It is a signaling choice, not a guarantee of network replacement. See
  [Send dialog](../src/qt/sendcoinsdialog.cpp) and
  [wallet model](../src/qt/walletmodel.cpp). Acceptance:
  [wallettests.cpp](../src/qt/test/wallettests.cpp) `walletTests` explicit on/off
  override and decoded PSBT sequence assertions. Rendering evidence is recorded
  in the wallet theme port review.
- **wallet-sweep:** only documented WIF/script forms, local-key descriptor
  destination in the selected wallet, no key import, preview by default and
  final authorization before broadcast. Destination eligibility requires native
  script satisfaction with available local keys, including encrypted keys:
  incomplete multisig is rejected, while a sufficient subset is accepted.
  Fee state is snapshotted under wallet
  locking; cancellation/invalidation can stop scanning or preview, while accepted
  broadcast submission completes. Reduce secret lifetime using secure storage,
  cleansing and console redaction without claiming complete host/toolkit erasure.
  The full operational contract is [wallet-privacy.md](wallet-privacy.md).
  See [RPC](../src/wallet/rpc/spend.cpp),
  [construction](../src/wallet/spend.cpp) and [dialog](../src/qt/sweepdialog.cpp). Acceptance:
  [wallet_sweepprivkeys.py](../test/functional/wallet_sweepprivkeys.py) `run_test`
  covers supported script forms, destination rejection, no import, stale/conflicting
  previews and broadcast; [wallettests.cpp](../src/qt/test/wallettests.cpp)
  `walletTests` covers secure cache and preview invalidation;
  [test_wallet_privacy_contract.py](../ci/test/test_wallet_privacy_contract.py)
  checks source structure of authorization/redaction boundaries. Gap: that static
  check is not a race proof; cancellation/authorization concurrency and GUI
  confirmation need runtime and visual evidence.
- **wallet-coin-control:** expose wallet facts, effective value, locked/reused/
  grouped outputs and send fee/change/RBF consequences. Locked outputs remain
  visible but unselectable; ordinary availability filters still exclude unsafe
  and immature outputs. Material wallet changes clear explicit selection.
  Conspicuously validate custom change; do not add automatic consolidation or
  anonymity claims. See [wallet-privacy.md](wallet-privacy.md),
  [coin control](../src/qt/coincontroldialog.cpp) and
  [wallet facts](../src/wallet/interfaces.cpp). Acceptance:
  [wallettests.cpp](../src/qt/test/wallettests.cpp) `walletTests` selected/locked
  and invalidation cases, [wallet_send.py](../test/functional/wallet_send.py) and
  [test_wallet_privacy_contract.py](../ci/test/test_wallet_privacy_contract.py)
  row-filter assertions. Gap: populated GUI and custom-change visual matrix.

### Runtime, configuration, portability and identity

- **runtime-config:** `confrw` defaults to network-datadir `bitcoin_rw.conf`.
  Preserve recognized settings and precedence: internal forced settings, CLI,
  read/write config, JSON settings, network config section, default config
  section, then built-in default. For ordinary scalar values, CLI uses its last
  value and config uses its first after negation processing; list/chain/network
  options have separate inherited rules. Typed getters and diagnostics must not
  leak secrets. See merging (`src/common/settings.cpp`, source reference),
  configuration (`src/common/config.cpp`, source reference) and access/writes (`src/common/args.cpp`, source reference).
  Acceptance: getarg_tests.cpp (`src/test/getarg_tests.cpp`, source reference) and
  feature_config_args.py (`test/functional/feature_config_args.py`, source reference) `run_test`.
  Scoped Roots policy grammar and its acceptance gates are specified by
  `policy-parsing`.
- **operator-stats:** `uptime` returns elapsed seconds from a monotonic clock,
  independent of adjusted wall-clock time; acceptance:
  rpc_uptime.py (`test/functional/rpc_uptime.py`, source reference) `run_test`.
  Retained statistics source defines `time_from`, `time_to`, and arrays of
  `[relative_seconds, tx_count, mempool_bytes, minimum_fee_sat_per_kB]`, without
  interpolation. The in-memory target is 10 MiB, cleanup is periodic rather
  than a hard allocation ceiling, and enabled collection would be controlled
  by a positive target. See stats model (`src/stats/stats.cpp`, source reference),
  schema (`src/stats/rpc_stats.cpp`, source reference), startup declarations (`src/stats/init.cpp`, source reference)
  and monotonic source (`src/common/system.cpp`, source reference). Gap/conflict: production does
  not call registration, option initialization or sample collection at the
  pinned source. `stats_tests/stats` in
  stats_tests.cpp (`src/stats/test/stats_tests.cpp`, source reference) exercises the model directly,
  not an available RPC. GUI-on/headless-off help wording also conflicts with
  the model's default-disabled constant. Wiring and schema/default decisions
  must be reviewed before claiming preservation of a working statistics service.
- **runtime-resources:** advisory file allocation preserves existing bytes;
  FD-limit results and GiB conversion avoid overflow. Native RAM detection and
  warning thresholds are retained. v31 uses a 1 GiB default cache on 64-bit
  hosts with at least 4 GiB detected RAM, and 450 MiB otherwise (also when RAM
  is unknown); source used a fixed 450 MiB default. Explicit `dbcache` values
  retain checked MiB conversion, a 4 MiB minimum and a 1 GiB 32-bit cap.
  See [cache sizing](/src/node/caches.cpp), [FS helpers](/src/util/fs_helpers.cpp),
  [byte units](/src/util/byte_units.h) and
  [port review](/doc/roots-v31-port-review.md#l122-runtime-resource-adaptation).
  Source memory-pressure, I/O-priority and sequential/cache-discard helpers have
  no production callers and are not added as active services. Local Linux
  tests do not establish execution guarantees for other platform branches.
- **build-portability:** retain supported CMake switches and actual dependency
  probes, Qt 5/6 branches, QR/signer/taskbar conditions, native crypto flags,
  and usable system LevelDB memory environment. Defaults include wallet/tests
  enabled, GUI disabled, embedded LevelDB/secp256k1. IPC can be deliberately
  disabled, with no IPC coverage claim. See CMakeLists.txt (`CMakeLists.txt`, source reference),
  build guides (`doc/build-unix.md`, source reference), dependencies (`doc/dependencies.md`, source reference) and
  LevelDB probe (`cmake/module/FindLevelDB.cmake`, source reference). Acceptance: configure/build
  each supported profile and its registered `ctest` targets; build-contract
  checks in ci/test (`ci/test`, source reference). Gap: the baseline covers one Linux headless
  wallet profile, not all optional/platform configurations. `ENABLE_TOR_SUBPROCESS`
  has no production consumer and cannot promise an automatic Tor instance.
- **identity-desktop:** built tools identify Bitcoin Roots `v31.1-roots.3`
  by default; `CLIENT_VERSION_TAG` accepts only `v31.1-roots.<positive revision>`
  and sets archive-build identity without Git metadata. Numeric protocol version
  and BIP14 `Satoshi` identity remain upstream-compatible. Retain Roots icons,
  Bitcoin Core/Knots lineage and attribution, font-aware QR-to-PNG export and
  supported Windows taskbar progress without mass-rebranding history. See
  [version](/src/clientversion.cpp), [QR export](/src/qt/qrimagewidget.cpp) and
  [taskbar](/src/qt/wintaskbarprogress.cpp). Acceptance: production CMake release
  version cases in [test_identity.py](/test/cmake/test_identity.py), QR/icon/PNG
  clipboard assertions in [apptests.cpp](/src/qt/test/apptests.cpp), actual
  receive Save action/PNG parity/font refresh in
  [wallettests.cpp](/src/qt/test/wallettests.cpp), binary `-version`, and
  supported Qt/platform builds. See the [L13.1 evidence](/doc/roots-v31-port-review.md#l131-identity-and-desktop-integration)
  for the receive/export resolution/font matrix and platform/profile gaps.

### Maintenance dispositions

These IDs account for source work without treating it as operator behavior.

- **maintenance-ci:** classifiers, test fixtures, sanitizer/corpus scheduling,
  platform checks and reuse of proven promotion CI. Acceptance:
  `python3 -m unittest discover -s ci/test`, relevant lint and actual required
  candidate CI profiles. Equivalent infrastructure must test the exact candidate;
  a successful older workflow/build cannot retire the gate.
- **maintenance-release:** immutable source/tag/asset validation, portable
  export/replay with `.github` exclusions and `git am -3 --keep-cr`, contributor
  credit, five-platform archives, canonical COPYING, signature trust and
  new-draft-only behavior. See maintainer workflow (`contrib/roots/README.md`, source reference)
  and release tools (`ci/release/README.md`, source reference); acceptance: their tests under
  ci/test (`ci/test`, source reference) and temporary export/replay. Optional notice tools do not
  prove complete dependency copyright coverage. Publishing, tagging and remote
  canonical updates are outside these contracts' implementation work.
- **maintenance-docs:** catalog/provenance/privacy/guidance, release notes and
  generated manuals/example config follow the actual source/help. Acceptance:
  check-doc.py (`test/lint/check-doc.py`, source reference), local links, `git diff --check` and
  regenerated-output comparison when interfaces change. Preserve upstream names
  where they identify lineage rather than Roots UI.
- **maintenance-native:** behavior-preserving C++ modernization and test/fuzz
  fixture adaptation, safe empty file writes and test-only assertion hooks.
  Acceptance: affected unit/functional/fuzz suites and diff inspection. Reverted
  transient script changes do not create a consensus contract or require a port.

### Dependencies by stable ID

The shared consensus-neutral and configuration rules apply to every contract.
This table records additional feature dependencies; identifiers remain separate
even when implementations share a commit or file.

| Contract | Depends on |
| --- | --- |
| policy-standardness | None |
| policy-carrier | policy-standardness |
| policy-dust | policy-standardness |
| policy-subdust | policy-dust |
| policy-replacement | policy-standardness |
| policy-coinage | None |
| policy-reuse | policy-standardness |
| mining-template | policy-coinage, policy-carrier, policy-replacement |
| policy-parsing | runtime-config, policy-standardness, policy-carrier, policy-dust, policy-subdust, policy-replacement, policy-coinage, policy-reuse, mining-template |
| effective-policy | policy-parsing, runtime-config, policy-standardness, policy-carrier, policy-dust, policy-subdust, policy-replacement, policy-coinage, policy-reuse |
| peer-controls | policy-replacement |
| peer-health | peer-controls |
| wallet-foundation | build-portability |
| wallet-rbf | wallet-foundation, policy-replacement |
| wallet-sweep | wallet-foundation, wallet-rbf, runtime-config |
| wallet-coin-control | wallet-foundation, wallet-rbf |
| operator-stats | runtime-config |
| runtime-resources | build-portability |
| runtime-config | None |
| build-portability | None |
| identity-desktop | build-portability |
| maintenance-ci | None |
| maintenance-release | None |
| maintenance-docs | None |
| maintenance-native | None |

## Consensus and provenance boundary

Bitcoin Roots remains compatible with Bitcoin Core consensus. It does **not**
enforce RDTS/BIP110 consensus rules. Policy, relay, mempool, and mining-template
configuration must not make a consensus-valid block invalid.

Bitcoin Core v30.3 is the direct base. Bitcoin Knots history is useful only as
provenance for selected policy research; it is not imported as an upstream
dependency. The historical replay candidate and archived maintenance branch are
evidence for maintainers, not product specifications.

## Deliberate non-features

Roots does not ship or promise:

- RDTS/BIP110 consensus enforcement;
- Network Watch, block/transaction feeds, or block/mempool visualizers;
- Tor "pairing" credentials, proxy credentials, or control-port secrets;
- Tor endpoint sharing or a dedicated endpoint-sharing UI;
- CoinJoin, payjoin, silent automatic consolidation, a privacy score,
  anonymity guarantee, address graph, chain-analysis dashboard, or payment
  history redesign;
- the candidate's broad Knots common-maintenance, consensus/script, wallet, or
  documentation overlays merely because they occurred in historical source;
- the discarded replay, frozen-state, generated-evidence, or promotion control
  plane; or
- a VitePress documentation overlay, tonal/base16 font dependency, or other
  separate visual identity layer.

For maintainers, `contrib/roots/README.md` (`contrib/roots/README.md`, source reference) documents
the Git-native maintenance workflow. For current build and runtime options, use
the generated manuals and the built binary help.
