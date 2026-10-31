# Roots Core v32.1 port review

## Exact inputs and authentication

Input observation: 2026-10-31. The unpublished port branch `topic/32.1/port`
starts directly at Bitcoin Core v32.1 commit
`f0de87f040621468b956f01f526a43a010310501`. Old Roots history is source
material for a new thematic series; it must not be merged into this base.

The official repository is <https://github.com/bitcoin/bitcoin.git>. Its
advertised `refs/tags/v32.1` object is
`94f59fa45ea443882655625f4a647608274453e2`, peeling to
`f0de87f040621468b956f01f526a43a010310501`. Both match the local tag and the
prepared branch's HEAD. Core v31.1 tag object
`bfa6a4b79cd4c1562fd32857e3147763efae37fb` peels to the previous base
`9be056a8a72b624dae9623b2f7bded92c2a21c91`.

**Upstream tag signature verification was explicitly waived by the user.**
It is not a passing signature check; no upstream keys were imported. Official
repository identity, tag object, peeled commit and ancestry remain required.
This waiver does not apply to new Roots commit signatures or later release
artifact verification.

Publication is independently established by the [official release announcement](https://bitcoincore.org/en/releases/32.1/)
and [GitHub release](https://github.com/bitcoin/bitcoin/releases/tag/v32.1):
release ID `426769835`, `draft=false`, `prerelease=false`, published
`2026-10-28T14:23:53Z`. The announcement distinguishes v32.1 from v32.0,
which was tagged but never fully announced or released.

### Selected Roots source and ref refresh

Roots origin remains `git@github.com:luganoplanb/bitcoin-roots`. The host's SSH
configuration permissions prevented the initial read. Read-only HTTPS access to
the same repository succeeded without changing origin or host configuration.
Fetching only `main` and `roots/31.1` into their existing remote-tracking refs
found no movement from the planning snapshot:

| Input | Planning and refreshed commit | Disposition |
| --- | --- | --- |
| `origin/roots/31.1`, selected canonical source | `e93a8cca047c12ce9cab97181dd30705ed0e7e83` | Carry all released changes through roots.6. No newer canonical changes observed. |
| `origin/main` | `5d09cb937f0024a6fae0b4ba3da7f0ae7fd0063f` | Inventory separately; preserve useful website/tooling in main integration. |
| main's integrated Roots anchor | `34b672ffe2049050068409113783b44b3116b9dc` | roots.2; later canonical fixes remain required. |
| local `roots/31.1` | `83ec87511a52074a1dc95d8c03c5ada4c71c61cc` | Stale; excluded as a source selector, left untouched. |

All six remote Roots tag objects and peeled commits match local tags:

| Tag | Tag object | Peeled commit |
| --- | --- | --- |
| `v31.1-roots.1` | `6cd32ddf7effb622802701b29c79d2cf33d5cdbb` | `7cd11e298ec99e9bfc497f0fe3b9a18ab48a61a7` |
| `v31.1-roots.2` | `d6bcaa0a53f984d32a37b1008d026b7ce41c6ac4` | `34b672ffe2049050068409113783b44b3116b9dc` |
| `v31.1-roots.3` | `aca0c15393a6e83781f1c883e3364e1899138b60` | `59185c317ef80ef289b46c38ba82081b61a1d101` |
| `v31.1-roots.4` | `c80165c9def05f24ce5874327202b3045e414058` | `2fe385d7f51a2a7e40a778fc3a5c6e580499fbb0` |
| `v31.1-roots.5` | `b08459ad72294b201e3b17365e7c5ebcfc143f4f` | `93348f8530aadce4562aef40272b1e0af3e3478b` |
| `v31.1-roots.6` | `0ce3cd96401d3fc593c881350f855957887a7f2c` | `e93a8cca047c12ce9cab97181dd30705ed0e7e83` |

Ancestry checks establish the append-only chain Core v31.1 → roots.1 → roots.2
→ roots.3 → roots.4 → roots.5 → roots.6; roots.2 is also an ancestor of main.
Core v31.1 is not an ancestor of v32.1: these release branches diverged.
This is an expected upstream ancestry distinction, not missing Roots history;
the new series uses the exact official v32.1 base. Source baseline
worktree `bitcoin-roots-release6` remains at the selected roots.6 commit.
The port worktree was clean at input verification. Existing stale/prunable
worktree metadata was observed but not modified. The authorized signing-rule
edit in the separate main checkout's `AGENTS.md` is preserved.

### New Roots commit signing

The shared repository-local configuration resolves in the port worktree to
`user.signingkey=5EADD53F2CD1F0B7AEEE920D25FC5C29CD528E32`,
`commit.gpgsign=true`, `gpg.format=openpgp`. Every new Roots commit, including
merges, cherry-picks, patch imports and temporary replay commits, must use this
key (Denis Roio, `jaromil@dyne.org`) and have its signature checked. Preserve
original authorship and credits. No unsigned fallback is permitted. Configuration
verification does not prove that a future signing operation will succeed.

### Input verification scope

Focused checks compare official/local tag objects and peeled commits, all six
published Roots tag identities, source ref movements, ancestry, branch/worktree
identity and signing configuration. They establish source provenance only.
No product behavior, native build, hosted CI or release artifact pass is claimed
at this input boundary. Complete source/contract accounting and the maintenance
baseline follow before this review unit is accepted.

## Inventory interpretation and acceptance owners

The inventory covers **38 commits** in
`9be056a8a72b624dae9623b2f7bded92c2a21c91..e93a8cca047c12ce9cab97181dd30705ed0e7e83`,
**25 stable IDs** in that tip's `doc/roots-features.md`, its additional
address-export prose contract, and **148 net main-only paths** in
`34b672ffe2049050068409113783b44b3116b9dc..5d09cb937f0024a6fae0b4ba3da7f0ae7fd0063f`.
Ranges here identify trees or source provenance, not an executable replay plan.
No implementation or behavioral acceptance is claimed by a planned disposition.

Owner labels refer to the v32 port work: L2 foundation; L3 local policy;
L4 graph/replacement/dynamic dust; L5 mining; L6 networking/cache; L7 wallet
backend/export; L8 Qt6; L9 runtime/configuration; L10 maintenance/release/docs;
L11 whole-candidate audit/compatibility; L12 canonical/release handoff;
L13 separate main integration. Split ownership means each owner must preserve
the relevant part; it does not authorize another writer during this review unit.
All owners inherit consensus-valid-block acceptance, native interface retention,
full applicable suites, signed commits and the absence of release authority.

`Fold/adapt` means retain the final observable source behavior while using
current v32 interfaces and incorporating its follow-up fixes. `Retain native`
identifies inspected upstream behavior to keep; it is not a claim that new-base
behavioral equivalence has already passed. Historical evidence, old platform
gaps and the v31 reduced-verification waiver cannot satisfy v32 acceptance.
No stable contract is dropped. Historical revision labels and generated help
are superseded by candidate-specific identity and newly generated help in L10;
historical notes and published assets stay intact.

### Stable contract dispositions

Source IDs S01–S38 resolve to the complete commit table below. Tests named here
are acceptance obligations, normally carried/adapted from the selected roots.6
source. A source filename is not a claim that the pristine v32 tree contains it.
The exact source catalog and its linked grammar/subdust/effective-policy
contracts remain the authority for defaults, units, errors and dependency edges.

| Stable ID | Planned disposition and source | Owner and observable acceptance |
| --- | --- | --- |
| `policy-parsing` | Fold/adapt S02, S11; whole-token/scoped grammar, ranges, overflow, quarter-vbyte rounding, redacted diagnostics. | L3.1; `policy_args_tests.cpp`, `feature_policy_args.py`, `policy_args` fuzz and independent `roots_policy_oracle_tests.cpp`; L4.2 instrumented arithmetic. |
| `effective-policy` | Fold/adapt S07, S02–S04; report initialized applied settings, enforcement gates and units, not reparsed guesses. | L9.1 with L3–L5; `rpc_effective_policy.py`, defaults/overrides/disabled/precedence and actual admission parity; native v32 RPC schema tests. |
| `policy-standardness` | Fold/adapt S02; retain local 1650-byte script/20-byte sigop limits, 2500 legacy sigops, bare-pubkey/multisig/CAT21/token/witness/ephemeral settings and exact reasons. | L3.1–L3.2; `transaction_tests.cpp`, `mempool_sigoplimit.py`, `mempool_roots_local_policy.py`; paired local rejection versus consensus-valid block cases. |
| `policy-carrier` | Fold/adapt S02–S03, S11; final 83-byte/full-count behavior, 1 vbyte/data byte default, fractional rounding, input/witness recognition and extra policy weight. Preserve actual multi-OP_RETURN rejection; resolve misleading old help in L10. | L3/L4; `mempool_datacarrier.py`, `mempool_subdust_fee_penalty.py`, `feature_taproot.py`, `mempool_tests.cpp`; independent arithmetic and valid-block side. |
| `policy-dust` | Fold/adapt S02–S03, S11; 3000 sat/kvB floor, dynamic off by default, initialized scheduler ownership, retry/shutdown safety, missing-estimate floor fallback and atomic snapshots. Old text describing broken pre-repair startup is historical. | L3 fixed thresholds; L4.2 dynamic lifecycle; `mempool_dust.py`, `mempool_dynamic_dust.py`, native publication tests, bounded `dynamic_dust_fee` sanitizer/fuzz. |
| `policy-subdust` | Fold/adapt S03, S11; enabled deficit penalty affects modified fees only; preserve independent decimal/multiprecision oracles and signed/char boundaries. | L4 with L3/L5; `mempool_subdust_fee_penalty.py`, native policy oracles, carrier composition and block validity. |
| `policy-replacement` | Fold/adapt S03; full-RBF fee/non-opt-in default, Never/OptIn and v3/TRUC semantics, native graph package invariants. Adapt deprecated RPC fields deliberately. | L4.1; `mempool_rbf_policy.py`, `mempool_truc.py`, package/RBF/cluster native and functional suites, fee-delta and ancestor cases. |
| `policy-coinage` | Fold/adapt S02, S04; relay maturity/coin-block age default disabled, next-height/reorg semantics and overflow-safe UTXO arithmetic. | L3 admission/L5 mining; `mempool_roots_local_policy.py`, `miner_tests.cpp`, exact boundary and valid-block cases. |
| `policy-reuse` | Fold/adapt S03; `spkreuse=allow` default and configured conflict tracking across mempool changes/replacement. | L4.1; `mempool_rbf_policy.py`, affected package/graph native and functional suites; no inference from a renamed upstream structure. |
| `mining-template` | Fold/adapt S04; 1200000-weight/default priority-area, fee/size/resource controls, dependency ordering, residual native chunks and accounting. | L5.1–L5.2; `miner_tests.cpp`, `mining_roots_template.py`, `mining_prioritisetransaction.py`, full GBT/available IPC suites and valid-block tests. |
| `peer-controls` | Fold/adapt S05, S17–S19, S28–S30, S33–S34; final count default **100**, 10 decimal MB charged dynamic-storage cap, permissions and current transport privacy. | L6.1–L6.2; permission/addrrelay/getaddr/eviction/Bloom/duplicate-fetch tests, `net_peer_connection_tests.cpp`, compact extra unit/functional tests and producer benchmark smokes. |
| `peer-health` | Fold/adapt S05, S19; current-connection facts/reset/selection refresh, unavailable-state clearing, no obsolete `misbehavior_score`. | L6 node API/L8.2 GUI; `rpc_net.py`, `rpc_getblockfrompeer.py`, `apptests.cpp`, sorting/persistent-index/per-peer state tests and rendered peer views. |
| `wallet-foundation` | Fold/adapt S06, S17; descriptor/SQLite boundaries, write-access/directory/symlink guards, signer inherited-FD closure even below lowered soft limits, local unlock and anti-CAT21 locktimes. | L7.1; `wallet_tests.cpp`, `wallet_multiwallet.py`, `wallet_signer.py`, native DB/process tests and old-wallet fixtures where available. |
| `wallet-rbf` | Fold/adapt S06; wallet-derived per-send preference and actual prepared transaction/PSBT signaling. v32 PSBT v2 default remains native. | L7.1 backend/L8.1 GUI; `wallettests.cpp`, wallet send/PSBT suites, both RBF states and versions 0/2. |
| `wallet-sweep` | Fold/adapt S06; transient WIF keys never imported, local descriptor destination must satisfy signing, preview default, final exact-txid authorization, atomic cancellation/submission. | L7.1/L8.1; `wallet_sweepprivkeys.py`, `wallettests.cpp`, `test_wallet_privacy_contract.py`, incomplete/sufficient multisig/Taproot, locked/watch-only, stale-preview and failure cleanup. |
| `wallet-coin-control` | Fold/adapt S06; wallet facts, effective value, grouped/reused/locked outputs and notification invalidation; locked rows visible but unselectable. | L7.1 interface/L8.1 GUI; `wallet_send.py`, `wallettests.cpp`, privacy contract fixtures, same-balance notifications and rendered layouts. |
| `runtime-config` | Fold/adapt S07; forced→CLI→read/write config→JSON→network→default precedence and native scalar/negation rules; complete-read, EOF, rewind/comment, atomic write/reset semantics. | L9.1; `getarg_tests.cpp`, `feature_config_args.py`, full read/write config tests, partial-read/write/close failures and redaction. |
| `operator-stats` | Retain native monotonic uptime; fold dormant support from S07/S17 without registration or sampling. Broader statistics remain deliberately unavailable. | L9.1–L9.2; `rpc_uptime.py`, native clock/source inspection; assert `getmempoolstats` unavailable. Do not enable dormant schema/model tests as a product feature. |
| `runtime-resources` | Fold/adapt S07/S09; allocation/extension preserves bytes, safe FD saturation/closure and GiB conversion; retain native RAM/cache defaults including new indexes. | L9.2; full filesystem/streams/system/resource tests, signed/unsigned extremes and bounded sanitizer checks; platform branches inspected separately. |
| `build-portability` | Fold/adapt S01, S13–S17; embedded defaults/system LevelDB+secp checks, filesystem/compiler bridges, QR/signer/IPC/taskbar/icon/MiniUPnPc conditions. Drop Qt5 and obsolete libevent dependency/header adaptation because v32 uses Qt6 and native HTTP. | L2.1–L2.2; clean installed-dependency headless/wallet/Qt6 profiles, `test_leveldb.py`, Homebrew fixtures where still applicable, native crypto probes and affected full suites. Actual unavailable platforms remain gaps. |
| `identity-desktop` | Fold/adapt S08/S09 and later identity revisions; v32 Roots archive tag validation, icons, font-aware QR-PNG export and conditional Windows taskbar. | L2 wiring/L8 GUI/L10 identity and help; `test_identity.py`, `apptests.cpp`, `wallettests.cpp`, fresh-process persistence and stated viewport/font matrix. Protocol IDs/embedded attributions stay unchanged. |
| `maintenance-ci` | Fold/adapt S08/S11/S17/S25 plus relevant main-only compatibility tooling; preserve cadence, full diagnostics, sanitizer/fuzz artifacts and exact-SHA normal CI. | L10.1; full `python3 -m unittest discover -s ci/test`, classifiers/failure transport fixtures and relevant lint; L11 whole candidate, L13 ordinary integration CI. |
| `maintenance-release` | Fold/adapt S08/S17/S21/S35; Git-native immutable source/tag/new-draft/export/parity, notice bytes, five-platform packaging and signature trust. | L10.2/L12; full release fixtures, signed disposable replay excluding `.github/**`, blob/mode/symlink parity. Permanent tags, dispatch, manifest/platform signing and publication require later authorization. |
| `maintenance-docs` | Fold/adapt source catalog/privacy/guidance/release notes, S10/S12/S20–S21/S23–S24/S27/S30–S32/S35–S38. Supersede release labels/generated help, preserve history. | L10.3; candidate-help regeneration twice, local links, metadata/docs fixtures and `git diff --check`; L11 final claims audit/L12 handoff/L13 website. |
| `maintenance-native` | Fold/adapt S08/S09/S11/S17 and source fixture repairs; keep needed behavior-preserving modernization, safe empty writes and test-only assertion hooks. | L2–L9 owners of affected code; native/functional/fuzz regressions and registration; L11 complete audit, not a blanket common-maintenance overlay. |

### Additional released address-export contract

The roots.6 catalog describes address export in prose rather than adding a
26th stable bullet ID. It is accounted here explicitly as **address-export
(supplemental)**, folded/adapted in L7.2 from S22, S25–S26 and S37, with
L8 secret-history/UI integration and L10 documentation. Carry opt-in
`exportaddresskey`, normal local-wallet authorization/unlock, destination type
comparison, canonical `keys` array (full identities/scalars/compression/roles),
P2PKH/P2WPKH aliases, recognized P2SH/P2WSH multisig/Miniscript wrappers,
Taproot internal/leaf/effective-output mode and local MuSig2 participants.
Partial ownership is accurately reported and never broadens export powers.
Preserve disabled/private-key-free/external-signer errors, key unavailability,
logs/history redaction and HD ancestor-xpub disclosure warnings; Quietproof's
native-P2WPKH limitation remains documented. Required evidence includes the
entire `wallet_exportaddresskey.py`, `scriptpubkeyman_tests.cpp`,
`wallet_rpc_tests.cpp`, `key_tests.cpp`, `rpc_tests.cpp` and Qt RPC secret-history
tests. Current Core `addhdkey`, `derivehdkey`, `exportwatchonlywallet` and new
key-provider/schema APIs are overlap to audit, not authorization to export more.

### Dependency and non-feature boundaries

L2 precedes compiled port work. L3 admission precedes L4 graph/dust; L3–L4
precede L5 mining and L7 wallet policy integration. L6 uses L2–L3 and L4–L5
outputs where relay touches policy. L8 consumes L6/L7 node interfaces; L9
consumes L3–L7 authoritative settings. L10 reconciles all previous themes,
L11 qualifies the complete tree, L12 pins its accepted final candidate, and
L13 integrates that candidate separately. `effective-policy` depends on parsing
and every reported policy; carrier extra-weight/subdust/replacement/mining
depend on consistent graph weights/modified fees; dust snapshots depend on
node lifetime/configuration; wallet GUI relies on backend authorization and
notification semantics. None can be retired independently by a symbol rename.

Preserve the catalog's deliberate non-features: no RDTS/BIP110; no Network
Watch/feeds/visualizers; no Tor pairing credentials, secret or endpoint-sharing
UI; no CoinJoin/payjoin/automatic consolidation/privacy score/anonymity
guarantee/chain-analysis dashboard/history redesign; no broad historical Knots
consensus/script/wallet/documentation overlays; no retired replay/frozen-state/
generated-evidence/promotion control plane. Dormant statistics and the unused
Tor-subprocess declaration remain dormant. Qt5 and old libevent dependency
patches are obsolete support, not lost runtime features. The historical catalog
excludes a VitePress overlay from the canonical product; the already existing
main website is separately retained under L13, without bringing its font/visual
dependencies into canonical node or Qt code. There is no unrelated wishlist
expansion or fixed commit quota.

## Current v32 overlap review

These observations inspect the exact pristine v32.1 files at
`f0de87f040621468b956f01f526a43a010310501`, compared with the selected v31
source. Line locators refer to that base and will move during implementation.
They justify adaptation boundaries; later owners still execute current tests.

| Area and base evidence | Consequence and acceptance owner |
| --- | --- |
| `doc/dependencies.md:13–42`, `src/qt/CMakeLists.txt:14,148`: CMake 3.22, GCC 12.1/Clang 17, Boost 1.74 and Qt >=6.2; targets use `Qt6::`. No libevent dependency row. | L2 retains supported probes/platform fixes and Qt6; old Qt5 branches/libevent system-header patches are superseded, without disabling a runtime feature. Existing host dependency availability remains unverified here. |
| `src/txgraph.h:138–154`, `src/node/miner.cpp:164,253–268`: native chunk/ancestor and policy-weight interfaces remain active. | L4/L5 must compose extra carrier weight, modified fees, replacement, priority-prefix ancestor removal and residual chunks with these APIs; a graph API is not policy equivalence. |
| `src/net_processing.cpp:502–545`: `InvToSendBucket` owns global backlog plus count/size token buckets (12 MB/600 s, 50 MB cap). | L6 adapts Roots relay controls without restoring old per-peer scheduling; L4/L5 supply consistent effective policy. Full relay/transport tests required. |
| `src/net.cpp:1978–1985,4294`, `src/net.h:721`: v1 reconnection carries the original node's proxy override through the reconnect queue. | L6 preserves upstream private-broadcast origin privacy on v2 failure, with the current transport regression profile. Old socket hooks cannot discard this field. |
| `src/net_processing.h:44`: native `DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN{100}`. | Retain native final count default; S34/roots.5 explicitly supersedes the earlier 32768 proposal. L6 still ports byte accounting, fixtures, locks and benchmark behavior; same constant alone does not establish full cache equivalence. |
| `src/httpserver.cpp:45–79,708`: native socket/select handling and HTTP thread pool replace the old libevent transport dependency; libevent mentions elsewhere explain analogous parsing behavior. | L9 adapts errors/limits/configuration to native HTTP; L2 drops obsolete dependency support, L10 updates current build/help guidance. Do not infer dependency use from explanatory comments. |
| `src/wallet/rpc/spend.cpp:1013,1053,1738`: RPC defaults/constructor use PSBT version 2 and accept explicit version selection. | L7/L8 retain this default and verify actual transaction sequences and PSBT versions 0/2 for per-send RBF and sweep. Old PSBT fixtures must be adapted. |
| `src/wallet/scriptpubkeyman.h:128–130,212–217,267`, `src/wallet/rpc/wallet.cpp:838,914,957,1142–1151`: native solving/signing/key-provider APIs and add/derive HD key/watch-only export RPCs. | L7.2 uses current authorization/provider boundaries; these RPCs do not replace address-specific authorized key export or justify additional disclosure. Check new RPC/OpenRPC schemas and all export error/ownership cases. |
| `src/rpc/mempool.cpp:346,441,520,627`: explicit adjusted/BIP141 size fields and deprecated `bip125` gates. | L4/L9 keep native fields truthful alongside Roots effective policy and replacement settings; verify deprecated/default RPC responses, not old unconditional fields. |
| `src/node/mining_args.cpp:30–99`, `src/node/miner.cpp:108–117`: current `BlockCreateOptions`, flattening and reserved-weight/sigops/consensus-limit checks. | L5 adapts Roots limits to native option validation and current daemon/IPC consumers, preserving upstream rejection semantics and consensus validity; IPC unavailable is a gap. |
| `src/node/caches.cpp:23–91`: 1 GiB default at >=4 GiB detected RAM on 64-bit, saturating MiB conversion, native txospender-index cache share. | L9 retains native RAM/default/index accounting; port only demonstrated missing resource safeguards and recheck index-specific boundaries. Historical cache text is not a new default instruction. |
| `src/common/system.cpp:122–125`, `src/rpc/server.cpp:184–198`: startup and uptime use `SteadyClock` and `GetUptime`. | Retain native implementation of the narrow uptime sub-contract; L9 runs `rpc_uptime.py`. Dormant broader statistics are not adopted or enabled. |
| `src/streams.cpp:95–112`: `AutoFile::write` validates the handle, then the non-obfuscated branch calls `fwrite` without an empty-span guard. | L9 preserves S09's safe-empty-write contract after null-handle validation and native position/obfuscation bookkeeping; absence of a guard is a source lead, with current regression/sanitizer evidence required. |

No Roots restriction is added to block consensus. Current Core consensus,
serialization, transport privacy and native wallet operations remain the base;
only necessary policy/resource/interface adaptations belong to their owners.

## Complete canonical commit accounting

Every source commit is listed once in chronological order, including documentation,
release preparation, tests and follow-up repairs. All 38 recorded authors are
Jaromil; preserve that authorship and the older upstream/devjoinedthechat/Plan-B
credits within the carried work. Owning contracts above specify acceptance
suites; L11 audits this entire table and range-diff again at the final candidate.

| ID | Source commit | Source subject | Disposition / acceptance owner |
| --- | --- | --- | --- |
| S01 | `6a60180521daedbc404caf23a8c4a77bcb739de7` | build(roots): establish v31 portability foundation | L2: Fold/adapt foundation, system-library checks and actual platform support; preserve native crypto/embedded defaults. |
| S02 | `e5445b7c96193c7ec141705aac5ba96651e0ee30` | feat(policy): preserve Roots parsing and local transaction checks | L3: Fold strict parser/local admission, consensus-valid block fixtures and configured defaults; do not copy obsolete validation signatures. |
| S03 | `846c9de12a5e71259dc1ed829413d68f449ebfd5` | feat(policy): preserve cluster accounting and repair dynamic dust | L4: Fold graph/modified-fee/replacement/reuse and repaired dynamic-dust scheduling, snapshots and safe scaling. |
| S04 | `0fbff64239358c69a266705b189770b0f5570ff3` | feat(mining): preserve Roots priority and template policies | L5: Fold priority prefix, ancestor/residual chunk accounting, template limits and native consensus validity. |
| S05 | `2cd5097c909d1ab2f8bf27eb0df97b24b6c0807e` | feat(net): preserve Roots peer controls and health view on v31 | L6/L8: Fold peer controls and current-connection APIs; GUI peer selection/reset belongs to L8. |
| S06 | `ab07fc5490af30ce7c6da1c137c8280d1f4815f0` | feat(wallet): preserve Roots signing and spend controls on v31 | L7/L8: Fold wallet DB/signer/sweep/coin-control/backend authorization and GUI RBF/preview/worker boundaries. |
| S07 | `44c15f2cde7d36f2733814d6ab68ce148f1a7079` | feat(rpc): preserve Roots operator interfaces on v31 | L9: Fold initialized effective-policy/configuration, resource guards and dormant statistics; retain native uptime. |
| S08 | `2f8bedaf1c56b3647746773492cb0d6c81a33e86` | chore(roots): integrate v31 identity and maintenance tooling | L8/L10: Fold identity/resources/QR/taskbar and Git-native maintenance/release tooling, attribution and fixture adaptation. |
| S09 | `8e7ffad5d932e07c0c80f3756f7c3a442786877e` | fix(roots): restore audited portability and runtime behavior | L8/L9: Fold display-unit startup/persistence ordering and GUI tests; safe empty AutoFile writes/stream sequencing and benchmark fixture correction. |
| S10 | `0c0d2f1bf2e70f85e93423b61d1acd9b0b327578` | docs(roots): record v31 contract preservation evidence | L10/L11: Retain historical preservation evidence as history; regenerate current v32 claims after actual gates, never reuse old pass labels. |
| S11 | `9aaf62146fae1a89d65121468633d5c1701699e9` | fix(test): bound policy oracles and retain CI diagnostics | L3/L4/L10: Fold independent bounded policy oracles including signed-limit/char repair, safe tx-pool fuzz fixtures and full CI diagnostics/artifact transport. |
| S12 | `03bac6b5da7409d16abf1edeaf116b1ffc763e28` | docs(release): prepare v31.1-roots.1 release notes | L10/L12: Retain roots.1 historical release notes; supersede active candidate identity/help and prepare new release handoff without publication. |
| S13 | `64bb4d9137f0b4691ce0c3204dae24c5271931cf` | fix(build): preserve Homebrew Boost system header classification | L2: Fold Homebrew Boost system-header classification where current imported targets require it; run maintained fixture. |
| S14 | `d959ae99ffe7bef0593c2aca6e797fe702b06c41` | fix(build): retain implicit Homebrew system header flags | L2: Fold implicit Homebrew system-header preservation; check current compiler/imported-target behavior. |
| S15 | `c07c02369c52d67365314dbf6c09f52bf34af8cb` | fix(build): classify Homebrew IPC dependency headers as system | L2: Fold Homebrew CapnProto/libmultiprocess system-header classification where current IPC dependencies require it. |
| S16 | `83ec87511a52074a1dc95d8c03c5ada4c71c61cc` | fix(build): classify Homebrew libevent headers as system | L2: Drop obsolete libevent-only header support because v32 has native HTTP/no libevent dependency; retain historical provenance. |
| S17 | `7cd11e298ec99e9bfc497f0fe3b9a18ab48a61a7` | fix(release): preserve notice bytes and integrate nightly fixes | L6/L7/L9/L10: Fold exact NOTICE bytes (.gitattributes), subprocess descriptor-type fix, dormant RPC nits, wallet fixture ownership and explicit setban test cleanup; retain follow-up regressions. |
| S18 | `e5137d7fc9bc3c31848aeac021adbd3a9fbb764b` | fix(net): restore explicit peer permission boundaries | L6: Fold explicit addr/all permission implications, additive grants, manual outbound bounds and addrrelay/getaddr/eviction regressions. |
| S19 | `f41d71d7d23f8f17d6be125435627da437459269` | refactor(rpc): remove obsolete peer misbehavior score | L6/L8: Fold removal of obsolete misbehavior_score with honest peer state/duplicate block-fetch tests; never reintroduce misleading RPC field. |
| S20 | `662cc3ebfc1ce9de2dfbdf705f19a682ef731ecc` | chore(release): prepare roots.2 P2P review candidate | L10: Retain roots.2 historical P2P release notes; supersede active identity/generated docs with v32 candidate. |
| S21 | `34b672ffe2049050068409113783b44b3116b9dc` | docs(release): define Folding across Core release cycles | L10/L12/L13: Retain Folding/append-only/new-Core-base maintenance policy; adapt current branch guidance, no fixed series quota. |
| S22 | `fad94e12f7b830e820c9f722a8545bffc809f2d5` | feat(wallet): add address-specific private key export | L7.2: Fold opt-in initial address-specific export with local wallet unlock/authorization; latest S37 generalization supersedes narrow implementation. |
| S23 | `3a74a4cd7859981e21ef074ad3cbdde9412c3158` | chore(release): prepare v31.1-roots.3 identity and notes | L10: Retain roots.3 historical notes; replace active candidate version metadata only. |
| S24 | `2b4c8982dd65e87947a085735bdf4455b63ac136` | docs(release): regenerate manuals for v31.1-roots.3 | L10: Supersede roots.3 generated manuals using fresh v32 candidate help, preserving historical source lineage. |
| S25 | `b8c98a129f141ac5ba077c33b473120f0cd2ece4` | test(wallet): include address key export in fast regressions | L7/L10: Fold exportaddresskey registration in fast regressions/classification; coverage must include later generalized export. |
| S26 | `59185c317ef80ef289b46c38ba82081b61a1d101` | fix(wallet): compare exported key destinations by type | L7.2: Fold destination type-aware exported-key comparison; preserve regression rather than string-only address matching. |
| S27 | `fe7cfe976edabc8c8d374b2bbe2bbaf2bef734c1` | docs: inventory mempool policy and cache review contracts | L3/L4/L6/L10: Retain mempool/cache review contract inventory, reconcile final defaults and actual upstream overlaps. |
| S28 | `bd5422a481aae345cf287bce1c8ab3dc296c20b7` | test: characterize compact reconstruction cache contracts | L6.2: Fold compact extra cache unit/functional characterization, test-only accessors and registered fixtures. |
| S29 | `e9ddb283303167331fb6f5e76fedbf95ffa2b272` | bench: measure compact reconstruction cache tradeoffs | L6.2: Fold producer cache benchmarks; distinguish production path from modeled metadata; no required performance gain. |
| S30 | `5fa358fbb7ce5e95be8f959888ae25dd70e3b1de` | docs: clarify compact reconstruction cache accounting | L6.2/L10: Fold corrected charged-dynamic-storage/cache accounting explanations; historical 32768 default superseded by S34. |
| S31 | `f6f9987732b9452b3b7883145de19ed9e8a840a7` | chore(release): prepare v31.1-roots.4 identity and notes | L10: Retain roots.4 historical notes; supersede active revision identity. |
| S32 | `7588cf44afc6bfbc06de67c0fb61f386ff16e13d` | docs(release): regenerate manuals for v31.1-roots.4 | L10: Supersede roots.4 generated help through current native generators; no manual edits. |
| S33 | `2fe385d7f51a2a7e40a778fc3a5c6e580499fbb0` | test: declare lock preconditions for cache transaction helper | L6.2: Fold explicit lock preconditions for cache transaction fixture helper and its callers. |
| S34 | `79ebaa31225897f47403a95e510654f9531cdc45` | fix(net): restore Core default for compact block extra transactions | L6.2: Retain final Core count default 100, explicit-override/startup regressions and corrected public help; do not resurrect 32768. |
| S35 | `6faec825074b25c6ece812ef93d57b0cc34fc5cd` | chore(legal): attribute Roots copyrights to Plan-B Foundation | L2/L8/L10: Fold Plan-B Foundation legal attribution across carried source/resources and generated metadata; preserve upstream/Jaromil/devjoinedthechat credits. |
| S36 | `93348f8530aadce4562aef40272b1e0af3e3478b` | chore(release): prepare v31.1-roots.5 draft | L10/L12: Retain roots.5 historical release notes and latest defaults; supersede active candidate metadata. |
| S37 | `add7e6aa8ffbdc39be3070b126662174834d1410` | feat(wallet): generalize address-key export on Roots 31.1 | L7.2/L8: Fold full generalized address export, descriptor/key helpers, provider/partial ownership rules, Taproot/MuSig2 support and all backend/RPC/Qt secret-history regressions. |
| S38 | `e93a8cca047c12ce9cab97181dd30705ed0e7e83` | chore(release): prepare v31.1-roots.6 candidate | L10/L12: Retain roots.6 historical notes and provenance; supersede active version/generated help and reconcile full final wallet privacy/export documentation. |

Earlier fixes remain attached to their owning behavior, including L9's native
stream edge cases and L8's startup-order persistence repair. The source's old
L7–L16 numbering is historical; the v32 owner labels above control this port.
S22/S26/S37 are cumulative, not alternative export implementations; S28–S34
are cumulative cache characterization plus final default correction.

## Main-only differences and separate integration

The exact roots.2-to-main tree difference contains 148 paths: 111 website paths,
16 mode-only changes and 21 other paths. There are no main-only production C++
or consensus changes in this net difference. The main graph retains much older
Roots/Knots history; a reachability log of roots.2..main would include that
history, so this inventory uses the exact net path/blob/mode/symlink difference
and path provenance. Old branch reachability is not additional product scope.

Each path below has a disposition class and the full source commit reported by
`git log -1 <pinned-main> -- <path>`. This locator identifies the last affecting
source change; the full pinned tree and its difference remain authoritative,
including earlier fixes. Classes supply owning milestones, behavior and tests:

| Class | Disposition, acceptance owner and tests |
| --- | --- |
| W | Retain/adapt in **L13** website integration only. Keep VitePress config/theme/design/content, documentation mirror symlinks, release catalogue/download links, major-grouped Folding patch browser, cache/renderers, static-HTML checks and publication workflow; preserve historical URLs/assets, qualify new 32.x navigation without fabricating release assets. Run `npm test` (docs prepare + all Node tests), available site build/static checks and ordinary CI. Browser checks and dependency availability are separate evidence; no installation or deployment is authorized. |
| M | Retain executable modes in **L13**, reconciling source registration with owning canonical L2/L3/L4/L5/L6/L7/L9/L10. These 16 paths have byte-identical blobs to roots.2 and only change 100644→100755. Do not invent a main-only behavior fix or overwrite later canonical file content. Verify blob/mode parity and relevant full maintenance/functional profiles. |
| D | Retain/adapt clangd compilation-database guidance, config and generated-metadata ignore rules in **L13**; reconcile current CMake paths and native flags with L2. Run docs/local-link/lint checks and inspect metadata exclusion; generated caches stay outside Git. |
| L | Retain/adapt symlink-aware executable/file handling and website-mirror lint exclusions in **L13**, without exempting product/build/tests/workflows from ordinary CI. Run relevant lint and website/maintenance fixtures. |
| C | Preserve compatibility harness, CI wrapper/workflow/source-tool regression calls and maintainer guidance. **L11.2** imports/adapts and actually qualifies v32 fixtures on both exact binaries; **L10** owns applicable canonical CI and **L13** retains main tooling. Existing generation guards intentionally qualify only **30.3** (`ci/compatibility.py:75,125,142`), not 31.1 or 32.1. Keep binary/source/serializer/block provenance, process termination/cleanup, disabled automatic maintenance, bidirectional valid/invalid cases, normalized UTXO checks, lifecycle evidence and artifacts. Run both compatibility source-tool suites and full maintenance; actual matching-binary qualification is a separate required L11 gate. |
| G | Retain/adapt contributor/readme/maintainer guidance in **L10/L13**, superseding obsolete active release/branch labels, retaining Folding, separate product/tooling parity, ordinary integration CI and immutable old releases. Merge the authorized current AGENTS signing rule into new guidance in L2; never overwrite it from pinned old source. Run documentation/privacy/release fixtures and local links. |

The main-only website privacy guide (`5d09cb937f0024a6fae0b4ba3da7f0ae7fd0063f`)
and README revision (`ebdc72c0cd`) are guidance, not permission to weaken node
privacy or imply a privacy guarantee. Existing main's compatibility follow-ups
include provenance/shutdown before fixture restarts, failed-cleanup rejection,
automatic-maintenance suppression, lifecycle evidence and UTXO artifacts;
retaining the harness files without these checks would be an omission.
The catalogue's historical canonical non-feature boundary and this explicit
main-only website retention are intentionally distinct.

### Exact main-only path accounting

Mode notation is Git tree mode: 100644 regular data, 100755 executable, 120000
symlink; 000000 denotes absence at roots.2. Symlink targets must be inspected
and retained/rebased as links during L13, not dereferenced into product copies.

| Path | Modes roots.2→main | Source commit | Class |
| --- | --- | --- | --- |
| `.clangd` | 000000→100644 | `789fe585b66eea7ea812f8129d3605ecb467380d` | D |
| `.github/workflows/ci.yml` | 100644→100644 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | C |
| `.github/workflows/deploy-docs.yml` | 000000→100644 | `15ea51185b9d891dc0797df72891cc52ddd552ff` | W |
| `.github/workflows/reusable-compatibility.yml` | 000000→100644 | `d3a7c61cc38195e44c25e2e445bd1dbf0b976656` | C |
| `.gitignore` | 100644→100644 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | D |
| `AGENTS.md` | 100644→100644 | `7e9fe109d2b3419918cb024c1f98282a86425947` | G |
| `CONTRIBUTING.md` | 100644→100644 | `db3606f0c0692d4f7286866af2af8c1ebf4ff997` | G |
| `README.md` | 100644→100644 | `ebdc72c0cd25603ddc89026faef9fc7bf5d6a538` | G |
| `ci/compatibility.py` | 000000→100755 | `78c585b98aa1e67405b11b26c3f7a0251e28e651` | C |
| `ci/test/roots-regressions.py` | 100644→100755 | `7e9fe109d2b3419918cb024c1f98282a86425947` | M |
| `ci/test/test_compatibility_ci.py` | 000000→100644 | `78c585b98aa1e67405b11b26c3f7a0251e28e651` | C |
| `ci/test/test_homebrew_boost_headers.py` | 100644→100755 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | M |
| `ci/test/test_homebrew_capnp_headers.py` | 100644→100755 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | M |
| `ci/test/test_homebrew_libevent_headers.py` | 100644→100755 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | M |
| `ci/test/test_roots_compatibility.py` | 000000→100755 | `78c585b98aa1e67405b11b26c3f7a0251e28e651` | C |
| `ci/test/test_roots_regressions.py` | 100644→100755 | `7e9fe109d2b3419918cb024c1f98282a86425947` | M |
| `ci/test/test_workflow_selection.py` | 100644→100755 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | M |
| `contrib/roots/README.md` | 100644→100644 | `db3606f0c0692d4f7286866af2af8c1ebf4ff997` | G |
| `contrib/roots/compatibility/README.md` | 000000→100644 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | C |
| `contrib/roots/compatibility/build.py` | 000000→100644 | `486de130e3a0a2dd00a5c369862d21af74bbb45f` | C |
| `contrib/roots/compatibility/compare.py` | 000000→100755 | `f4ad5b388077c657d7cdf827fbd4e4356910111f` | C |
| `contrib/roots/compatibility/fixtures.py` | 000000→100644 | `78c585b98aa1e67405b11b26c3f7a0251e28e651` | C |
| `doc/developer-notes.md` | 100644→100644 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | D |
| `doc/website/.gitignore` | 000000→100644 | `6050fc6833412a6e7085001bc395ae2d487c3ced` | W |
| `doc/website/.impeccable/config.json` | 000000→100644 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/.impeccable/design.json` | 000000→100644 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/.impeccable/surfaces/index-md.md` | 000000→100644 | `8e65013f6738516e618eb8766d6e0919b893c64a` | W |
| `doc/website/.vitepress/config.ts` | 000000→100644 | `5d09cb937f0024a6fae0b4ba3da7f0ae7fd0063f` | W |
| `doc/website/.vitepress/tests/patch-overview.test.mjs` | 000000→100644 | `db3606f0c0692d4f7286866af2af8c1ebf4ff997` | W |
| `doc/website/.vitepress/tests/patch-renderer.test.mjs` | 000000→100644 | `097549b6ebd124951f0f73e96c7581a122b2ce93` | W |
| `doc/website/.vitepress/tests/patch-series.test.mjs` | 000000→100644 | `6050fc6833412a6e7085001bc395ae2d487c3ced` | W |
| `doc/website/.vitepress/tests/patch-theme.test.mjs` | 000000→100644 | `097549b6ebd124951f0f73e96c7581a122b2ce93` | W |
| `doc/website/.vitepress/tests/prepare-releases.test.mjs` | 000000→100644 | `ce2be1b6651f5fe9a38256558ddbc2c5c36ac404` | W |
| `doc/website/.vitepress/tests/release-assets.test.mjs` | 000000→100644 | `ce2be1b6651f5fe9a38256558ddbc2c5c36ac404` | W |
| `doc/website/.vitepress/tests/release-catalogue.test.mjs` | 000000→100644 | `ce2be1b6651f5fe9a38256558ddbc2c5c36ac404` | W |
| `doc/website/.vitepress/tests/site.test.mjs` | 000000→100644 | `5d09cb937f0024a6fae0b4ba3da7f0ae7fd0063f` | W |
| `doc/website/.vitepress/tests/website-publication.test.mjs` | 000000→100644 | `15ea51185b9d891dc0797df72891cc52ddd552ff` | W |
| `doc/website/.vitepress/theme/components/ComparisonTable.vue` | 000000→100644 | `7a3bbfe62014184f07c0184c3966bd24eb55547f` | W |
| `doc/website/.vitepress/theme/components/DocumentationIndex.vue` | 000000→100644 | `cdd88e6c80f23db014163b8e6dccdfc7788cc196` | W |
| `doc/website/.vitepress/theme/components/Home.vue` | 000000→100644 | `ca2fa6b28f8c0bc4fb7bedb90d2ac15f7949d732` | W |
| `doc/website/.vitepress/theme/custom.css` | 000000→100644 | `ca2fa6b28f8c0bc4fb7bedb90d2ac15f7949d732` | W |
| `doc/website/.vitepress/theme/index.ts` | 000000→100644 | `cdd88e6c80f23db014163b8e6dccdfc7788cc196` | W |
| `doc/website/.vitepress/theme/patches.css` | 000000→100644 | `097549b6ebd124951f0f73e96c7581a122b2ce93` | W |
| `doc/website/DESIGN.md` | 000000→100644 | `cdd88e6c80f23db014163b8e6dccdfc7788cc196` | W |
| `doc/website/PRODUCT.md` | 000000→100644 | `cdd88e6c80f23db014163b8e6dccdfc7788cc196` | W |
| `doc/website/RELEASE-INPUTS.md` | 000000→100644 | `db3606f0c0692d4f7286866af2af8c1ebf4ff997` | W |
| `doc/website/content/CONTRIBUTING.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/INSTALL.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/README.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/SECURITY.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/ci` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/compare.md` | 000000→100644 | `8dcc41519a7df406e30834001d72f7c9e16bf539` | W |
| `doc/website/content/contrib` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/depends` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/CMakeLists.txt` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/Doxyfile.in` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/JSON-RPC-interface.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/README.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/README_doxygen.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/README_windows.txt` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/REST-interface.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/assets-attribution.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/assumeutxo.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/benchmarking.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/bips.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/bitcoin-conf.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/bitcoin_logo_doxygen.png` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/build-freebsd.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/build-netbsd.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/build-openbsd.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/build-osx.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/build-unix.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/build-windows-msvc.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/build-windows.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/cjdns.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/dependencies.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/descriptors.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/design` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/developer-notes.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/dnsseed-policy.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/external-signer.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/files.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/fuzzing.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/guix.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/i2p.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/init.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/man` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/managing-wallets.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/multiprocess.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/multisig-tutorial.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/offline-signing-tutorial.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/p2p-bad-ports.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/policy` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/productivity.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/psbt.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/reduce-memory.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/reduce-traffic.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/release-notes` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/release-notes.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/release-process.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/tor.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/tracing.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/translation_process.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/translation_strings_policy.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/doc/zmq.md` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/documentation.md` | 000000→100644 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/getting-started.md` | 000000→100644 | `3853ba692b4912eaacc007f824936af6892edfdf` | W |
| `doc/website/content/index.md` | 000000→100644 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/principles.md` | 000000→100644 | `cdd88e6c80f23db014163b8e6dccdfc7788cc196` | W |
| `doc/website/content/privacy.md` | 000000→100644 | `5d09cb937f0024a6fae0b4ba3da7f0ae7fd0063f` | W |
| `doc/website/content/public/bitcoin-roots.lottie.json` | 000000→100644 | `8e65013f6738516e618eb8766d6e0919b893c64a` | W |
| `doc/website/content/public/favicon.svg` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/share` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/src` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/test` | 000000→120000 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/content/wallet-privacy.md` | 000000→100644 | `5d09cb937f0024a6fae0b4ba3da7f0ae7fd0063f` | W |
| `doc/website/package-lock.json` | 000000→100644 | `6050fc6833412a6e7085001bc395ae2d487c3ced` | W |
| `doc/website/package.json` | 000000→100644 | `15ea51185b9d891dc0797df72891cc52ddd552ff` | W |
| `doc/website/scripts/check-patch-browser.mjs` | 000000→100644 | `097549b6ebd124951f0f73e96c7581a122b2ce93` | W |
| `doc/website/scripts/check-patch-html.mjs` | 000000→100644 | `097549b6ebd124951f0f73e96c7581a122b2ce93` | W |
| `doc/website/scripts/check-static-html.mjs` | 000000→100644 | `5d09cb937f0024a6fae0b4ba3da7f0ae7fd0063f` | W |
| `doc/website/scripts/generate-doc-catalog.mjs` | 000000→100644 | `d41d2e27df060e7e9ac04e7a4e027dce06e904e8` | W |
| `doc/website/scripts/patch-browser.js` | 000000→100644 | `db3606f0c0692d4f7286866af2af8c1ebf4ff997` | W |
| `doc/website/scripts/patch-cache.mjs` | 000000→100644 | `097549b6ebd124951f0f73e96c7581a122b2ce93` | W |
| `doc/website/scripts/patch-overview.mjs` | 000000→100644 | `db3606f0c0692d4f7286866af2af8c1ebf4ff997` | W |
| `doc/website/scripts/patch-page.mjs` | 000000→100644 | `db3606f0c0692d4f7286866af2af8c1ebf4ff997` | W |
| `doc/website/scripts/patch-renderer.mjs` | 000000→100644 | `6050fc6833412a6e7085001bc395ae2d487c3ced` | W |
| `doc/website/scripts/patch-series.mjs` | 000000→100644 | `6050fc6833412a6e7085001bc395ae2d487c3ced` | W |
| `doc/website/scripts/patch-theme.js` | 000000→100644 | `097549b6ebd124951f0f73e96c7581a122b2ce93` | W |
| `doc/website/scripts/prepare-releases.mjs` | 000000→100644 | `15ea51185b9d891dc0797df72891cc52ddd552ff` | W |
| `doc/website/scripts/release-assets.mjs` | 000000→100644 | `ce2be1b6651f5fe9a38256558ddbc2c5c36ac404` | W |
| `doc/website/scripts/release-catalogue.mjs` | 000000→100644 | `ce2be1b6651f5fe9a38256558ddbc2c5c36ac404` | W |
| `doc/website/scripts/render-patches.mjs` | 000000→100644 | `097549b6ebd124951f0f73e96c7581a122b2ce93` | W |
| `doc/website/scripts/website-publication.mjs` | 000000→100644 | `15ea51185b9d891dc0797df72891cc52ddd552ff` | W |
| `test/cmake/test_identity.py` | 100644→100755 | `7e9fe109d2b3419918cb024c1f98282a86425947` | M |
| `test/cmake/test_leveldb.py` | 100644→100755 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | M |
| `test/functional/mempool_dynamic_dust.py` | 100644→100755 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | M |
| `test/functional/mempool_rbf_policy.py` | 100644→100755 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | M |
| `test/functional/mempool_subdust_fee_penalty.py` | 100644→100755 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | M |
| `test/functional/mining_roots_template.py` | 100644→100755 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | M |
| `test/functional/p2p_compactblocks_extratxs.py` | 100644→100755 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | M |
| `test/functional/p2p_roots_controls.py` | 100644→100755 | `7e9fe109d2b3419918cb024c1f98282a86425947` | M |
| `test/functional/rpc_effective_policy.py` | 100644→100755 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | M |
| `test/functional/wallet_sweepprivkeys.py` | 100644→100755 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | M |
| `test/lint/lint-files.py` | 100755→100755 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | L |
| `test/lint/lint_ignore_dirs.py` | 100644→100644 | `0cd0cc423a97133c5e9f6891b921fddb734042bc` | L |
| `test/lint/test_runner/src/lint_docs.rs` | 100644→100644 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | L |
| `test/lint/test_runner/src/util.rs` | 100644→100644 | `611a1e6bcc9e10e43d36815fa4f46910ee83f592` | L |

## Source-accounting verification

### L3 local policy adaptation

The local policy implementation is adapted from S02
`e5445b7c96193c7ec141705aac5ba96651e0ee30`, with S11
`9aaf62146fae1a89d65121468633d5c1701699e9` independent signed-limit/byte
oracle corrections. S03 `846c9de12a5e71259dc1ed829413d68f449ebfd5` supplies
only the common-library placement of the script-size setting, the carrier
extra-weight primitive and the pre-staging arithmetic bound at this stage.
Jaromil's implementation/contract lineage and upstream Bitcoin Core attribution
are retained; new carried test files retain the Plan-B Foundation copyright.

Core v32's accurately counted legacy-sigop algorithm remains authoritative and
is parameterized by the configured local bound. Its native
`ValidateInputsStandardness` result API and diagnostic regressions remain
available. Admission additionally uses the Roots configurable standardness
checks and source rejection reasons. The essential `validation.cpp` changes are
limited to local admission: standardness, carrier accounting, relay coin age,
strict sigop density and an arithmetic guard before narrowing adjusted weight
into the native graph. Block validation, consensus script flags, serialization,
coinbase maturity and the upstream consensus regression assertions are unchanged.
No production `src/consensus/` or `src/script/` changes are needed.

The scoped [policy parsing contract](feature-contracts/policy-parsing.md)
preserves whole-token grammar, effective selection, raw-negation checks,
ranges, quarter-vbyte rounding and contextual diagnostics. Native Core parsing
of inherited controls remains outside that scope. Data-carrier limits default
to 83 aggregate bytes with full counting, and multiple OP_RETURN outputs retain
their actual local rejection. Explicit fixture allowances isolate upstream
consensus/resource assertions from these local restrictions.

Carrier extra-weight arithmetic has an isolated primitive and independent
multiprecision oracle; it has no admission/graph consumer yet. Sub-dust modified
fees, graph reservations, replacement/TRUC/reuse modes and dynamic-dust lifecycle
remain L4; template limits/priority selection remain L5; read/write text config
and effective-policy RPC integration remain L9. Supplied staged controls warn
that enforcement is pending. Parser acceptance and dormant helpers do not
establish these later behaviors. Current L3 execution evidence follows its
focused and complete policy gates; historical source results below remain
historical.

#### L3 local restriction and valid-block coverage

`mempool_roots_local_policy.py::test_paired_boundaries` checks exact admission
and submission rejection reasons, absence from the mempool/template, an explicit
allowing profile, then acceptance of the same transaction in a block under the
rejecting profile. The cases use consensus-valid scripts and rescan MiniWallet
after direct block construction. Zero-valued integer bounds remain bounds;
they are not advertised as disabled settings.

| Local restriction | Paired coverage and profiles |
| --- | --- |
| CAT21 marker / bare pubkey / unknown witness outputs | `mempool_roots_local_policy.py::run_test`: default marker/pubkey rejection, explicit allowing switches, unknown-witness default allowance and configured rejection; same-tx valid blocks. |
| Bare multisig | `test_paired_boundaries`, `bare multisig default/override`: `bare-multisig`, default versus `permitbaremultisig=1`. |
| Carrier-only transactions | `carrier-only default/override`: `bare-datacarrier`, default versus `permitbaredatacarrier=1`; fixture stays above native 65-byte minimum. |
| Carrier enablement / aggregate byte limit | `carrier disabled` and `carrier83/82-byte boundary`: `datacarrier`, disabled/82 versus default 83. `mempool_datacarrier.py` also retains default 83/84 and zero/explicit-large limits. |
| Full input/witness carrier counting / nonstandard carriers | `mempool_datacarrier.py::test_taproot_carrier`: independent switches, exact full-count budget boundary and valid block; `OPNet witness carrier` additionally checks default `txn-datacarrier-nonstandard`, explicit allowance and default-profile block acceptance. |
| Multiple OP_RETURN outputs | `run_test`: exact `multi-op-return` and same-tx valid block; `mempool_accept.py` retains native acceptance assertions under explicit nonstandard allowance. |
| Rune / OLGA token filters | `run_test` and `OLGA token filter`: `tokens-runes` / `tokens-olga`, default token-filter off versus enabled. OLGA isolates the independent nonstandard-carrier/full-count gates. |
| Output / spent-output script bounds | `output script bound` / `spent script bound`: `scriptpubkey-size` / `bad-txns-input-script-size`, configured 33/23 versus default 1650. |
| ScriptSig bound / fixed 1650 ceiling | `scriptSig/redeem serialized bound` and `fixed scriptSig ceiling`: exact `scriptsig-size`; higher configured bound or explicit nonstandard allowance, plus valid blocks. The separate redeem-size check is dominated by the earlier enclosing scriptSig bound under ordinary admission. |
| Total witness size | `default total witness bound`: `bad-witness-witness-size`, default 1650 versus 2000 using valid Taproot NOPs. |
| Witness argument versus tapscript push size | P2WSH/Taproot `witness argument80/81` cases reject only 81-byte arguments with the intended stack-item reasons and accept valid blocks; `mempool_datacarrier.py` accepts an executed 520-byte script push with explicit carrier allowances. `feature_taproot.py` retains its independent consensus push-limit assertions, including invalid oversized pushes. |
| P2WSH stack count / script size / Taproot annex | Dedicated paired cases retain `bad-witness-stackitem-count`, `bad-witness-script-size` and `bad-witness-taproot-annex`; explicit nonstandard allowance permits admission. Large legacy scripts use bounded data pushes rather than exceeding the consensus 201-opcode ceiling. |
| Legacy sigops / strict density / extreme adjusted weight | Configured legacy-limit and density cases retain `bad-txns-input-sigops-toomany-overall` / `bad-txns-too-many-sigops`; default and strict-density 0 allowance are separate. `mempool_sigoplimit.py` retains 2500/2505, package and extreme `too-large-cluster` cases with valid-block counterparts. |
| Dust / ephemeral send, nonzero dust and anchor permissions | `run_test` plus `anchor permission`, `default non-anchor dust permission`, `multiple dust cap`, `bare anchor permission`: intended `dust-nonanchor`, `dust-nonzero`, `anchor`, `dust` and `bare-anchor` reasons; explicit send/dust/anchor permissions or zero dust rate/minimum fee isolate each gate. |
| Relay fee / coin-block age / maturity | `default relay fee floor`, `coin-block gate independent of maturity`, `maturity gate independent of coin blocks`: `min relay fee not met`, `bad-txns-input-immature-coinblocks`, `bad-txns-input-immature-depth`; default/zero/extreme profiles and same-tx valid blocks. `run_test` also retains exact age boundaries and tip reorg/reconsider behavior. |

Malformed redeem scripts, absent witness/control data and other consensus-invalid
fixtures keep their native rejection assertions; they cannot supply a valid-block
acceptance counterpart. The full changed-fixture selection includes all affected
P2P, package, mining and wallet fixtures. Graph fee accounting, dynamic dust and
replacement modes remain L4; independent template selection remains L5. This map
does not imply enforcement of staged controls.

L3 verification on 2026-10-31 used the current Linux headless wallet/tests/IPC
profile. The full native run passed 841 ordinary cases plus one external-assets
warning; CTest passed 380 with one assets skip and no failures. All 38 registered
instances from the 37 changed functional files have current passing evidence:
36 passed the complete run, and the fee-estimation/Taproot-wallet fixtures passed
after explicit local allowances isolated their native padding/fee assumptions.
Their assertions were retained. The extended local-policy test exercised 25
additional rejection/allowance/valid-block pairs. Documentation/include/whitespace
and changed-file syntax checks passed. Missing external assets and Python
`lief`/`capnp`/`zmq`, current Qt/platform profiles and instrumented fuzz remain
explicit gaps for the applicable later owners; registration and historical
baselines do not establish those passes. These are execution observations,
not a parent milestone acceptance or release qualification.

The source baseline maintenance command
`python3 -m unittest discover -s ci/test` ran from the clean roots.6 baseline
at `e93a8cca047c12ce9cab97181dd30705ed0e7e83`: **153 passed, zero failed,
zero skipped**, exit 0. This includes the maintained privacy/docs/release/tool
fixtures, not native v32 execution. No old sanitizer, Qt or platform result is
inherited as a v32 pass.

At the unchanged v32 production base, `python3 test/lint/check-doc.py` exited 0:
209 used arguments, 230 documented, zero undocumented. Its 21 documented-but-not-
matched arguments are informational output, not failed or skipped tests.
The inventory audit compares all 38 source commit identities/order, all 25
stable IDs, supplemental export coverage and all 148 main-only path identities,
last-affecting commits, blob/mode distinctions and symlinks against Git.
All 39 test basenames named in the contract matrix resolve in the selected
roots.6 source. Local-link review finds no relative Markdown links in this
review document; source paths are intentionally revision-qualified code text.
Whitespace, conflict markers, unstaged/untracked scope and empty index are
checked separately because ordinary `git diff --check` excludes this new file.

This completes input/source accounting only. Each implementation owner must
replace planned dispositions with actual current-candidate evidence. L11 must
reconcile every row, range-diff, intermediate buildability, final credits and
whole-candidate compatibility; L12/L13 then pin final canonical/integration
identities. No release, tag, publication, main promotion or product behavior
change occurred during this inventory.

### L4.1 native graph, replacement and fee accounting

Carrier cost, immutable admission dust deficits, configured replacement/TRUC
and full-script reuse claims now feed native v32 graph admission. The carried
behavior comes from S03 (`846c9de12a5e71259dc1ed829413d68f449ebfd5`), with
released S11 independent signed/decimal oracle handling retained. Jaromil,
devjoinedthechat and upstream Core/Knots attribution is preserved. Native
`ByRatio` comparisons, cluster/diagram algorithms, block-removal return values
and deprecated RPC gates are retained; no ancestor compatibility index is added.

Entries and staged additions carry the same final modified fee and adjusted
weight into replacement diagrams, fee changes, ancestor/descendant summaries,
trimming and removal/reorg dependency restoration. Actual fees, BIP141 metrics
and block rewards remain independent. The global absolute-fee/weight budgets
retain the released conservative main-plus-staged bound, including pending
removals. Direct reservation tests cover abort, failed fee changes, committed
replacement and return to zero, including intentionally rejected replacements
whose final state alone would fit. See `doc/feature-contracts/subdust.md`.

Independent multiprecision regression reproduced a source discrepancy at signed
extremes: repeated relative updates of an already saturated modified fee differed
from re-admission using the aggregate stored operator delta. Resident updates now
recompute the documented admission formula, with atomic budget failure. The
wallet chain-limit query also checks its synthetic fee/weight reservation before
staging; an infeasible `INT64_MIN` delta is rejected. Neither correction widens
native storage or changes actual transaction fees or block consensus.

Native RPC size/chunk/fee descriptions include carrier cost and dust deficits.
`vsize_bip141` and transaction `weight` retain actual metrics;
`vsize_adjusted` and graph fields report policy accounting. The deprecated
`fullrbf` field remains opt-in and reports the applied input-conflict Always mode, while
deprecated `bip125-replaceable` retains signaling meaning. Runtime configuration
and the full effective-policy schema remain L9-owned.

L4.1 focused verification executed all 52 selected amount, mempool, RBF,
package, txgraph, cluster-linearization, policy-parser and oracle cases, plus
the complete 29-script affected graph/package profile on rebuilt binaries.
Pure sigop sizing explicitly disables carrier cost as in the released source;
two dust/anchor allowing profiles disable the separately tested deficit.
Exact local rejection, submission, template exclusion and same-transaction valid
block assertions remain. New subdust cases check independent fee/weight arithmetic,
package ancestry, replacement, atomic RPC failure and actual block rewards.

The released code gates prevout conflicts by Never/OptIn but inserts independent
script-reuse conflicts later, subject to replacement economics. The older
catalog statement that Never disables all replacement was broader than this
runtime path. This port preserves the observed implementation and makes its
scope explicit in current help/docs. `mempool_graph_policy.py` characterizes
both Never and OptIn with independent inputs and non-signaling v2 originals:
one satoshi below the exact incremental relay requirement rejects while equality
replaces, with exact mempool and actual/modified fee outcomes. The original
default reuse, direct-prevout and TRUC mode assertions remain.

Dynamic-dust publication, recomputation, locking and persisted operator-delta
serialization are covered by the following L4.2 implementation. At the L4.1
boundary, newly carried arithmetic fuzz sources were registration, not an
instrumented pass.
The subdust script retains the optional `--test-mining-priority` assertions as
an explicit L5 gate.

### L4.2 dynamic dust and lifecycle

The complete released S03/S11 contracts now use native v32 estimator and
scheduler ownership. One task is initialized after successful chainstate loading
and estimator construction, before mempool reload; retries cannot capture an
abandoned pool. Native scheduler stop/join precedes owner destruction. Block
targets preserve the raw LONG-horizon 80% estimate, and mempool targets preserve
actual fees at native policy-size positions. Safe thousandth scaling and the
configured floor/missing-estimate fallback retain independent arithmetic
oracles. Full snapshots, admission and node/wallet dust interfaces synchronize
on the mempool lock; resident deficits are immutable.

Persistence now writes the stored operator delta rather than the modified-fee
delta, preventing a dust deficit from being serialized as an operator change
and subtracted twice on restart. Reorg admission captures one fresh deficit;
native confirmation clearing of operator deltas remains intact. Startup setting
changes, default/off, enabled/blocksonly, scheduler, missing/warmed estimates,
reindex retry, restart and reorg are observable lifecycle fixtures. Independent
P2SH, P2WSH, Taproot and mixed-input carrier fixtures calculate exact adjusted
weight/fee boundaries while retaining BIP141 metrics and valid-block acceptance.
Mining priority remains L5; integrated runtime configuration and effective-policy
RPC schema remain L9. Current full-suite and instrumented execution results are
recorded separately from the historical L4.1 boundary.
