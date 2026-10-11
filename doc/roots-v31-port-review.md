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

Retain the seven-theme target: **7 instead of 72 commits**. Historical fixes,
tests and documentation travel with the behavior they correct; release/CI work
travels with maintenance, rather than recreating chronological fixup commits.
No broad compatibility layer is proposed.

| Theme | Owning scope and prerequisite |
| --- | --- |
| L6 build/portability | Dependency probes and supported switches; contract documents and this review. Establishes the foundation only. |
| L7 local policy/mining | Admission, replacement, carrier/dust/coinage/reuse, strict parsing, mining, interaction/fuzz coverage. Requires L6. |
| L8 peers | Resource/permission controls and current-connection health; GUI/functional coverage. Requires L7 and L6. |
| L9 wallet | Database/signer boundaries, per-send RBF, sweep authorization/secrets and coin-control facts. Requires L8. |
| L10 operator/runtime | Read/write config, effective-policy RPC and platform resource support; dormant stats remain dormant. Requires L7 state and L9. |
| L11 integration | Identity/icons/QR/taskbar, generated help artifacts, CI and release/export tools. Requires L10. |
| L12 evidence | Final preservation review, range comparison and exact-candidate supported gates. Requires L6–L11. |

The largest adaptation is L7: v31 uses `TxGraph` clusters and chunk-based mining,
not the old ancestor-index selection model. L9 carries distinct security
boundaries but its L2 divisions separate foundation from sweep/coin control.
The old-to-upstream aggregate wallet/Qt diff includes thousands of unrelated
upstream lines and is not a port-size estimate. No implementation has established
that these themes need a split yet. Reassess the actual proposed diff and
feature-owned tests before L7/L9 execution; revise milestones first if their
independent risks cannot be reviewed safely in one thematic commit. Seven is
a target, not permission to hide unreviewable coupling.

`runtime-config` is an L10 implementation dependency of L7 parsing and L9 sweep.
Until then, feature tests must cover Core CLI/config semantics and explicitly
defer Roots `confrw` precedence cases; they cannot claim those cases passed.
The full precedence gate runs when L10 lands. Feature-local tests still ship
with each implementation; L12 is additional aggregate evidence.

## Contract dispositions against actual v31.1 APIs

These are implementation directions for review, not completed-port claims.
**Adapted** retains all intended behavior while changing Core-specific placement;
**kept** retains compatible behavior/support; **adopted-upstream** needs equivalent
defaults, configuration, invariants and current acceptance evidence. No intended
behavior is proposed **dropped**. Sub-aspect equivalence cannot retire a whole
contract with remaining Roots behavior. Existing source gaps remain explicit.

