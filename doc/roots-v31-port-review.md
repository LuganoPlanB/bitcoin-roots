# Roots Core v31 port review

This is a preservation review for an unpublished local candidate, not a release
announcement or an executable patch manifest. The accepted 30.3 contracts define
intent; Git defines each version's implementation. At the input-review stage,
the candidate contained unmodified Core plus this document. The foundation now
carries preservation documentation and CMake selections/probes described in
[the portability guide](roots-build-portability.md). Later themes must land
before their Roots runtime behavior can be described as available.

## Exact inputs and authentication

The source base is Core v30.3 commit
`49faec4f87f5cd19c88db01a82e5c68b087c8227`. The selected canonical source is
`17e1d484aba2a881be03b8928fba3c0125e027a2`; the final source including the
accepted contract, parser, RPC and cross-feature work is
`10d772eb7f4b00b5193428a0dde4c06558d6c297` on
`topic/30.3/feature-contracts`. The final range contains **72 commits**, including
68 original canonical commits and four preparation commits. L4 intentionally
made no change and has no commit. Both source ancestry checks pass.

The selected new base is the official stable **Core v31.1** release:

- Annotated tag: `v31.1`, object `bfa6a4b79cd4c1562fd32857e3147763efae37fb`.
- Peeled commit: `9be056a8a72b624dae9623b2f7bded92c2a21c91`.
- Primary signer: Michael Ford,
  `E777299FC265DD04793070EB944D35F9AC3DB76A`.
- Signing subkey: `CFB16E21C950F67FA95E558F2EEB9F5CC09526C1`.
- Isolated-keyring `git verify-tag --raw v31.1`: exit 0, `GOODSIG` and
  `VALIDSIG` with the above subkey and primary fingerprint. GPG owner trust is
  undefined; this is not claimed as a web-of-trust certification.