| Contract | Disposition / theme | Upstream comparison, adaptation and acceptance gate |
| --- | --- | --- |
| policy-parsing | adapted / L7 | `node::ApplyArgsManOptions` now consumes cluster limits; Roots policy parser and grammar are absent. Preserve L2's complete-token/range/diagnostic decisions, quarter-vbyte rounding and CLI/config precedence. Port parser unit/startup tests and L5 fuzz boundaries; complete read/write precedence in L10. |
| effective-policy | adapted / L10 | `getmempoolinfo` has cluster-era size/fee information but no Roots `policy` object. Read authoritative initialized v31 state plus synchronized dynamic dust; retain L3's schema, units and enforcement gates. Port `rpc_effective_policy.py`, help and admission parity assertions. L4 supplied no snapshot refactor to carry. |
| policy-standardness | adapted / L7 | Core `IsStandardTx` takes carrier budget, bare-multisig flag and dust rate. Core permits bare multisig by default and lacks Roots configurable script/witness, strict sigop, CAT21/token and ephemeral modes. Preserve Roots limits/defaults/reasons solely in local gates. Run transaction/sigop/TRUC cases and valid-block fixtures; do not change script consensus. |
| policy-carrier | adapted / L7 | Core defaults to a 100,000-vbyte OP_RETURN budget, not Roots' aggregate 83-byte budget, and lacks recognized input/witness carriers and cost weighting. Adapt detection/extra policy weight to `TxGraph` fee/weight accounting. Retain Roots `multi-op-return` rejection despite source help conflict. Port `mempool_datacarrier.py`, extra-weight boundaries, tapscript and L5 dust-cost interactions. |
| policy-dust | adapted / L7 | Core retains `GetDustThreshold` and a 3,000 sat/kvB floor, but has a fixed initialized rate and no `dustdynamic` publication. Retain scheduler, estimator/floor behavior and synchronized snapshots; adapt mempool-position estimation to clusters. Run `mempool_dust.py` and changing-publication unit cases. Estimator-unavailable and extreme-multiplier gaps are not passes. |
| policy-subdust | adapted / L7 | No upstream deficit penalty. Preserve worked contract units, saturation, exemptions and modified-fee accounting without changing actual fee rewards. Adapt policy weight/modified fees consistently through cluster admission, replacement and mining. Port sub-dust tests and L5 arithmetic fuzz/valid-block cases. |
| policy-replacement | adapted / L7 | Core has unconditional full RBF and cluster feerate-diagram replacement, with enforced TRUC; Roots Never/OptIn/TRUC mode controls are absent. Default full RBF alone is not equivalence. Preserve configured modes, v3 opt-in behavior, fee/resource gates and truthful `fullrbf`; adapt to current replacement algorithms without restoring ancestor indexes. Run RBF/TRUC/package/sibling and policy-block tests. |
| policy-coinage | adapted / L7 | Relay coin-block age and maturity controls are absent. Reuse available coin heights; add local gates and bounded age arithmetic, never alter consensus coinbase maturity. Preserve disabled defaults and add/retain enabled, boundary and valid-block acceptance cases; prior dedicated relay coverage remains a gap. |
| policy-reuse | adapted / L7 | No upstream `spkreuse` creation/spend tracking or conflict mode. Preserve default allow and explicitly configured local tracking across admission, replacement and reorg. Dedicated functional mode/reorg coverage is still required before claiming equivalence. |
| mining-template | adapted / L7 | `BlockAssembler::addChunks` replaces ancestor-index assembly. Core's default weight is 4,000,000 with reserved-weight handling and minimum fee 1 sat/kvB, not Roots' 1,200,000 weight / 1,000 sat/kvB / 100,000-byte priority area. Preserve size/weight normalization and coin-age priority while avoiding duplicates and charging ancestor/chunk resources correctly. Port miner boundaries and L5 priority/fee/block-acceptance cases. |
| peer-controls | adapted / L8 | Core compact reconstruction extras default to 100 entries, with no Roots 10 MB memory bound; orphanage is `node::TxOrphanage`, not the former pool layout. Core whitelist/upload/BIP permissions do not establish forceinbound, explicit compact-filter permission, subnet disconnect or clearnet-v2-only equivalence. Preserve each documented default, malformed-input bound, Tor-bind isolation and SOCKS secret boundary; run extra-tx, permissions, filter, eviction and invalid-message tests. |
| peer-health | adapted / L8 | Core has current-peer stats/table/details but lacks the complete Roots extra fields and unavailable-state contract. Use existing refresh/interfaces; port stale-detail reset, sort/selection and bounded CPU reporting. Run `rpc_net.py`/Qt apptests and required resolution/font-scale matrix; no collector/history feature. |
| wallet-foundation | adapted / L9 | Descriptor/SQLite and external signing remain upstream. v31 database enumeration still recursively traverses directories; Roots directory/reparse/permission safeguards remain necessary. `RunCommandParseJSON` now takes an argv vector instead of a shell command string; preserve child-FD closure on this API. Port multiwallet/signer/unit cases, retaining no-BDB-build and locking boundaries. |
| wallet-rbf | adapted / L9 | Core has a per-send checkbox and sequence override, but initializes the checkbox checked rather than from wallet `walletrbf`. A similar UI is insufficient. Preserve wallet-derived preference, explicit overrides, confirmation and PSBT signaling tests. |
| wallet-sweep | adapted / L9 | No Core `sweepprivkeys` RPC/dialog. Adapt wallet fee/coin APIs and cancellation through interfaces; preserve WIF/script scope, selected local-key descriptor destination, no import, preview default, locked fee snapshot, transient secure cache/redaction and final broadcast authorization. Run sweep functional/Qt/security-structure tests plus runtime cancellation and visual evidence. |
| wallet-coin-control | adapted / L9 | Core retains ordinary coin-control/listLockedCoins; it lacks Roots effective/reuse/grouping facts and full invalidation contract. Adapt current wallet output/input-size APIs, preserve visibility/selection filters and conspicuous change validation. Run wallet-send/Qt selection/invalidation/source checks and populated GUI matrix. |
| runtime-config | adapted / L10 | `common::MergeSettings` contains forced/CLI/JSON/config sources, but no Roots read/write text configuration layer or `confrw`. Insert the existing layer and typed writes into current ArgsManager without a duplicate policy store; preserve network-file/default/negation/list rules. Run getarg/config functional and deferred L7 precedence cases. |
| operator-stats | kept; uptime adopted-upstream / L10 | `uptime` still returns `TicksSeconds(GetUptime())`; `GetUptime` uses `SteadyClock` initialized at app startup. Its function/schema/clock/defaults are identical in relevant source; upstream adds a wall-clock-jump regression. Both the exact pinned-source acceptance test and current upstream `rpc_uptime.py` pass on the authenticated base. Independent review accepted this narrow sub-aspect, not retirement of the whole contract. Core has no Roots stats model/RPC. Keep the retained stats source dormant: do not register, sample or advertise `getmempoolstats` without a separate compatibility decision. Source GUI/default/help conflict remains a gap. |
| runtime-resources | adapted / L10 | Core has `GetTotalRAM` and RAM-aware cache sizing, so helper-name similarity cannot retire Roots sizing semantics. Compare exact cache thresholds/units and failure defaults before sub-aspect retirement; preserve Roots byte-safe allocation, FD bounds and platform-scoped advisory helpers where needed. Run RAM/FS/util tests and actual supported-platform checks; unused helpers are not a cross-platform service. |
| build-portability | adapted / L6 | Core remains CMake/C++20 with wallet/tests enabled, GUI off, embedded LevelDB/secp256k1, optional signer/QR and IPC. It raises GCC/Clang minimums and supplies Qt6-only CMake targets. Carry Roots Qt5/6 selection, required system-library usability probes and native crypto flags on these APIs; missing optional dependencies are explicit gaps. A declared Tor-subprocess switch has no source consumer and must stay a non-feature, not become an automatic launcher. MiniUPnPc build support must not imply a missing runtime consumer. |
| identity-desktop | adapted / L11 | Core version/icons/help remain Core; Roots identity, QR-PNG export and Windows taskbar support are not equivalent. Port limited identity/desktop changes while preserving upstream names/protocol attribution. Generate manuals/config from final help only; run version, Qt QR/icon and Windows/platform/visual gates. |
| maintenance-ci | adapted / L11 | Core's v31 compiler/platform/cluster fixtures supersede old job assumptions; Roots classifier, exact promotion proof and platform/fuzz scheduling need adaptation. Retain actual exact-candidate CI gates and bounded corpora scheduling; run maintained tool tests and supported candidate profiles. |
| maintenance-release | adapted / L11 | Upstream does not provide Roots immutable source/tag/assets, signed new-draft-only five-platform release, portable export or contributor-credit rules. Adapt version/base validation and replay without publishing/tagging. Retain `.github` exclusions and `git am -3 --keep-cr`; test temporary export/replay and all release tools. Expanded notices remain deferred. |
| maintenance-docs | adapted / L6,L11,L12 | Carry accepted contracts first with explicit deferred status, reconcile catalog/privacy/provenance to actual candidate, generate help artifacts when interfaces land, and complete review evidence last. Historical release notes are provenance, not a v31 feature claim. Check links/doc lint/whitespace and generated-output parity. |
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
| `5c210e17fb` | L6 build/dependency support; L11 platform CI/release integration. Adapt required probes, not old generated summaries. |
| `85257a0986`, `724f8c2ee7` | L7 policy utility dependencies; L8 peer diagnostics; L9 signer boundary; L10 config/runtime/stats support. Split mixed runtime content by contract; preserve dormant status. |
| `1b73fe3d21`, `ec9db369f5` | L7 policy/mining/dust/replacement and broad functional fixtures; L8 peer controls; L9 wallet boundary fixes/fixtures; L10 runtime dependencies; L11 CI test-script integration. Adapt actual surviving deltas to clusters/interfaces. |
| `dd03dcb989` | L9 database/SQLite/signer hardening, adapted. |
| `c592888088` | L6 Qt build support; L8 peer view; L9 wallet GUI support; L11 identity/QR/taskbar. Split mixed GUI content without importing incidental upstream text. |
| `fc0dc945d0` | L7 feature-owned dust/sub-dust/sigop/TRUC tests, plus L8 peer fixtures and L10 operator tests where present. Preserve tests' actual feature ownership. |
| `9f1780f330`, `4570a6a547` | L6 necessary build registration; L11 assurance/CI/release tooling and fixtures; owning-feature tests across L7–L10. No replacement maintenance state system. |
| `9da5851410`, `26b691e4f3`, `201927c151`, `e819a07a4a`, `56e2894760` | L11 CI scheduling, apt-retry, optional-feature selection and branded archive checks; adapt to v31 jobs. |
| `83fa42a963`, `f5182f0e3a`, `7791eede50`, `11c84c39ef`, `0efa437e29`, `f2595557d9`, `38bc7ec2ce`, `df8aa8e2b7`, `ddae467a78`, `778ff50d19`, `c474825f96`, `17e1d484ab` | L11 release/rehearsal/source/archives/signing/new-draft/promotion CI and notice deferral; preserve final safeguards, fold corrective history. `f5182f0e3a` also supplies L9 sweep/privacy boundary tests and L10 safe-empty-stream writes/tests, plus L11 apt/sanitizer checks. No releases dispatched. |
| `25f889f97e`, `3e13c1f213` | L11 Windows icon and QR QString-size corrections, kept/adapted with identity. |
| `b0b235d6a1`, `f4066e5282`, `30f673a4bc`, `a13f28e2f4`, `1517d925b2`, `628bdf4e38`, `02464df449`, `089888acdd` | L6/L11/L12 catalog, scope, historical notes and provenance; rewrite candidate claims for actual implementation, retain relevant history/credit. |
| `bbd7339af2`, `9b778b9a23` | L8 peer stale-state fix/apptests; L10 streams assertion fixtures and L11 QR/build GUI assertions from the mixed test commit. |
| `e4b26462de`, `0e3ae72d89` | L9 wallet-derived per-send RBF and locked Qt assertions, adapted. |
| `5e2d79e937` | L7 `node/mempool_args.cpp` string-view prefix modernization, adapted only where the parser remains; no consensus feature. |
| `fb21288879`, `dc1b217cbd`, `9a216bc389`, `7c70374050`, `f3cd7da535`, `96fecfd411`, `a1748ccc64`, `f9e9a36cd1` | L9 sweep RPC/GUI, coin-control facts, documentation, asynchronous authorization, locked fee snapshot and secret cleansing. Fold security corrections into final feature. |
| `875fa15829`, `9ab60f217b`, `9d9c125d9e` | L7 priority-selected ancestor/resource correctness and locale-independent regression fixtures, adapted to chunk mining. |
| `aa27c9117a`, `48f3b3bbe8`, `a6f35e915c` | L7 configured RBF/TRUC semantics and tests; L10 truthful RPC field; L11 final generated help. |
| `30e7eac266`, `12d67ba60a`, `e6d4abc704`, `085a84a7cb`, `8cb07ef1b9` | L7/L8/L10 feature-owned full-suite, Taproot-carrier, fee-estimator and startup fixtures; adapt to current APIs without erasing upstream regressions. |
| `1f7cfce176` | L11 regenerate manuals/example configuration from final help, not source-byte replay. |
| `16b2198275`, `47329c00dd`, `1b2431c74c` | L6 native crypto flags, exact MiniUPnPc dependency patch and usable system LevelDB memory environment; the Chain interface include and mempool/1p1c lint fixtures follow L7/L8. |
| `784b2af17a`, `f74cc0433b`, `73c97862e2` | L7 synchronized dynamic-dust publication/snapshot and changing-rate tests; consumers in L10 report the same authoritative state. |
| `f66eca4ae4` | L6 accepted stable contract collection and source/provenance reconciliation. |
| `87c950de92` | L7 strict policy grammar, scoped help/diagnostics and parser/startup tests; distinct source behavior change. |
| `b60a6e04d0` | L10 additive effective-policy RPC/schema/parity tests; distinct source API change. |
| `10d772eb7f` | L7 valid-block/carrier/penalty/priority interactions and parser/arithmetic fuzz boundaries; L6 retains documented gaps until tests are ported. |

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