On 2026-10-11 the [official release index](https://bitcoincore.org/en/releases/)
and [31.1 release page](https://bitcoincore.org/en/releases/31.1/) identified 31.1
as the newest stable v31 point release. The tag was fetched explicitly from
`https://github.com/bitcoin/bitcoin.git`; its object and peeled commit match
that repository's advertised ref. Signer identity was authenticated by matching
the full primary fingerprint from the independent official
[contact page](https://bitcoincore.org/en/contact/) and repository
[security policy](https://github.com/bitcoin/bitcoin/blob/v31.1/SECURITY.md).
The public key was retrieved from keys.openpgp.org by that full fingerprint;
keyserver presence alone was not the trust basis. The cryptographic result and
the independently sourced identity are separate evidence.

Before creating a branch, `git ls-remote` against the public Roots repository
for `refs/heads/*31*` and `refs/tags/*31*` returned no matching refs. Local
heads, remote-tracking refs and worktrees also contained no v31 product line.
The new local `topic/31.1/feature-contract-port` worktree was created directly
from `v31.1`, initially with exact base HEAD and a clean index/worktree.
The public `main` was separately refreshed and inspected at
`cbfef9c160484af94442db9c1a992c3ef0d9a639`; its new compatibility-harness/test
history still identifies Core v30.3 as its base, and v31.1 is not an ancestor.
This public-main advancement is not substituted for the separately accepted
source tip above. The old Roots tip is not an ancestor. No public ref was changed; no old Roots
branch was merged. Core's own release merge at the base is upstream history,
not a Roots-series merge.

## Proposed smaller series and dependencies

The user approved a nine-theme target: **9 instead of 72 commits**. The former
policy/mining theme is split into independently reviewed local checks, graph
accounting, and mining adaptations. Historical fixes,
tests and documentation travel with the behavior they correct; release/CI work
travels with maintenance, rather than recreating chronological fixup commits.
No broad compatibility layer is proposed.

| Theme | Owning scope and prerequisite |
| --- | --- |
| L6 build/portability | Dependency probes and supported switches; contract documents and this review. Establishes the foundation only. |
| L7 parsing/local checks | Scoped parsing, script/witness/sigop standardness, carrier detection/budgets, fixed dust/ephemeral and relay age/maturity. Requires L6. |
| L8 graph policy | Carrier policy weight, sub-dust modified fees, replacement/TRUC/reuse and authorized dynamic-dust repair. Requires L7. |
| L9 mining | Coin-age priority, template normalization, residual chunk assembly and joint interactions. Requires L8. |
| L10 peers | Resource/permission controls and current-connection health; GUI/functional coverage. Requires L7 and L6. |
| L11 wallet | Database/signer boundaries, per-send RBF, sweep authorization/secrets and coin-control facts. Requires L10. |
| L12 operator/runtime | Read/write config, effective-policy RPC and platform resource support; dormant stats remain dormant. Requires L7 state and L11. |
| L13 integration | Identity/icons/QR/taskbar, generated help artifacts, CI and release/export tools. Requires L12. |
| L14 evidence | Final preservation review, range comparison and exact-candidate supported gates. Requires L6–L13. |

Preimplementation review found distinct algorithm risks: v31's block builder
can include or skip only the current whole chunk, while Roots selects an
individual priority prefix before fee-selected descendants. Graph policy
weight/modified-fee consistency across staging, diagrams, replacement and
trimming needs its own review before that mining adaptation. The approved
split separates those risks without restoring ancestor indexes or retiring
any intended contract. Existing accepted commits are unchanged.

The user also authorized a bounded L8 repair of the documented non-off
dynamic-dust startup defect: node-owned scheduling after final successful
chainstate load, safe retry/shutdown lifetime, configured-floor fallback when
estimates are unavailable and overflow-safe multiplier scaling, all with
regressions. Default remains off. This authorization does not enable dormant
statistics or permit unrelated repairs. Dynamic runtime is still deferred.

`runtime-config` is an L12 implementation dependency of L7 parsing and L11 sweep.
Until then, feature tests must cover Core CLI/config semantics and explicitly
defer Roots `confrw` precedence cases; they cannot claim those cases passed.
The full precedence gate runs when L12 lands. Feature-local tests still ship
with each implementation; L14 is additional aggregate evidence.

## Contract dispositions against actual v31.1 APIs

These are implementation directions for review, not completed-port claims.
**Adapted** retains all intended behavior while changing Core-specific placement;
**kept** retains compatible behavior/support; **adopted-upstream** needs equivalent
defaults, configuration, invariants and current acceptance evidence. No intended
behavior is proposed **dropped**. Sub-aspect equivalence cannot retire a whole
contract with remaining Roots behavior. Existing source gaps remain explicit.

| Contract | Disposition / theme | Upstream comparison, adaptation and acceptance gate |
| --- | --- | --- |
| policy-parsing | adapted / L7,L8 | `node::ApplyArgsManOptions` consumes cluster limits and scoped grammar; L8 consumes graph controls and repairs dynamic-dust publication. L9 consumes mining size and priority controls. Preserve L2's complete-token/range/diagnostic decisions, quarter-vbyte rounding and CLI/config precedence. Parser unit/startup and L5 fuzz boundaries are carried; integrated read/write precedence remains L12. |
| effective-policy | adapted / L12 | `getmempoolinfo` has cluster-era size/fee information but no Roots `policy` object. Read authoritative initialized v31 state plus synchronized dynamic dust; retain L3's schema, units and enforcement gates. Port `rpc_effective_policy.py`, help and admission parity assertions. L4 supplied no snapshot refactor to carry. |
| policy-standardness | adapted / L7 | Core `IsStandardTx` takes carrier budget, bare-multisig flag and dust rate. Core permits bare multisig by default and lacks Roots configurable script/witness, strict sigop, CAT21/token and ephemeral modes. Preserve Roots limits/defaults/reasons solely in local gates. Run transaction/sigop/TRUC cases and valid-block fixtures; do not change script consensus. |
| policy-carrier | adapted / L7,L8 | Core defaults to a 100,000-vbyte OP_RETURN budget, not Roots' aggregate 83-byte budget, and lacks recognized input/witness carriers and cost weighting. L7 adapts detection/budgets; L8 adapts extra policy weight to `TxGraph` fee/weight accounting. Retain Roots `multi-op-return` rejection despite source help conflict. Port `mempool_datacarrier.py`, extra-weight boundaries, tapscript and L5 dust-cost interactions. |
| policy-dust | adapted / L7,L8 | L7 preserves fixed dust/ephemeral checks. L8.2 adds node-owned scheduling after final successful load, synchronized snapshots and initialized estimator/native cluster-position selection. Missing estimates return the configured floor; scaling saturates safely. This repairs the documented source lifecycle/fallback/extreme gaps. Focused runtime tests and whole-L8 gates are recorded separately in the L8 evidence. |
| policy-subdust | adapted / L8 | No upstream deficit penalty. Preserve worked contract units, saturation, exemptions and modified-fee accounting without changing actual fee rewards. Adapt policy weight/modified fees consistently through cluster admission, replacement and mining. Port sub-dust tests and L5 arithmetic fuzz/valid-block cases. |
| policy-replacement | adapted / L8 | Core has unconditional full RBF and cluster feerate-diagram replacement, with enforced TRUC; Roots Never/OptIn/TRUC mode controls are absent. Default full RBF alone is not equivalence. Preserve configured modes, v3 opt-in behavior, fee/resource gates and truthful `fullrbf`; adapt to current replacement algorithms without restoring ancestor indexes. Run RBF/TRUC/package/sibling and policy-block tests. |
| policy-coinage | adapted / L7 | Relay coin-block age and maturity controls are absent. Reuse available coin heights; add local gates and bounded age arithmetic, never alter consensus coinbase maturity. Preserve disabled defaults and add/retain enabled, boundary and valid-block acceptance cases; the historical dedicated-relay coverage gap is addressed by candidate enabled/disabled, boundary, tip-reorg and valid-block tests. |
| policy-reuse | adapted / L8 | No upstream `spkreuse` creation/spend tracking or conflict mode. Preserve default allow and explicitly configured local tracking across admission, replacement and reorg. Dedicated functional mode/reorg coverage is still required before claiming equivalence. |
| mining-template | adapted / L9 | `BlockAssembler::addChunks` replaces ancestor-index assembly. Core's default weight is 4,000,000 with reserved-weight handling and minimum fee 1 sat/kvB, not Roots' 1,200,000 weight / 1,000 sat/kvB / 100,000-byte priority area. Preserve size/weight normalization and coin-age priority while avoiding duplicates and charging ancestor/chunk resources correctly. Port miner boundaries and L5 priority/fee/block-acceptance cases. |
| peer-controls | adapted / L10 | Core compact reconstruction extras default to 100 entries, with no Roots 10 MB memory bound; orphanage is `node::TxOrphanage`, not the former pool layout. Core whitelist/upload/BIP permissions do not establish forceinbound, explicit compact-filter permission, subnet disconnect or clearnet-v2-only equivalence. Preserve each documented default, malformed-input bound, Tor-bind isolation and SOCKS secret boundary; run extra-tx, permissions, filter, eviction and invalid-message tests. |
| peer-health | adapted / L10 | Core has current-peer stats/table/details but lacks the complete Roots extra fields and unavailable-state contract. Use existing refresh/interfaces; port stale-detail reset, sort/selection and bounded CPU reporting. Run `rpc_net.py`/Qt apptests and required resolution/font-scale matrix; no collector/history feature. |
| wallet-foundation | adapted / L11 | Descriptor/SQLite and external signing remain upstream. v31 database enumeration still recursively traverses directories; Roots directory/reparse/permission safeguards remain necessary. `RunCommandParseJSON` now takes an argv vector instead of a shell command string; preserve child-FD closure on this API. Port multiwallet/signer/unit cases, retaining no-BDB-build and locking boundaries. |
| wallet-rbf | adapted / L11 | Core has a per-send checkbox and sequence override, but initializes the checkbox checked rather than from wallet `walletrbf`. A similar UI is insufficient. Preserve wallet-derived preference, explicit overrides, confirmation and PSBT signaling tests. |
| wallet-sweep | adapted / L11 | No Core `sweepprivkeys` RPC/dialog. Adapt wallet fee/coin APIs and cancellation through interfaces; preserve WIF/script scope, selected local-key descriptor destination, no import, preview default, locked fee snapshot, transient secure cache/redaction and final broadcast authorization. Run sweep functional/Qt/security-structure tests plus runtime cancellation and visual evidence. |
| wallet-coin-control | adapted / L11 | Core retains ordinary coin-control/listLockedCoins; it lacks Roots effective/reuse/grouping facts and full invalidation contract. Adapt current wallet output/input-size APIs, preserve visibility/selection filters and conspicuous change validation. Run wallet-send/Qt selection/invalidation/source checks and populated GUI matrix. |
| runtime-config | adapted / L12 | `common::MergeSettings` contains forced/CLI/JSON/config sources, but no Roots read/write text configuration layer or `confrw`. Insert the existing layer and typed writes into current ArgsManager without a duplicate policy store; preserve network-file/default/negation/list rules. Run getarg/config functional and deferred L7 precedence cases. |
| operator-stats | kept; uptime adopted-upstream / L12 | `uptime` still returns `TicksSeconds(GetUptime())`; `GetUptime` uses `SteadyClock` initialized at app startup. Its function/schema/clock/defaults are identical in relevant source; upstream adds a wall-clock-jump regression. Both the exact pinned-source acceptance test and current upstream `rpc_uptime.py` pass on the authenticated base. Independent review accepted this narrow sub-aspect, not retirement of the whole contract. Core has no Roots stats model/RPC. Keep the retained stats source dormant: do not register, sample or advertise `getmempoolstats` without a separate compatibility decision. Source GUI/default/help conflict remains a gap. |
| runtime-resources | adapted / L12 | Core has `GetTotalRAM` and RAM-aware cache sizing, so helper-name similarity cannot retire Roots sizing semantics. Compare exact cache thresholds/units and failure defaults before sub-aspect retirement; preserve Roots byte-safe allocation, FD bounds and platform-scoped advisory helpers where needed. Run RAM/FS/util tests and actual supported-platform checks; unused helpers are not a cross-platform service. |
| build-portability | adapted / L6 | Core remains CMake/C++20 with wallet/tests enabled, GUI off, embedded LevelDB/secp256k1, optional signer/QR and IPC. It raises GCC/Clang minimums and supplies Qt6-only CMake targets. Carry Roots Qt5/6 selection, required system-library usability probes and native crypto flags on these APIs; missing optional dependencies are explicit gaps. A declared Tor-subprocess switch has no source consumer and must stay a non-feature, not become an automatic launcher. MiniUPnPc build support must not imply a missing runtime consumer. |
| identity-desktop | adapted / L13 | Core version/icons/help remain Core; Roots identity, QR-PNG export and Windows taskbar support are not equivalent. Port limited identity/desktop changes while preserving upstream names/protocol attribution. Generate manuals/config from final help only; run version, Qt QR/icon and Windows/platform/visual gates. |
| maintenance-ci | adapted / L13 | Core's v31 compiler/platform/cluster fixtures supersede old job assumptions; Roots classifier, exact promotion proof and platform/fuzz scheduling need adaptation. Retain actual exact-candidate CI gates and bounded corpora scheduling; run maintained tool tests and supported candidate profiles. |
| maintenance-release | adapted / L13 | Upstream does not provide Roots immutable source/tag/assets, signed new-draft-only five-platform release, portable export or contributor-credit rules. Adapt version/base validation and replay without publishing/tagging. Retain `.github` exclusions and `git am -3 --keep-cr`; test temporary export/replay and all release tools. Expanded notices remain deferred. |
| maintenance-docs | adapted / L6,L13,L14 | Carry accepted contracts first with explicit deferred status, reconcile catalog/privacy/provenance to actual candidate, generate help artifacts when interfaces land, and complete review evidence last. Historical release notes are provenance, not a v31 feature claim. Check links/doc lint/whitespace and generated-output parity. |
| maintenance-native | adapted / owning theme | Inspect every surviving source hunk; keep behavior-preserving C++/empty-write/test hooks only where needed on v31 APIs. Port test fixtures with their features rather than overwriting newer Core tests. Transient reverted script work is not a new consensus feature. Run affected unit/functional/fuzz gates. |

The shared consensus-neutral and no-RDTS boundaries apply to every row. Contract
dependencies from the accepted collection remain in force. No retirement of
Qt5, optional system libraries, dormant support or intended operator behavior
is authorized by a missing local dependency. If a required support path cannot
be preserved on v31, seek an explicit compatibility decision before removing it.

## Complete source-to-theme accounting

Every abbreviated object below uniquely resolves in the pinned source range.
Rows assign the final semantic content, including tests, docs and maintenance;
mixed source commits may feed multiple themes. References are provenance, not
instructions to replay whole commits. Aggregate commits must include the full
`Source-commit:` values for the content they use and honest adaptation notes.

| Source commits | Owning themes and treatment |
| --- | --- |
| `5c210e17fb` | L6 build/dependency support; L13 platform CI/release integration. Adapt required probes, not old generated summaries. |
| `85257a0986`, `724f8c2ee7` | L7–L9 policy utility dependencies; L10 peer diagnostics; L11 signer boundary; L12 config/runtime/stats support. Split mixed runtime content by contract; preserve dormant status. |
| `1b73fe3d21`, `ec9db369f5` | L7 local checks; L8 accounting/dynamic dust/replacement; L9 mining and broad functional fixtures; L10 peer controls; L11 wallet boundary fixes/fixtures; L12 runtime dependencies; L13 CI test-script integration. Adapt actual surviving deltas to clusters/interfaces. |
| `dd03dcb989` | L11 database/SQLite/signer hardening, adapted. |
| `c592888088` | L6 Qt build support; L10 peer view; L11 wallet GUI support; L13 identity/QR/taskbar. Split mixed GUI content without importing incidental upstream text. |
| `fc0dc945d0` | L7 fixed-dust/sigop tests; L8 sub-dust/TRUC tests, plus L10 peer fixtures and L12 operator tests where present. Preserve tests' actual feature ownership. |
| `9f1780f330`, `4570a6a547` | L6 necessary build registration; L13 assurance/CI/release tooling and fixtures; owning-feature tests across L7–L12. No replacement maintenance state system. |
| `9da5851410`, `26b691e4f3`, `201927c151`, `e819a07a4a`, `56e2894760` | L13 CI scheduling, apt-retry, optional-feature selection and branded archive checks; adapt to v31 jobs. |
| `83fa42a963`, `f5182f0e3a`, `7791eede50`, `11c84c39ef`, `0efa437e29`, `f2595557d9`, `38bc7ec2ce`, `df8aa8e2b7`, `ddae467a78`, `778ff50d19`, `c474825f96`, `17e1d484ab` | L13 release/rehearsal/source/archives/signing/new-draft/promotion CI and notice deferral; preserve final safeguards, fold corrective history. `f5182f0e3a` also supplies L11 sweep/privacy boundary tests and L12 safe-empty-stream writes/tests, plus L13 apt/sanitizer checks. No releases dispatched. |
| `25f889f97e`, `3e13c1f213` | L13 Windows icon and QR QString-size corrections, kept/adapted with identity. |
| `b0b235d6a1`, `f4066e5282`, `30f673a4bc`, `a13f28e2f4`, `1517d925b2`, `628bdf4e38`, `02464df449`, `089888acdd` | L6/L13/L14 catalog, scope, historical notes and provenance; rewrite candidate claims for actual implementation, retain relevant history/credit. |
| `bbd7339af2`, `9b778b9a23` | L10 peer stale-state fix/apptests; L12 streams assertion fixtures and L13 QR/build GUI assertions from the mixed test commit. |
| `e4b26462de`, `0e3ae72d89` | L11 wallet-derived per-send RBF and locked Qt assertions, adapted. |
| `5e2d79e937` | L7 `node/mempool_args.cpp` string-view prefix modernization, adapted only where the parser remains; no consensus feature. |
| `fb21288879`, `dc1b217cbd`, `9a216bc389`, `7c70374050`, `f3cd7da535`, `96fecfd411`, `a1748ccc64`, `f9e9a36cd1` | L11 sweep RPC/GUI, coin-control facts, documentation, asynchronous authorization, locked fee snapshot and secret cleansing. Fold security corrections into final feature. |
| `875fa15829`, `9ab60f217b`, `9d9c125d9e` | L9 priority-selected ancestor/resource correctness and locale-independent regression fixtures, adapted to chunk mining. |
| `aa27c9117a`, `48f3b3bbe8`, `a6f35e915c` | L8 configured RBF/TRUC semantics and tests; L12 truthful RPC field; L13 final generated help. |
| `30e7eac266`, `12d67ba60a`, `e6d4abc704`, `085a84a7cb`, `8cb07ef1b9` | L7–L10/L12 feature-owned full-suite, Taproot-carrier, fee-estimator and startup fixtures; adapt to current APIs without erasing upstream regressions. |
| `1f7cfce176` | L13 regenerate manuals/example configuration from final help, not source-byte replay. |
| `16b2198275`, `47329c00dd`, `1b2431c74c` | L6 native crypto flags, exact MiniUPnPc dependency patch and usable system LevelDB memory environment; the Chain interface include and mempool/1p1c lint fixtures follow L7/L10. |
| `784b2af17a`, `f74cc0433b`, `73c97862e2` | L8 synchronized dynamic-dust publication/snapshot and changing-rate tests; consumers in L12 report the same authoritative state. |
| `f66eca4ae4` | L6 accepted stable contract collection and source/provenance reconciliation. |
| `87c950de92` | L7 strict policy grammar, scoped help/diagnostics and parser/startup tests; distinct source behavior change. |
| `b60a6e04d0` | L12 additive effective-policy RPC/schema/parity tests; distinct source API change. |
| `10d772eb7f` | L7 local valid-block/carrier interactions and parser fuzz; L8 penalty/carrier arithmetic and fuzz; L9 priority/fee/template interactions; L6 retains documented gaps until tests are ported. |

Source-range authors are Jaromil and devjoinedthechat. Preserve their existing
copyright/credit and upstream Core/Knots lineage; use retained source authorship
and attribution in aggregate commit bodies. These thematic rewrites are not
single-commit cherry-picks and must not fabricate an `-x` trailer.

## Input-stage verification

Baseline evidence is tied to untouched Core commit
`9be056a8a72b624dae9623b2f7bded92c2a21c91`, not to a completed Roots port.
The Linux headless descriptor-wallet profile uses GCC 14.2, CMake 3.31,
Python 3.13, SQLite 3.46.1 and libevent 2.1.13, with GUI and IPC explicitly off.
`ccache` is absent; no cache launcher is selected. Builds use conservative
parallelism and functional tests use isolated regtest directories.

The executed baseline commands (build-directory spelling shortened here) are:

```sh
cmake -S . -B build-baseline -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_GUI=OFF -DENABLE_IPC=OFF \
  -DCMAKE_C_COMPILER_LAUNCHER= -DCMAKE_CXX_COMPILER_LAUNCHER=
cmake --build build-baseline -j 2
ctest --test-dir build-baseline --output-on-failure -j 2
build-baseline/test/functional/test_runner.py rpc_uptime.py feature_config_args.py \
  mempool_dust.py wallet_multiwallet.py wallet_signer.py --jobs=2 \
  --tmpdir=<isolated-baseline-regtest-directory>
python3 test/lint/check-doc.py
```

Configure/build exit 0; all 577 build steps complete with no compiler warnings
or errors. CTest exits 0: **152 executed/passed, 0 failed, 1 skipped** out of
153 registered tests. `script_assets_tests` skips because external unit-test
data was not configured; it is not a pass. The focused functional runner exits
0: **6 executed/passed, 0 failed, 0 skipped**, including the automatic
`wallet_multiwallet.py --usecli` variant. This proves Core's baseline profile,
not the deferred Roots features.

The exact pinned-source `test/functional/rpc_uptime.py` was also run from the
30.3 source worktree against this baseline's `test/config.ini`, with an explicit
isolated `--tmpdir`: **1 executed/passed, 0 skipped**, exit 0. Together with
upstream's stronger clock-jump case and equal implementation/schema/defaults,
this supports the narrow uptime equivalence decision. Documentation lint exits
0: 210 options used, 222 documented, none undocumented. Input inspection confirms
72/72 unique source references and 25/25 contract rows, exact authenticated base,
no old Roots ancestry, empty index, clean pinned source and review-document
whitespace. Full candidate functional/GUI/platform/fuzz gates remain later work.

Initial input-stage optional-profile probes established (before external prerequisite repair):

- GUI with default QR support could not configure: libqrencode was absent.
- GUI with QR disabled also could not configure: Qt6 (minimum 6.2) was absent.
- IPC-enabled headless configuration succeeds; this alone is not an IPC
  executable or runtime-test pass.
- Clang 19 fuzz/address/undefined configuration could not link: compiler-rt ASan
  static, normal and C++ archives were absent. That probe did not execute
  sanitizers/fuzzing. The executor installed no dependencies.

Platform/GUI/sanitizer gaps do not justify claiming unsupported profiles work,
retiring intended support, or using the prior source build as candidate proof.

## Foundation support and remaining gates

The uncommitted foundation carries Qt-major selection through discovery,
targets, resources, translation tools and static plugins. Qt 5's older static
packages without `qt5_import_plugins` now receive an explicit diagnostic; the
old source already called that unavailable command. Dynamic Qt 5 retains its
5.11.3 minimum. This is a capability diagnostic, not a decision to retire Qt 5.
Four localized source-backed adaptations restore Qt-major library-path, locale
name and address-filter API selection, and the explicit QDebug header required
by the external-signer warning. No signer or wallet operation changes.

System LevelDB preserves the source's release/debug MemEnv link checks, adds
fresh probes when a configured package changes, handles integrated/split helper
cache transitions and absent configurations, and selects the system MemEnv
header in the production wrapper. System secp256k1 retains required module
checks and adds v31's `secp256k1_musig_pubkey_get` requirement using imported
pkg-config target usage, refreshing cached metadata/library discovery after
package search-path changes. Embedded libraries remain the default. Native crypto
flags remain on upstream targets/files; no compatibility layer is introduced.

MiniUPnPc discovery retains the OFF default and API >=17 requirement. Its
summary explicitly distinguishes dependency discovery from the deferred UPnP
runtime and depends recipe integration. Roots taskbar, dedicated Tor launch,
policy, peer and wallet extensions remain unavailable until their owning
themes; all 25 preservation contracts and source gaps are retained in the
[catalog](roots-features.md).

Finder fixtures exercise integrated/split MemEnv, missing-helper rejection and
an unusable Debug configuration despite usable Release. Additional isolated
fixtures point at the already-built v31 embedded libraries: both system-option
configurations succeed, the production database/MuSig objects compile, and a
header fixture missing only the required MuSig getter is rejected. These are
finder/API/header adaptation evidence, not full distribution-package profiles.
Repeated same-cache LevelDB transitions link the consumer in every successful
configuration. A same-cache secp256k1 valid/legacy/valid sequence rejects exactly
the missing MuSig getter and recovers after restoring the valid package.

An external prerequisite repair supplied Qt 5.15.15, Qt 6.8.2, QRencode 4.1.1
and Clang 19 compiler-rt. Both real GUI profiles configure with QR and DBus
ON and IPC OFF, build, and pass offscreen CTest: **154 passed, 0 failed,
1 skipped** out of 155 per profile. Six Qt suites each report an aggregate
21 passes including setup/cleanup, with zero failures or skips. This proves
Linux offscreen build/test support, not visual, static, Windows or X11 coverage.
Qt 6 discovery reports missing optional XKB headers and Doxygen; neither
prevents the tested profile. The executor performed no installation.

Clang fuzz/address/undefined configuration and the upstream bitcoin_crypto
build succeed. A bounded SHA256 harness links with all three sanitizers,
checks the known abc digest and runs 1,000 seeded inputs without findings;
SHA256AutoDetect selects x86_shani(1way;2way). This is compiler/runtime/native
crypto support evidence, not the deferred L7 policy or arithmetic fuzz suite.
Real system LevelDB, secp256k1 and MiniUPnPc package profiles remain unproven;
fixture results do not replace them or retire their intended support.

Current foundation verification on 2026-10-11: the Linux headless wallet
profile builds with IPC OFF and ON. CTest reports respectively **153 passed,
0 failed, 1 skipped** and **155 passed, 0 failed, 1 skipped**; external
`script_assets_tests` data is unavailable in both. Uptime/config/signer
functional tests pass three cases per profile; an explicit `bitcoin-node`
override also passes those three cases. Both IPC Python interface cases skip
because `capnp` is unavailable, so IPC interface runtime coverage is not claimed.
Documentation option lint, local links/anchors and whitespace checks pass.
The required real Qt 5/6 GUI configure/build/test gate now passes. Foundation
changes remain uncommitted for independent review.

## L7 local policy verification

The approved narrowed L7 adapts scoped parsing and local checks. Candidate
links are collected in [the policy catalog](roots-features.md#local-policy-and-mining)
and [the parsing contract](feature-contracts/policy-parsing.md). The source
`policy_args` fuzz harness from
`10d772eb7f4b00b5193428a0dde4c06558d6c297` is retained, and native transaction
fuzzing now calls carrier recognition. Native overloads keep their existing
signatures and use the Roots defaults; their non-production callers are unit
and fuzz tests. Production admission supplies the initialized option snapshot.

Admission maturity uses next-block depth, with unconfirmed inputs contributing
zero age. Coin-block accumulation caps at the configured bound before each
multiplication/addition, preserving the integer boundary without floating-point
rounding or an ancestor/priority cache. Fresh admission after a tip reorg is
covered. Carrier checks remain independent of the general standardness switch;
full counting and nonstandard-carrier permission are tested separately.

All added rejection paths stay in local policy or `MemPoolAccept::PreChecks`.
`ConnectBlock`, consensus input/script checks and flags, actual fee/reward
accounting, native TxGraph and mining implementations have no L7 changes.
Consensus-valid block fixtures cover locally rejected CAT21, bare pubkey,
unknown-witness outputs, token markers, multiple OP_RETURN, dust, carrier and
relay-age transactions. Native script/package/cache/wallet-notification tests
retain their assertions with explicit raw-pubkey fixture allowances and precise
rejection reasons. Resource tests retain their upstream cluster/package/TRUC/RBF
and mining assertions with explicit carrier/script budgets across restarts;
multi-OP_RETURN tests assert Roots rejection and Core's nonstandard override.
Fee-estimator and RBF low-feerate fixtures explicitly select their sampling range.

The remaining default headless functional suite is also covered, beyond the
selected policy filter. Its block resurrection, CSV/DER, orphan/package,
burn-accounting RPC and large wallet-script fixtures retain their original
assertions with explicit allowances for their constructed outputs and padding.
The IBD fee-filter fixture follows native bucket quantization; L10 restores
the source incremental-fee seed selection and its corresponding fixture values. Full native units, non-Boost CTest
gates, and the applicable functional tests pass; optional-feature/environment
skips remain gaps. The bounded Clang ASan/UBSan campaigns cover parser,
transaction/carrier and native input/witness callers, not the L8/L9 extensions.

L5 interaction ownership remains bounded:

| Source coverage | Candidate disposition/remaining owner |
| --- | --- |
| Parser grammar, types, negation, cost and dynamic syntax round trips | L7 unit/startup and actual sanitizer fuzz; no dynamic runtime claim. |
| Fixed dust thresholds, ephemeral/local standardness, carrier budgets and valid blocks | L7 focused tests and broader native regression fixtures. |
| Subdust deficit rounding/saturation, carrier extra weight, fee deltas, replacement/package/TRUC/reuse combinations | L8; existing source methods and arithmetic fuzz remain required gates. L7's native regression runs do not establish these Roots extensions. |
| Dynamic startup, publication, estimator fallback, extreme scaling and retry/shutdown | Authorized bounded L8 repair delivered; L9 repeats the joint publication/template/reward gate. Default off is unchanged. |
| Priority ancestors, residual mining, template normalization, actual-versus-modified reward interactions | L9 source L5 fixtures and native residual/priority/resource regressions delivered; joint gates below. |
| Integrated Roots confrw precedence | L12 only; ordinary CLI/config/JSON cases run in L7. |

Prior-release mempool compatibility remains skipped because release fixtures are
unavailable. External script-assets and other previously recorded optional
matrix dependencies are not represented as passes. Final L7 acceptance and the
later full candidate integration matrix remain separate review gates.

## L8.1 native graph accounting and replacement

Carrier cost, sub-dust penalty, configured RBF/TRUC modes and script reuse now
have consumers; their L7 parsing-only warnings are removed. Dynamic dust remains
L8.2 and mining size/priority controls remain L9. `getmempoolinfo.fullrbf` reports
the initialized RBF mode here, while the additive effective-policy schema is L12.

Entries retain actual fees/weight separately from carrier-adjusted policy weight
and admission-time deficits. A changeset inserts the final fee and weight into
`TxGraph`; resident deltas update that same graph fee. Native chunk diagrams,
replacement, ancestor/descendant RPC summaries and trimming therefore share the
same accounting. Graph tests cover abort/commit, fee updates, block removal and
reorg dependency discovery. Native chunk mining already reads this accounting;
L9 owns priority/residual selection and its remaining combinations. Coinbase
rewards continue to use actual fees. Valid-block fixtures cover carrier cost,
sub-dust rejection, TRUC rejection and script reuse.

Numerical adaptation is deliberate: fee-rate evaluation saturates before an
unrepresentable quotient, and graph admission reserves absolute modified fees
and adjusted weights across the main graph and pending additions. The budgets
are `INT64_MAX` and `INT32_MAX`, respectively; pending removals are conservatively
reserved until commit. Extreme proposals can therefore be rejected even when
the final graph alone would fit. Failed resident fee changes are atomic and
reported as RPC errors. This avoids wrapping policy deficits or native diagram
sums and does not introduce an ancestor-index compatibility layer.

Replacement preserves Never/OptIn/Always and Reject/Accept/Enforce selection,
including OptIn replacement of non-signaling v3. Native v31 cluster resource and
feerate-diagram rules remain the economic replacement test. Non-TRUC parents
retain Roots' individual relay floor; inherited CPFP/ephemeral expectations are
updated to that source contract, with zero-floor coverage retained separately.
TRUC Reject and script reuse checks still apply during reorg admission, matching
the source legacy-bypass set. The RBF contradiction warning is emitted once at
startup validation rather than once per options reconstruction.

Reuse tracking uses full scripts and sets of all creator/spender claims instead
of a single hash-keyed pointer. It rejects twin outputs, input/output reuse,
ancestor conflicts and repeated staged claims, and considers independent
mempool script conflicts under native replacement rules. Staged package reuse
was an untested source gap; rejecting it closes the same local rule over native
staging. Tests cover allow/conflict, fee-based policy replacement, block removal,
reorg behavior and removal of one of multiple tracked claims. This remains a
local script rule without ownership or privacy inference.

Provenance: `1b73fe3d21`, `ec9db369f5` supply graph-policy behavior;
`aa27c9117a`, `48f3b3bbe8`, `a6f35e915c` supply corrected RBF modes/reporting;
`fc0dc945d0` and `10d772eb7f` supply sub-dust, carrier and interaction evidence.
Scoped parsing from `87c950de92` is consumed without widening its grammar.
Source attribution remains Jaromil and devjoinedthechat with retained
Core/Knots lineage. Dynamic publication commits remain L8.2-owned.

## L8.2 dynamic dust lifecycle repair

The source constructor asserted the presence of an unwired scheduler and
registered callbacks capturing a mempool that chainstate retries could destroy.
The authorized repair registers one node-owned task after the final successful
load, updates immediately and at the source 15-minute cadence, and stops/joins
the scheduler before destroying the final mempool or estimator. Interrupted or
failed load attempts register no dust tasks. Live dust getters and standardness
snapshots synchronize publication with admission, ephemeral checks and wallets.
Resident sub-dust deficits remain immutable; actual fees and rewards are unchanged.

Target mode uses the initialized long horizon with source success threshold 0.8.
Mempool mode traverses native chunk/topological order to the decimal-kvB policy
vsize position and takes the boundary entry's actual fee rate. It retains no
ancestor-score compatibility index. Missing/zero estimates use the configured
floor. Thousandths multiplier evaluation quantizes to sat/kvB, rounds down and
saturates at INT64_MAX without overflowing intermediate products. Default off
and parser grammar/units are unchanged. This repairs source lifecycle, fallback
and arithmetic gaps rather than claiming that dormant source modes worked.

Restart coverage exposed another source gap: the persisted fee delta included
the admission dust deficit, causing reload to subtract it twice. DumpMempool now
serializes copied operator deltas only; reload computes one new admission deficit
at its live rate. The regression preserves a nonzero operator delta across
restart and observes unchanged accounting at the configured floor.

The default full build also exposed the accepted L7 script-size policy global
being defined in bitcoin_node while the standalone wallet links bitcoin_common.
Its definition now lives with common standardness; declaration, default, runtime
assignment and consensus behavior are unchanged.

Current evidence is retained under `.gestalt/l8-policy`: full native units/CTest,
the default headless functional suite, a separate Clang ASan/UBSan daemon and
focused unit run, arithmetic/graph fuzz campaigns and exact binary/source/log
hashes. Dynamic runtime coverage includes all modes, extreme grammar values,
absent and warmed real estimators, periodic updates, floor fallback, immutable
deficits, production reindex retry, restart, shutdown and concurrent publication.
Source synchronization/fallback contracts remain the provenance reference;
Jaromil's `784b2af17a`, `f74cc0433b` and `73c97862e2` supply the synchronized
publication, changing-rate test and standardness-snapshot source history.
L8.1's conservative representable-case graph restrictions remain explicit in
[sub-dust semantics](feature-contracts/subdust.md). L9 mining combinations and
L12 integrated runtime configuration remain their respective owners.

## L9.1 priority selection and residual native chunks

Local mining defaults are Roots' 1,200,000 weight units, 1,000 sat/kvB and
100,000 serialized-byte priority area. Size-only and weight-only settings
leave the other resource at its protocol maximum; both settings enforce both.
Serialized size reserves 1,000 bytes as on the source; native weight reserve
normalization and additional coinbase sigops remain intact. Resource equality
is excluded by the inherited conservative template boundary.

Coin age is evaluated from the active UTXO set under the chain and mempool
locks, using the source's modified-vsize input discount. This deliberately
replaces entry caches and confirmation/reorg repair indexes: formerly
unconfirmed inputs start aging when present in the active UTXO set. Parent
readiness, priority/individual actual-feerate/descending-txid ordering and the final
area/threshold-crossing transaction follow source behavior. No priority delta
API is added; the existing mining RPC changes fee deltas only.

After a nonempty priority prefix, a temporary native TxGraph contains exactly
the remaining entries and direct dependencies. Already included parents and
their policy fees/weights are absent, so each descendant's native residual
chunk has accurate resource and fee accounting. It retains configured cluster
bounds, native bounded optimization and L8's conservative aggregate signed
budgets. This costs O(mempool) temporary storage plus native cluster work;
priority-disabled or empty-prefix templates use the existing graph directly.
Builders have local RAII ownership and are destroyed before residual refs and
graphs. Template assembly does not mutate or stage the live mempool graph.
Chunks are included/skipped whole, in native dependency order. Actual
serialized sizes and sigops are checked alongside native policy weight;
entries are asserted unique and actual fees alone fund the coinbase reward.

Provenance: priority/template content from `1b73fe3d21`, `ec9db369f5`,
`875fa15829`, `9ab60f217b`, `9d9c125d9e` and joint fixtures from
`10d772eb7f`. Retain Jaromil/devjoinedthechat credit and inherited Core/Knots
lineage. L9.2 owns the whole joint policy/mining gate and split-test accounting;
only integrated `confrw` remains deferred to L12.

## L9.2 split coverage and consensus separation

The source L5 commit `10d772eb7f` adds twelve functional subdust methods; every
method is present and invoked in the candidate. Ownership is reconciled here:

| Source methods / coverage | Delivered candidate gate |
| --- | --- |
| `test_dust_output_increases_required_fee`, `test_partial_dust_proportional_penalty`, `test_multiple_dust_outputs_stack`, `test_penalty_disabled`, `test_op_return_not_penalized`, `test_above_dust_not_penalized` | L8 admission/accounting boundaries, retained in the joint subdust functional run. |
| `test_threshold_and_low_rate_boundaries`, `test_anchor_and_standardness` | L7 dust/standardness plus L8 rounding; retained zero/low-rate and local-reject/valid-block gates. |
| `test_carrier_cost_interaction` | L8 independently charged carrier weight and deficit; L9 residual odd policy weight and exact descendant fee/resource boundaries. |
| `test_fee_delta_and_replacement` | L8 actual/modified fields and replacement fee diagrams; L9 residual delta one-sat boundary and priority tie independence from operator deltas. |
| `test_package_and_truc` | L8 ordinary-parent relay floor, native sponsorship, TRUC mode selection and modified package fields; joint package/RBF/TRUC scripts run with L9 assembly. |
| `test_mining_and_block_acceptance` | L9 priority on/off, minfee equality, penalty disabled, fee delta, actual rewards under both selection modes, and local-reject/valid-block acceptance. |
| Source parser `policy_args` and arithmetic/admission `subdust_fee_penalty` fuzz | L7/L8 independent grammar/arithmetic oracles retained; current bounded L9 sanitizer smoke plus native pool template option fuzz. |
| L8 dynamic runtime/publication/estimator/retry/reload and immutability | Existing functional cases retained, with current template fee fields and mined rewards checked after publication and operator-delta persistence. |
| Source priority-parent/stale-ancestor mining corrections | Native one/two-parent, shared/waiting descendant, repeated-build, exact remaining fee/weight/size and duplicate/order regressions. |
| Source integrated Roots `confrw` precedence subsection | Only split-policy/mining integration deferral; L12. Ordinary CLI/config/JSON startup cases remain active. |

Synthetic residual accounting tests combine a priority-selected negative-modified
parent with a carrier-adjusted descendant. The descendant meets an exact
rounded-vsize floor, misses it after a one-sat operator delta, and respects the
remaining policy-weight boundary. Actual parent/child fees and coinbase rewards
are checked separately. Pool fuzz assembly varies priority enabled/disabled,
serialized size, weight, reserve and sigop reservations before block removal
and reorg admission. These are short smoke checks, not exhaustive campaigns.

Mining's synthetic fixtures now lock cs_main before mempool, matching production
lock order. Fee-only ephemeral and estimation fixtures explicitly disable the
source priority area; the estimator retains its native low-feerate sampling
with an explicit template fee floor. The disconnected-pool bulk fixture requests
the source's protocol-max template weight and FIFO fee ordering. Assertions
are retained. Sequence-lock fixtures use fee selection, and large-block upload
accounting requests the source size-only profile. The general mining test checks
the Roots default weight/fee limits and explicitly requests protocol-max weight
for its separate reserve-boundary case. Roots defaults remain covered by the
dedicated template matrix.

`validation.cpp` from `bool CheckBlock(` through end-of-file is byte-identical
to the authenticated v31.1 base. The consensus subtree, interpreter, block
primitive and kernel chain files have no candidate changes. This L9 diff touches
only local template selection/configuration plus tests/docs and a read-only
mempool builder accessor. No block-size/weight acceptance gate reads Roots local
template limits. Functional and unit tests add transactions excluded by local
policy/templates to valid blocks and accept them through the native interface.
No RDTS/BIP110 or other new consensus rule is present.

The per-contract/source-commit accounting above remains complete (72 commits,
25 contracts) and retains Jaromil/devjoinedthechat plus Core/Knots attribution.
Within the L7–L9 policy/mining split no parsing-only mining controls or unowned
source interaction cases remain. Optional platform/dependency gates remain
explicit environment gaps; heavy/nightly CI is L13.2, not a preservation claim.

The L9 acceptance gate includes 751 GCC unit cases, 154 CTest passes plus the
external script-assets skip, and 274 functional passes plus 17 environment
skips. One frozen functional sweep exposed six inherited fixture assumptions;
only those fixtures were corrected and rerun against the same daemon. CTest
launches interrupted by a unit-binary relink were rerun after the build finished.
The current miner cases also passed separately after the lock-order fixture
correction. Scoped Clang ASan/UBSan checks cover 44 policy/miner unit cases and
the dynamic/subdust template-reward interactions. The existing template matrix
sanitizer evidence retains the same production source and daemon hash. Parser,
penalty arithmetic and randomized native pool assembly have bounded sanitizer
fuzz smoke coverage. No repeated full sweep or exhaustive fuzz claim is made.

## L10.1 peer resource and permission controls

Compact reconstruction retains 32,768 extra slots by default with a separate
10 decimal MB transaction dynamic-usage budget. Zero/negative disables caching;
malformed fixed-point memory settings retain the source default. Four decimal
places of MB represent 100-byte increments. Slot allocation is capped from the
memory budget, avoiding an extreme count-only allocation. Eviction occurs
before addition to avoid size_t overflow on 32-bit platforms. The source's
100,000-byte individual dynamic-usage exclusion now covers replacements too,
matching the stated contract. Native v31 compact-block reconstruction is unchanged.

ForceInbound implies NoBan/Download. Its random fallback considers only inbound
ban-able peers, after unconditional NoBan/outbound protections; ordinary eviction
retains native scoring. Explicit blockfilters/compactfilters/cfilters permission
requires the basic index and grants native BIP157 services to that connection;
All retains the source non-explicit exception. Legacy whitelist defaults include
Addr; outbound ranges apply to automatic and manual connections. Bloom is
unadvertised globally by default and permitted on loopback unless explicitly
disabled. Onion-specific and first-bind Tor targets suppress address whitelist
permissions, including implicit loopback bloom.

Subnet disconnect uses v31's existing CSubNet overload and native RPC argument
views. Clearnet-v2-only suppresses v1 initiation and fallback for the source
routable IPv4/IPv6 classification; loopback/non-routable and encrypted overlay
networks retain source eligibility. Tests use a local forwarding SOCKS server
for routable targets and authentication, with no external traffic. Credentials
are not included in the proxy debug message. Fee filters default on with the
source explicit off switch. Fee-filter buckets retain the source 100 sat/kvB
seed below 1,000 sat/kvB initialized incremental fee and 1,000 otherwise;
both profiles and the IBD exit regression are tested. Duplicate CJDNS manual addresses ignore ports;
local-address scores saturate, and duplicate manual block fetch rejects before
resetting in-flight tracking. The source's nullable FetchBlock overload has no
production caller; v31's typed existing-header interface remains sufficient.

Native v31 already carries network-key inventory timers/address-cache grouping,
inactivity checks, reconnection cleanup, orphan resource controls, protected
outbound/manual peers and safe subversion handling. Their upstream regression
assertions remain. No new wire message/inventory type or filtered-witness-block
extension is introduced. These are local controls over existing P2P interfaces.
Peer-health fields and Qt evidence remain L10.2.

Provenance: mixed peer controls from `1b73fe3d21`, `ec9db369f5`; compact and
permission fixtures from `fc0dc945d0` plus their feature-owned corrective history
in the complete mapping. Retain Jaromil/devjoinedthechat and Core/Knots credit.

### L10.2 peer-health adaptation

The source current-connection fields travel through native `CNodeStats`,
`CNodeStateStats`, `getpeerinfo` and the existing GUI node interface. Thread CPU
measurement covers the message-processing/send pass, not historical samples.
Unavailable or backwards CPU readings are ignored, and RPC omits load unless
CPU time and connection duration are positive. Native v31's deprecated
`startingheight` RPC gate is retained rather than reversing upstream interface
changes. The source Starting Block detail uses the still-available native state
field and resets to Unknown when unavailable. Deprecated misbehavior score remains source-compatible at 0 or 100.

The peer detail pane clears state fields before each refresh, and clears the
hidden BIP324 session ID when switching transports. The model resolves peer data
by current row because refresh swaps its storage; persistent selection cannot
keep a pointer into a discarded snapshot. Native table columns and sorting are
preserved. Focused GUI tests can connect to caller-supplied real regtest nodes,
exercise sorting/refresh/disconnect selection and the production unavailable
try-lock branch, and capture the rendered window at the specified font/resolution
matrix. No static mockup or history collector is introduced.

These adaptations preserve the catalogued Roots contracts and attribution to
Jaromil and devjoinedthechat alongside the Bitcoin Core and Bitcoin Knots
provenance already recorded above.

## L11.1 wallet foundations and per-send RBF

The descriptor/SQLite and external-signer boundaries retain the native v31
wallet API. Roots database enumeration excludes node database directories and
avoids descending through symlinks/reparse points. SQLite opening probes directory
write access before opening an existing database, retaining v31 locking/error
handling; all 19 source SQLite warning/error modernization lines already exist
upstream. No legacy database build support or new backup/flush promise is added.

External signer invocation keeps v31's explicit argument vector and closes
unrelated inherited Unix descriptors after redirecting standard streams, retaining
the exec-error pipe at FD 3 with close-on-exec set. `close_range` closes all higher
descriptors when available. Its Linux fallback reads `/proc/self/fd` using raw
`getdents64` into a fixed stack buffer: it enumerates actual descriptors, including
those above a subsequently lowered soft limit, without post-fork heap allocation
or scanning a numeric descriptor limit. Other Unix builds use a detected native
`closefrom` fallback. If no safe closure mechanism succeeds, the child reports an
error through the retained pipe and exits before executing the signer. Non-Linux
fallback runtime behavior is an untested platform gap; unsupported fallback
platforms fail closed. The unsafe source RLIMIT-based fallback is replaced;
source process poll/kill APIs remain unported. The current Windows
CreateProcess implementation does not provide equivalent inherited-handle
isolation; Windows handle, permission and reparse behavior remain explicit gaps.

The Send dialog takes its initial RBF choice from the selected wallet's
`walletrbf`, retains native explicit coin-control overrides and reports signaling
from the prepared transaction even when no fee is charged. Saved PSBT tests decode
both signaling choices. Replacement acceptance remains dependent on network policy.
Automatic anti-fee-sniping locktimes avoid the source CAT21 policy marker; explicit
caller locktimes and consensus validity are unchanged. This ports the spending
safeguard associated with the accepted local-policy rule.

Focused candidate coverage exercises database-directory write rejection, skipped
node databases and symlink traversal; descriptor wallet/send and external signer
behavior; child descriptor closure plus preserved standard streams and process
errors; both wallet RBF defaults, explicit overrides, confirmation text and PSBT
sequences. Sweep authorization, coin-control facts and the rendered interaction
matrix remain L11.2. Attribution remains Jaromil and devjoinedthechat alongside
Bitcoin Core/Knots provenance.

L11.1 focused evidence: 34 GCC unit cases in system/filesystem/wallet/spend and
wallet database/load/PSBT suites; four functional profiles (wallet signer,
multiwallet RPC and CLI, wallet send); 21 Qt6 test results, including the RBF
confirmation and decoded PSBT assertions; and the same 34 focused unit cases in
Clang ASan/UBSan under a 60-second runtime bound. Leak detection was disabled
(`ASAN_OPTIONS=detect_leaks=0`); undefined behavior halts on error. This is bounded
sanitizer coverage, not a leak or exhaustive race proof. A Qt-only fixture reset
was corrected after its first run and the Qt suite rerun; production and headless
results stayed hash-matched. Qt5 and the visual matrix remain L11.2. The full
applicable candidate gate remains the L11 acceptance gate.

L11.1 signer review corrected the source fallback's soft-limit assumption. A new
regression opens a descriptor at 123 or higher before lowering the child's soft
limit to 32, forces fallback and checks descriptor closure, standard streams,
retained exec-error pipe and its close-on-exec flag. All three current signer
system cases pass under GCC and bounded Clang ASan/UBSan; `wallet_signer.py`
passes again against the corrected daemon. Unaffected wallet/database/RBF and
Qt6 evidence retains its original source/fixture and binary-hash provenance.
The combined focused gate now covers 35 distinct unit cases, with the three
signer cases replacing the original two; it is not a claim that every retained
case was rerun against the new binary. The full acceptance gate is still L11.

### L11.2 — sweep and coin-control authorization

The v31 port carries transient-key WIF sweep through shared native UTXO scanning,
RPC and Qt, without importing supplied keys. Preview remains the default. The
GUI requires a current preview, default-Cancel confirmation and an exact rebuilt
transaction-ID match. Atomic cancellation/final submission transitions prevent
accepted cancellation from later broadcasting. Worker disposal clears secure
storage and catches interruption errors; complete Qt/host memory erasure is not
promised. Fee convergence uses one locked native estimate/fallback snapshot.

Source review found an inherited destination defect: cached-script ownership
accepted an incomplete local-key multisig descriptor. A disposable source-build
probe confirmed accepted preview but incomplete wallet-only spending. Both v31
entry points now share a native signing-satisfaction check. Cached public
descriptor expansion maps available roots, including encrypted keys, to derived
public keys without private-key retrieval/export or unlock. An availability-gated
dummy creator uses native script satisfaction for multisig, Taproot alternatives
and MuSig participants. Missing 2-of-2 keys are rejected; a sufficient 2-of-3
subset remains eligible. Timelock maturity and future network acceptance remain
separate from local signing capability.

Coin-control preserves native availability/selection and PSBT behavior while
reporting effective value, input size, lock, watch-only, reuse/group, fee/change
and actual transaction RBF facts. Same-balance wallet notifications invalidate
selection. Horizontal scrolling retains readable columns at large fonts; summary
and controls fit the bounded visual profiles. These are facts, not privacy scores.

Focused final verification covers 91 cases across all affected wallet, shared
script/descriptor, key, RPC and process suites; six functional profiles (sweep,
send, multiwallet RPC/CLI, signer and scantxoutset); the complete Qt5 and Qt6
test suites (21 result rows each, including initialization/cleanup); and 48
bounded Clang ASan/UBSan cases with leak detection disabled. After a recursive
lock annotation correction, all 91 unit cases and all six affected functional
profiles were rerun against the frozen final binaries.
Eight actual Qt5/Qt6 profiles at 1280x800 and 1920x1080 with 100%/150% font scale
produced 80 captures. These unchanged-UI captures retain their original binary
hashes; both Qt suites passed again with the final destination correction.
Native confirmation captures preserve a prior test's background shutdown window
and synthetic regtest fee-estimator limitations. Linux/Xvfb evidence does not
establish Windows/macOS rendering, physical-DPI scaling, complete secret erasure
or exhaustive races. The full applicable L11 acceptance sweep remains the next
root-directed gate. Credits remain Jaromil, devjoinedthechat, Core and Knots.
