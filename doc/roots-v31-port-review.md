# Roots Core v31 port review

This is a preservation review for a review candidate, not a release
announcement or an executable patch manifest. The accepted 30.3 contracts define
intent; Git defines each version's implementation. Nine accepted thematic
commits now carry the implementation on Core v31.1. The stage reports below
retain their original scope and evidence limitations; their pending wording
does not supersede later accepted results. The [series audit](#l151-repaired-series-audit)
records exact history, contract coverage, compatibility decisions and closed
L14 repair findings. L15 is documentation acceptance only. The approved L16
repairs and reduced candidate gates, followed by independent terminal review,
remain pending; the draft PR is a review boundary, not a release.

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

The user approved an eleven-theme target: **11 instead of 72 commits**, including
the bounded L14 preservation repair, the documentation-only tenth L15 commit
and the separate eleventh L16 test-oracle/hosted-diagnostics repair commit. The former
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
| L14 repairs | Qt5 persistence, configured benchmark default and safe empty AutoFile writes. Requires L6–L13. |
| L15 evidence | Preservation ledger, range comparison and honest diagnostic/provenance accounting; documentation acceptance only. Requires L6–L14. |
| L16 diagnostic repairs | Two test-oracle conversions, hosted failure-log visibility and final candidate gates under the approved reduced verification. Requires accepted L15. |

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
with each implementation; L15 is additional aggregate evidence.

## Contract dispositions against actual v31.1 APIs

These are the reviewed implementation placements; the current coverage table
below records candidate evidence and remaining platform limits.
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
| maintenance-docs | adapted / L6,L13,L15 | Carry accepted contracts first with explicit deferred status, reconcile catalog/privacy/provenance to actual candidate, generate help artifacts when interfaces land, and complete review evidence last. Historical release notes are provenance, not a v31 feature claim. Check links/doc lint/whitespace and generated-output parity. |
| maintenance-native | adapted / owning theme | Inspect every surviving source hunk; keep behavior-preserving C++/empty-write/test hooks only where needed on v31 APIs. Port test fixtures with their features rather than overwriting newer Core tests. Transient reverted script work is not a new consensus feature. Run affected unit/functional/fuzz gates. |

The shared consensus-neutral and no-RDTS boundaries apply to every row. Contract
dependencies from the accepted collection remain in force. No retirement of
Qt5, optional system libraries, dormant support or intended operator behavior
is authorized by a missing local dependency. If a required support path cannot
be preserved on v31, seek an explicit compatibility decision before removing it.

## Complete source-to-theme accounting

Every source object below resolves in the pinned source range.
Rows assign the final semantic content, including tests, docs and maintenance;
mixed source commits may feed multiple themes. References are provenance, not
instructions to replay whole commits. Full source objects and honest adaptation
notes provide the provenance ledger. The [series audit](#l151-repaired-series-audit)
records the accepted commit-body format deviation; this ledger does not pretend
that omitted trailers exist.

| Source commits | Exact source authors | Owning themes and treatment |
| --- | --- | --- |
| `5c210e17fbafbdab01391fbfa60efa00701d6422` | Jaromil &lt;jaromil@dyne.org&gt; | L6 build/dependency support; L13 platform CI/release integration. Adapt required probes, not old generated summaries. |
| `85257a0986d841cb2f6a8db1eb4902e92cfa94f5`, `724f8c2ee71be107540ac5042ddb55d5c53ef69e` | Jaromil &lt;jaromil@dyne.org&gt; | L7–L9 policy utility dependencies; L10 peer diagnostics; L11 signer boundary; L12 config/runtime/stats support. Split mixed runtime content by contract; preserve dormant status. |
| `1b73fe3d2126e8a1233343e8485a28b507515426`, `ec9db369f5034677665ac1d93bdb87024fd7f21c` | Jaromil &lt;jaromil@dyne.org&gt; | L7 local checks; L8 accounting/dynamic dust/replacement; L9 mining and broad functional fixtures; L10 peer controls; L11 wallet boundary fixes/fixtures; L12 runtime dependencies; L13 CI test-script integration. Adapt actual surviving deltas to clusters/interfaces. |
| `dd03dcb98911e4ed75abb843ccdc5507b67dac7b` | Jaromil &lt;jaromil@dyne.org&gt; | L11 database/SQLite/signer hardening, adapted. |
| `c5928880885a32510cfd716f4354117eeaf4d4dc` | Jaromil &lt;jaromil@dyne.org&gt; | L6 Qt build support; L10 peer view; L11 wallet GUI support; L13 identity/QR/taskbar; L14 Qt5 startup/persistence repair. Split mixed GUI content without importing incidental upstream text. |
| `fc0dc945d0002ca054a7046a33b45ee92389b805` | Jaromil &lt;jaromil@dyne.org&gt; | L7 fixed-dust/sigop tests; L8 sub-dust/TRUC tests, plus L10 peer fixtures and L12 operator tests where present. Preserve tests' actual feature ownership. |
| `9f1780f33036b3a606b0120e94415587e44f018b`, `4570a6a54770fbda04d9cd05197b6f4fd8f88546` | Jaromil &lt;jaromil@dyne.org&gt; | L6 necessary build registration; L13 assurance/CI/release tooling and fixtures; L14 configured benchmark repair; owning-feature tests across L7–L12. No replacement maintenance state system. |
| `9da585141091354442aec822d5b47d8a0dc6afc0`, `26b691e4f35c66b28a2ddef7504790df29f07392`, `201927c151875aa3c19d96d7cbec00ece65c65b1`, `e819a07a4a1efc30d1d66f86394f7bbb0609f758`, `56e28947604c04e429e6035094e847452aeb38e1` | Jaromil &lt;jaromil@dyne.org&gt; | L13 CI scheduling, apt-retry, optional-feature selection and branded archive checks; adapt to v31 jobs. |
| `83fa42a963d3f190abeecec08db64f0175b2ebc8`, `f5182f0e3a7c49c85abe6159af0a16d1b6ae8298`, `7791eede50f6ca8bfca78ce2cb79ac005cc22d95`, `11c84c39ef4c0bf06d5f94b2299cf7502688cab3`, `0efa437e29d4fcb0ec567991c75221129c4c488f`, `f2595557d9e9029ab5506efc3aa094dd6252dc15`, `38bc7ec2ce41543a69dfec20e1fa98073f246502`, `df8aa8e2b79139f8c8cc9b4f1d412ab9d9a45792`, `ddae467a788aa0ca1f7c81fdd79d75e6014b10e7`, `778ff50d19a53a46bb0b0ba021a1111f886c7e7c`, `c474825f9698324747592c9535400ffd0cd01ed9`, `17e1d484aba2a881be03b8928fba3c0125e027a2` | Jaromil &lt;jaromil@dyne.org&gt; | L13 release/rehearsal/source/archives/signing/new-draft/promotion CI and notice deferral; preserve final safeguards, fold corrective history. `f5182f0e3a7c49c85abe6159af0a16d1b6ae8298` also supplies L11 sweep/privacy boundary tests and L12 safe-empty-stream writes/tests, plus L13 apt/sanitizer checks and L14 empty AutoFile write repair. No releases dispatched. |
| `25f889f97ef09b4596ccfc8a67cca3e420ffea9d`, `3e13c1f213b931c9304526cf17309a3af4d07174` | Jaromil &lt;jaromil@dyne.org&gt; | L13 Windows icon and QR QString-size corrections, kept/adapted with identity. |
| `b0b235d6a1d7c7548e96c6d171f5246c99e01be7`, `f4066e52827ae2edde22380159f4ace87e1a7dbf`, `30f673a4bca1ae92083bbd47035588216553ce18`, `a13f28e2f482a89a1b3979e9045ddabb4a3df498`, `1517d925b2d6e189bc395b484676d76b6d10b2f5`, `628bdf4e382b9c8b672e3017335ec37ef7ac542c`, `02464df449d3ce81bd3241ab6d7170788f621b06`, `089888acdd5c03a070a31177d9d20e2fce3ce72f` | Jaromil &lt;jaromil@dyne.org&gt;, devjoinedthechat &lt;235875317+devjoinedthechat@users.noreply.github.com&gt; | L6/L13/L15 catalog, scope, historical notes and provenance; rewrite candidate claims for actual implementation, retain relevant history/credit. |
| `bbd7339af2bb0b707f3d11f574f200352a7c2dbb`, `9b778b9a23d2d13a4b116984ad6739d83b1aa13f` | Jaromil &lt;jaromil@dyne.org&gt; | L10 peer stale-state fix/apptests; L12 streams assertion fixtures and L13 QR/build GUI assertions from the mixed test commit. |
| `e4b26462de371041bd9973e750e220f72895250e`, `0e3ae72d89552a4178e12b15fe56ffd87bf8eb03` | Jaromil &lt;jaromil@dyne.org&gt; | L11 wallet-derived per-send RBF and locked Qt assertions, adapted. |
| `5e2d79e9376a1fed32cd728091b23205e0ab9999` | Jaromil &lt;jaromil@dyne.org&gt; | L7 `node/mempool_args.cpp` string-view prefix modernization, adapted only where the parser remains; no consensus feature. |
| `fb2128887946f094cf1d69c8cbb3cfd3604ba9dc`, `dc1b217cbd4802cc45ef6d97919ce819f8f2a58e`, `9a216bc38919c9bd80c0438ef74f4013bc0d651d`, `7c70374050f61bfaa86f9de2c7955c3153c1e356`, `f3cd7da535bfe2f715b1ae49f32d54796503e0a3`, `96fecfd411a9b2e04633b8078dea038b7b3abf9b`, `a1748ccc6416563683710996e5380766dec61ac9`, `f9e9a36cd1715c4370a41f6527a71e2eb5c1536a` | Jaromil &lt;jaromil@dyne.org&gt; | L11 sweep RPC/GUI, coin-control facts, documentation, asynchronous authorization, locked fee snapshot and secret cleansing. Fold security corrections into final feature. |
| `875fa1582972f9cd6fa710b5f82bdc758b5470e4`, `9ab60f217b0ded70bebf08cb656de8d77944fa1c`, `9d9c125d9e6a3548f0d19678218bba9f66d6ae00` | Jaromil &lt;jaromil@dyne.org&gt;, devjoinedthechat &lt;235875317+devjoinedthechat@users.noreply.github.com&gt; | L9 priority-selected ancestor/resource correctness and locale-independent regression fixtures, adapted to chunk mining. |
| `aa27c9117a1f08e009d99417c0cd8151acbc7141`, `48f3b3bbe8930f91a6cedb3764ca3e1b6febd15f`, `a6f35e915c798236cebb6442756d0fe29a3214eb` | Jaromil &lt;jaromil@dyne.org&gt;, devjoinedthechat &lt;235875317+devjoinedthechat@users.noreply.github.com&gt; | L8 configured RBF/TRUC semantics and tests; L12 truthful RPC field; L13 final generated help. |
| `30e7eac266d6811c0520aa8846001b0501d47c5e`, `12d67ba60a926ec5993c2f2285bdece6e964d2fa`, `e6d4abc70406a8bc9082815a477ad0112e8e05ad`, `085a84a7cb936aa4317616fd9ba94152b3a26b32`, `8cb07ef1b92bf1f057d35bf478792725c14ab63e` | Jaromil &lt;jaromil@dyne.org&gt; | L7–L10/L12 feature-owned full-suite, Taproot-carrier, fee-estimator and startup fixtures; adapt to current APIs without erasing upstream regressions. |
| `1f7cfce1768ac2e26ebde8e8a2148098af4a7c83` | Jaromil &lt;jaromil@dyne.org&gt; | L13 regenerate manuals/example configuration from final help, not source-byte replay. |
| `16b2198275aee408686c22cb4365c594b7608ed6`, `47329c00dd66503166e7487d79a11589912ce2f3`, `1b2431c74c00eb5db1d0960e7480a39a4cce2ae2` | Jaromil &lt;jaromil@dyne.org&gt; | L6 native crypto flags, exact MiniUPnPc dependency patch and usable system LevelDB memory environment; the Chain interface include and mempool/1p1c lint fixtures follow L7/L10. |
| `784b2af17a0c8c0fa7a8b1988e9da168983a3890`, `f74cc0433bc4d5fefff6d1542707f32c532a78ee`, `73c97862e2888d2a65ea3adc4eaa1e2e4e96a877` | Jaromil &lt;jaromil@dyne.org&gt; | L8 synchronized dynamic-dust publication/snapshot and changing-rate tests; consumers in L12 report the same authoritative state. |
| `f66eca4ae466e198fecd66cac22a0033918b12ec` | Jaromil &lt;jaromil@dyne.org&gt; | L6 accepted stable contract collection and source/provenance reconciliation. |
| `87c950de92ea1d5b35eef50c2230e9705ad9467b` | Jaromil &lt;jaromil@dyne.org&gt; | L7 strict policy grammar, scoped help/diagnostics and parser/startup tests; distinct source behavior change. |
| `b60a6e04d09b4e706b6c382b844b61cc97c9280c` | Jaromil &lt;jaromil@dyne.org&gt; | L12 additive effective-policy RPC/schema/parity tests; distinct source API change. |
| `10d772eb7f4b00b5193428a0dde4c06558d6c297` | Jaromil &lt;jaromil@dyne.org&gt; | L7 local valid-block/carrier interactions and parser fuzz; L8 penalty/carrier arithmetic and fuzz; L9 priority/fee/template interactions; L6 retains documented gaps until tests are ported. |

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


## L12.1 operator schemas and configuration integration

The accepted effective-policy object and help are retained, with immutable
`MemPoolOptions::weight_per_data_byte` and `reject_spk_reuse` replacing source
process globals. Reporting stays within `MempoolInfoToJSON`'s existing mempool
lock; live dust publication and admission use the same authoritative rate.
No further snapshot refactor is justified: L4 supplied no refactor, and v31's
existing standardness snapshot already provides the required lifetime boundary.
Native `limitclustercount`, `limitclustersize` and `optimal` fields are preserved.

`uptime` is adopted upstream: v31 initializes its steady-clock source at startup
and returns integer elapsed seconds. `rpc_uptime.py` retains upstream's
wall-clock-jump coverage. Statistics model, schema, options declarations and unit
coverage are retained dormant; production registration, sampling and startup
option setup remain absent. A functional assertion requires `getmempoolstats`
to remain unavailable. Historical sample spacing, relative seconds, periodic
cleanup and memory target semantics are preserved, with no interpolation.

Roots read/write text config is restored before settings initialization, using
network-datadir `bitcoin_rw.conf` or the selected `confrw` path. Forced settings,
CLI, read/write config, JSON, network section and default section retain their
established order; scalar CLI selects its last value and text config its first
after negation. Source text config's flat key interpretation is retained even
when the file contains section headers. Config diagnostics redact sensitive
values through the existing argument logger. Numeric JSON Boolean getters
retain the source conversion behavior (zero false, nonzero true), with existing
scalar getter regression cases adapted accordingly. Writes use v31's native filesystem
path bridge; input-read and output open/close failures cannot replace the old
file or publish changed settings, and failed
reset removal is diagnosed correctly. These bounded adaptations avoid carrying
source's unchecked stream failure and inverted removal check into new code.
Read/write config is cleared on reread, and prune-state access takes the args lock.

Integrated `confrw` precedence gates previously owned by L7 and L11 are now
covered by `feature_policy_args.py`, `rpc_effective_policy.py` and
`wallet_sweepprivkeys.py`: effective strict parsing, JSON/config/CLI selection,
reported relay fee and sweep required-fee checks. Sweep retains source
`relayMinFee` semantics for its output dust guard; the runtime port does not
change that wallet contract. Ordinary config tests and
source stream-format tests remain applicable. Runtime-resource safeguards and
platform evidence are described in L12.2 below.


L12.1 review correction distinguishes valid EOF from `badbit` and non-EOF
failure in the newline-preserving reader, including partial final lines and
the invalid-line rewind/comment-copy path. Valid EOF state is cleared before
rewinding a final invalid line without a newline. Ordinary configuration parsing
also rejects failed reads before publishing parsed values; an existing unreadable
read/write file fails initialization. Deterministic failing-stream regressions
cover initial, partial-line and post-rewind failures; file-boundary coverage
preserves an unreadable input directory and its in-memory setting, removing only
the temporary output. Empty inputs and ordinary final lines without a newline
remain accepted. Dormant statistics are unchanged by this correction.


### L12.2 runtime-resource adaptation

The source runtime commit `85257a0986d841cb2f6a8db1eb4902e92cfa94f5`
provides the active allocation, file-descriptor and GiB safeguards. Allocation
preserves existing bytes across repeated fallback chunks and repositions update
streams between reads and writes. Requests inside an existing file do not
truncate it. The Windows branch checks existing length before extension; the
source's disabled Apple-specific truncate path is replaced by the common
preserving fallback. FD results saturate at `INT_MAX`, including infinite limits,
and GiB conversion uses checked shifts and a `size_t` bound.

`GetTotalRAM` has identical supported-platform detection and unknown-result
semantics in source and v31. Explicit `dbcache` conversion, its 32-bit 1 GiB cap,
4 MiB minimum, warning thresholds (450 MiB below 2 GiB RAM, otherwise 75%) and
failure fallback remain native. v31's upstream default is deliberately retained:
1 GiB on 64-bit hosts reporting at least 4 GiB RAM; 450 MiB otherwise, including
unknown RAM. This differs from source's fixed 450 MiB default. Retaining the native default
is an accepted compatibility decision, not evidence of exact default equivalence.

Source `mempressure`, I/O-priority, sequential-advice and close/uncache helpers
have no production callers. Its low-memory threshold has no setter and remains
zero. Omitting these unused helpers is an accepted compatibility decision; no collection,
pressure response, I/O scheduling or cache eviction service is activated.
The active allocation helper remains advisory; failures may leave allocation
incomplete, but reads must not clobber existing bytes.

Reporting returns owned JSON under the existing mempool lock; live dust updates
and standardness snapshots use that same recursive lock. Startup policy values
remain immutable after RPC warmup. The bounded publication regression also reads
the JSON policy while dust updates run, then joins its reader before destroying
the pool. Dormant statistics retain their source model and lifetime, without
production singleton calls, signals or concurrent consumers. No new runtime
mutex, singleton initialization or snapshot refactor is required.

Linux GCC builds and Clang checks are the locally supported evidence. Windows,
macOS, BSD and 32-bit execution are not proved by these runs; their conditional
branches are retained from the source/native APIs, with the Windows file-size
comparison expressed through `LARGE_INTEGER::QuadPart`. Whole-L12 acceptance
and broader platform/runtime matrices remain separate gates.

## L13.1 identity and desktop integration

The candidate retains Roots client identity, source/issue URLs and original
Roots PNG/ICO/ICNS/SVG/splash assets from `c592888088`. The Windows resource path
correction (`25f889f97e`) uses a configured resource file with absolute build-time
asset paths. Upstream protocol/legacy file names remain unchanged; this is not
mass rebranding. Core and Knots lineage remains explicit in the candidate README,
and binary copyright output retains Bitcoin Core credit alongside Roots.
Jaromil and devjoinedthechat source credit is retained in the thematic mapping.

The release version block from `7791eede50` now checks the selected `31.1` base:
untagged/default builds identify `v31.1-roots.1`; valid `CLIENT_VERSION_TAG`
values provide `v31.1-roots.<positive revision>` identity without Git metadata.
Numeric protocol versions and BIP14 `Satoshi` remain upstream-compatible.
`roots_build_identity` exercises the actual production CMake block: default,
two valid tag revisions, and five wrong-base/invalid-tag cases. The six headless
tools and actual Qt6 GUI identify Bitcoin Roots in captured `--version` output.
These are local candidate build names, not published release claims.

QR export retains the quiet zone, adds font-aware caption layout and updates
receive QR images on the money-font setting signal. The QString-size correction
from `3e13c1f213` uses the native Qt5/Qt6 size type. Qt apptests check Roots
icon/splash resources, PNG round-trip, clipboard export and quiet-zone geometry.
The wallet fixture drives the actual receive dialog's Save Image action into a
temporary PNG, compares it to the displayed export image, and checks refresh
through the production money-font setting. Keys, settings and regtest data are
disposable; no real wallet or network broadcast is involved.

Windows taskbar progress is carried behind `WITH_TASKBAR_PROGRESS`, enabled
only for Windows GUI builds. It registers/removes the native event filter with
the window lifetime and clears progress at synchronization completion. Qt5/Qt6
use their respective native-filter signatures. Linux configurations generate an
undefined taskbar macro; local tests do not establish Windows COM/event behavior.
Windows/macOS/32-bit runtime and cross-build evidence remains unavailable.

Example configuration is generated by the inherited generator from this exact
candidate's isolated `bitcoind --help` output and passes byte-identical repeat
regeneration. The selected profile has wallet enabled and IPC/USDT/ZeroMQ
disabled; its generated output omits the inherited optional ZeroMQ section.
This omission describes the tested help profile and does not retire ZeroMQ
support. No older source help/config content was copied into the generated file.
After the missing generator was installed externally, all seven manuals were
regenerated using the existing `contrib/devtools/gen-manpages.py` and GNU
help2man 1.49.3. Isolated wrappers invoke the unchanged tested headless binaries
and Qt6 GUI; daemon/wallet/GUI help has an explicit disposable regtest datadir.
The generator uses `LC_ALL=C` and `SOURCE_DATE_EPOCH=1791755546` (the starting
candidate commit timestamp). A second generation into a temporary directory
matches all seven manuals byte for byte. The manuals retain Bitcoin Core
copyright alongside Roots and contain no local wrapper/datadir paths. No manual
was hand-edited. Like the example config, they reflect the tested no-ZeroMQ
profile rather than proving optional-library availability.

Exact focused commands, build/binary/source hashes, result counts and the
receive/export evidence matrix are recorded below as local verification evidence;
L13.2 maintenance and the full L13 acceptance suite remain separate gates.

Focused L13.1 evidence on the uncommitted tree starting at `44c15f2cde`:

- `cmake --build <profile> --target bitcoin-qt test_bitcoin-qt -j 2`
  passed for the existing actual Qt5 and Qt6 profiles. The headless profile
  built `bitcoin bitcoind bitcoin-cli bitcoin-tx bitcoin-wallet bitcoin-util`.
- `python3 test/cmake/test_identity.py -v` passed 3 tests;
  `ctest --test-dir <headless-profile> -R roots_build_identity --output-on-failure`
  passed its one registered test.
- The actual `test_bitcoin-qt` binary passed 21 result rows, including
  initialization/cleanup, in each of eight Xvfb profiles: Qt5/Qt6 × 1280×800/
  1920×1080 × 100%/150% application font scale. `QT_SCALE_FACTOR=1` is retained;
  this is font scaling evidence, not physical DPI scaling. Each run has a
  60-second timeout and isolated XDG settings/runtime data.
- Each profile records receive controls, the receive QR dialog and the PNG
  save chooser at the full screen dimensions, plus the actual exported QR PNG:
  24 screenshots and 8 exported images. The save chooser uses Qt's non-native
  dialog for deterministic automated interaction. Screens retain preceding
  test widgets in the background; their synthetic balances are fixture facts.
  Representative Qt5/Qt6 receive views at 1280×800/150%, the Qt6 save chooser,
  and the exported QR at 1920×1080/150% were visually inspected: the affected
  payment text, QR caption and Save/Copy/OK actions were readable.
- `BITCOIND=<isolated-candidate-help-wrapper> bash contrib/devtools/gen-bitcoin-conf.sh`
  regenerated the example config; a second generation into a temporary file
  compared byte for byte. The wrapper passes an explicit disposable regtest
  datadir. An initial unwrapped help invocation hit the read-only default
  wallet directory and did not produce valid help; only the corrected isolated
  invocation supplies this generated file and current help evidence.
- `BUILDDIR=<isolated-candidate-help-wrappers> HELP2MAN=/usr/bin/help2man
  QT_QPA_PLATFORM=offscreen XDG_CONFIG_HOME=<isolated-settings>
  SOURCE_DATE_EPOCH=1791755546 LC_ALL=C python3 contrib/devtools/gen-manpages.py`
  generated all seven manuals. Repeat generation with `MANDIR=<temporary-directory>`
  and byte comparison passed 7/7; current version, retained credit and absence
  of local-path leakage were checked in each file. Generated differences are
  candidate version/date/URLs/credit, current Roots help/defaults and omission
  of the unsupported-in-this-profile optional ZeroMQ section.
- `python3 test/lint/check-doc.py` and `git diff --check` passed.

Local `.gestalt/l13-integration` evidence includes exact commands/results,
`l13.1-snapshot.json` with source/binary SHA256s, and
`l13.1-gui-snapshot.json` with image SHA256s/dimensions and all eight result
counts. This focused evidence does not substitute for the later full L13 suite,
Windows/macOS/32-bit tests or optional ZeroMQ/IPC/USDT profiles. On resumption,
31 previous source paths, 22 binaries and all 32 identity images matched their
recorded hashes; unchanged GUI and focused test evidence was reused.

## L13.2 maintenance, portable export and CI cadence

This adaptation retains the source classifier, exact canonical-promotion CI
proof, conservative nightly change gate, strict APT refresh, release-source/tag
validation, archive/COPYING checks, portable export/replay, optional notice
helpers, required manifest signing and new-draft-only tools. The checked-in
public key and expected fingerprint remain unchanged. Only a permanent tag
push can enter the signing environment; manual rehearsal has read-only
permissions, requires an absent future tag and cannot create a release. Signing
capability, signature/source mismatch and any existing draft/public release
still fail. No dispatch, publishing, remote ref mutation or signing with a real
secret was performed locally.

The remaining maintenance-only history is folded by responsibility:

| Source commits | Candidate disposition |
| --- | --- |
| `5c210e17fb`, `9f1780f330`, `4570a6a547` | Adapted CI/platform/assurance entry points and maintained fixtures; retain v31 native cache actions, compiler profiles, build script and test runners. Feature-owned code/tests stay in their earlier themes. |
| `9da5851410`, `26b691e4f3`, `201927c151`, `e819a07a4a`, `56e2894760` | Adapted additive classification, conservative scheduled-change gate, strict APT retry, optional-feature selection and branded archive checks. Broad platform jobs move to nightly/manual; no optional capability is retired. |
| `83fa42a963`, `f5182f0e3a`, `7791eede50`, `11c84c39ef`, `0efa437e29`, `f2595557d9`, `38bc7ec2ce`, `df8aa8e2b7`, `ddae467a78`, `778ff50d19`, `c474825f96`, `17e1d484ab` | Kept/adapted final release-source/rehearsal, five-platform archives, COPYING, portable replay, trusted signing/new-draft-only and promotion-proof safeguards. Corrective source history is folded; expanded notices remain optional/deferred. The configured-version sub-aspect is also preserved in L13.1. |
| `b0b235d6a1`, `f4066e5282`, `30f673a4bc`, `a13f28e2f4`, `1517d925b2`, `628bdf4e38`, `02464df449`, `089888acdd` | Adapted relevant candidate identity, maintained workflow guidance, provenance and credit; existing upstream release notes remain historical. Final aggregate/range evidence stays L15. |
| `1b73fe3d21`, `ec9db369f5`, `c592888088`, `9b778b9a23` | Remaining CI/build/GUI assertions are carried or adapted to v31 paths and the accepted owning-feature implementations; semantic source content is not reintroduced as a second chronological series. |

PR/development execution now uses the existing `ci/test_run_all.sh` and native
runners with a small affected-regression selector. Complete known paths select
wallet, network, policy/mining and runtime acceptance groups; mixed changes take
the union and incomplete/unknown paths choose broad fast coverage. Fast
functional execution is bounded at 900 seconds; selected libFuzzer smoke uses
5 seconds/target and a 15-second outer timeout. The nine possible smoke targets
have fixed seed 1 and per-input limits; input counts are results, not quotas.
The standalone selector also exposes a 60-second/target, 75-second outer
nightly campaign and a 7200-second selected-functional budget.

Scheduled/manual assurance retains full ASan/USDT/Qt, TSan/MSan, full Linux,
macOS and Windows fuzz corpora, extended/previous-release tests, tidy, i686
no-IPC, ARM32, kernel and native Windows/macOS smokes. Linux full-corpus replay
also runs the longer selected campaigns. Ordinary PRs have actual Qt tests,
fast native sanitizers/affected regressions and bounded fuzz smoke; explicit
labels can still request fresh heavy coverage. Every selected required PR job
must succeed. Nightly jobs with empty selected matrices are skipped before
strategy expansion; failed/incomplete schedule history retries. Explicit job
budgets and failure artifacts are documented in [CI guidance](/ci/README.md).

The v31 action/preset adaptations include current cache-action paths, LLVM 22
profiles, the upstream i686 no-IPC profile and VS2026 static builds. Windows
GUI jobs explicitly select the optional `miniupnpc` vcpkg feature when requiring
that library. Wallet-only Windows fuzz manifests explicitly disable ZMQ. The
vcpkg baseline is unchanged. Actual Windows/macOS/32-bit and hosted CI jobs were
not executed locally; static command/dependency checks do not establish their
success. No dependency was installed by this executor.

Focused local verification:

- Maintained source-tool tests cover classifiers, promotion proof, schedule
  gates, release source/tag/absence checks, archives/COPYING, optional notices,
  signing and new-draft refusal. The maintained suite passed 124 cases;
  after narrowing the mempool-argument selection, its six affected behavioral
  cases passed (125 current cases in aggregate with unchanged remaining tests).
- New tests execute actual nightly selector and required-result Bash blocks,
  plus affected selection/union/fallback, fast/nightly command budgets, real
  process timeout and retained failure logs. All pass. Parsed workflow YAML,
  dependency references/local actions and embedded Bash syntax validate locally.
  `actionlint` is unavailable; no hosted workflow was dispatched.
- The actual budget runner executed three wallet scripts successfully, then
  the conservative fallback for the mempool-argument path executed all 16 Roots
  scripts successfully. Its final narrower policy/mining/runtime selection is
  covered by the six behavioral cases and those matching native test passes.
  Tested product binaries and functional fixtures were unchanged.
- The new fast native ASan/UBSan profile configured on local Clang and built
  `bitcoin_clientversion`; this bounded probe is not a full sanitizer-profile
  pass. Sixteen changed shell scripts passed `bash -n` and ShellCheck.
- Temporary export verification snapshots the exact uncommitted candidate in
  a disposable local clone, validates against authenticated v31.1 commit
  `9be056a8a72b624dae9623b2f7bded92c2a21c91`, and invokes the actual
  `create-patch-series.sh`. Its internal `git am -3 --keep-cr` replay matches
  the snapshot outside `.github/**`; Core's `.github/**` tree is unchanged.
  Exported content retains Jaromil/devjoinedthechat credit. Synthetic snapshot
  commits/refs/tags exist only in the disposable fixture; candidate/source refs,
  index and history remain unchanged.

Exact commands, pass counts, intended cumulative/delta paths and current source,
binary/tool-test/export hashes are local evidence under
`.gestalt/l13-integration`. L13.1 GUI/capture/manual evidence remains unchanged
and is reused. Full L13 acceptance and the final candidate/hosted matrix remain
later gates, rather than being inferred from these focused checks.

## L15.1 repaired series audit

The audited implementation tip is
`8e7ffad5d932e07c0c80f3756f7c3a442786877e`, tree
`05dba633973f45bd3a3cf40fdad467443ff7fc89`. Both worktrees were clean at audit entry. The source and
base objects above resolve exactly; `v31.1` still resolves to the authenticated
annotated-tag object and peeled commit. This audit does not repeat online
release discovery or substitute public-main changes for the accepted source.

### History and attribution

The source range has 72 commits; the candidate range has nine accepted implementation
commits. The proposed evidence-only tenth commit remains pending acceptance.
The approved eleven-commit target is below 72 (61 fewer, about an 84.7%
reduction); the eleventh L16 repair commit is also pending. No candidate merge, `fixup!` or
`squash!` commit occurs after the new base. Each commit has exactly the previous
theme as parent; the old Roots tip is not an ancestor. Upstream merges before
the new base remain upstream history. The eleven-theme grouping includes the approved
revision of the original seven-theme proposal: graph policy and mining were
separated from local parsing/checks to isolate cluster accounting and residual
template risks, followed by the authorized preservation repairs. Accepted historical commit descriptions still mentioning seven
themes describe that earlier proposal, not additional commits to replay.

| Theme | Exact accepted commit | Aggregate responsibility |
| --- | --- | --- |
| L6 | `6a60180521daedbc404caf23a8c4a77bcb739de7` | Build/portability foundation and preserved contracts. |
| L7 | `e5445b7c96193c7ec141705aac5ba96651e0ee30` | Scoped parsing, local checks, valid-block regressions and inherited fixture adaptations. |
| L8 | `846c9de12a5e71259dc1ed829413d68f449ebfd5` | Native graph weight/modified fees, replacement/TRUC/reuse and authorized dynamic-dust repair. |
| L9 | `0fbff64239358c69a266705b189770b0f5570ff3` | Priority prefix, residual native chunks, resource/reward interactions. |
| L10 | `2cd5097c909d1ab2f8bf27eb0df97b24b6c0807e` | Peer resource/permissions, current-connection health and GUI evidence. |
| L11 | `ab07fc5490af30ce7c6da1c137c8280d1f4815f0` | Descriptor/signer boundaries, per-send RBF, transient sweep and coin-control facts. |
| L12 | `44c15f2cde7d36f2733814d6ab68ce148f1a7079` | Configuration/effective-policy, dormant statistics and resource safeguards. |
| L13 | `2f8bedaf1c56b3647746773492cb0d6c81a33e86` | Identity/desktop/help, Git-native maintenance/export and verification cadence. |
| L14 | `8e7ffad5d932e07c0c80f3756f7c3a442786877e` | Qt5 startup/persistence registration, configured benchmark default and safe empty-write repair with regressions. |
| L15 | Pending | This preservation ledger, observed diagnostics and limitations; documentation acceptance only. |
| L16 | Pending | Test-oracle/hosted-diagnostics repairs and final candidate gates under reduced verification. |

The full-object source ledger above covers exactly 72 distinct source commits,
with no object outside the pinned range. Its 25 contract IDs equal the catalog's
stable-ID set. Mixed source commits contribute to several themes; historical
corrections are folded into their corrected final behavior. Reverted script work
has no surviving consensus behavior. All source-range authors are Jaromil and
devjoinedthechat. Their credit and Bitcoin Core/Knots lineage are retained;
the new executor's aggregate authorship does not replace that attribution.

The user explicitly accepted the historical commit-body provenance deviation: L6 and L7 carry respectively
nine and twelve full `Source-commit:` trailers. L8–L13 use narrative source
references and the document ledger rather than those trailers. The full-object
ledger makes that provenance resolvable without rewriting accepted history;
L14 carries the three exact full source trailers. This is not a claim of historical trailer conformance or a fabricated cherry-pick `-x`.

### Range comparison and aggregate inspection

The exact comparison is:

```sh
git range-diff --no-color \
  49faec4f87f5cd19c88db01a82e5c68b087c8227..10d772eb7f4b00b5193428a0dde4c06558d6c297 \
  9be056a8a72b624dae9623b2f7bded92c2a21c91..8e7ffad5d932e07c0c80f3756f7c3a442786877e
git diff --binary 49faec4f87f5cd19c88db01a82e5c68b087c8227 10d772eb7f4b00b5193428a0dde4c06558d6c297
git diff --binary 9be056a8a72b624dae9623b2f7bded92c2a21c91 8e7ffad5d932e07c0c80f3756f7c3a442786877e
```

Default range-diff similarity finds **zero syntactic pairs: 72 old-only and
nine new-only entries**. That result is expected for many-to-one semantic
aggregation, different Core APIs and feature-owned test/doc corrections; it is
not evidence that 72 features disappeared. The full source ledger and thematic
table explain those unmatched entries. No matching threshold was adjusted to
manufacture apparent equivalence. The source aggregate changes 377 paths;
the candidate aggregate changes 313. Path-count reductions likewise do not
establish behavioral preservation.

The remaining aggregate source-only production paths were inspected explicitly:

| Source paths absent from the candidate delta | Disposition |
| --- | --- |
| `common/system.{cpp,h}`, `qt/clientmodel.cpp`, `rpc/server.cpp` | Native v31 monotonic uptime/startup display; only the reviewed uptime sub-aspect adopts upstream. |
| `policy/coin_age_priority.{cpp,h}`, `kernel/CMakeLists.txt`, `validation.h` | Entry-age caches and legacy admission signatures are replaced by active-UTXO priority/coinage and native v31 admission APIs; no kernel priority source is needed. |
| `policy/rbf.{cpp,h}`, `policy/truc_policy.{cpp,h}` | Legacy ancestor replacement/TRUC helpers are replaced by v31 cluster checks plus the configured gates in admission; Never/OptIn/Always and TRUC modes remain tested. |
| `policy/fees.{cpp,h}` | v31 moved the estimator under `policy/fees/`; long-horizon estimates, hourly flushing, initialization and changing-rate tests use those native APIs. The source bool/const flush wrapper has no result-consuming production caller. |
| `util/ioprio.{cpp,h}`, `util/mempressure.{cpp,h}` | Accepted omission of unused support, not a working pressure/I/O service or a whole runtime-resource retirement. |
| `util/batchpriority.cpp`, `util/exception.cpp`, `logging.cpp`, `util/overflow.h` | Native v31 header/logging/arithmetic maintenance replaces the old implementation; no separate Roots operator interface is represented by these maintenance hunks. |
| `util/check.{cpp,h}` | Native test-only assertion-to-exception hooks retain the required testing capability. |
| `util/fs.{cpp,h}` | Native path bridge and lock-error checks; Windows exclusive-open/reparse/permission execution remains an unproved platform boundary, not a Linux pass. |
| `util/strencodings.{cpp,h}` | Native restrictive subversion sanitization is retained; the source printable-character expansion is not imported. No raw or secret-bearing peer string is exposed. |
| `protocol.h` | The source reserved filtered-witness enum/helpers have no source production consumer; they are not an active wire feature to add. |
| `bench/mempool_stress.cpp` | Old cached coin-age/entry-constructor fixture arguments are unnecessary on v31's native entry API. L14 configured the benchmark profile and passed three bounded one-iteration smokes; this is not performance certification. |

Paths in this table are relative to `src/`. L14 adds `qt/optionsmodel.cpp`,
`bench/blockencodings.cpp` and `streams.cpp` to the candidate delta; those three
closed omissions are no longer source-only paths. Source-only upstream tests and
documentation remain assigned to their owning source-ledger themes: v31 tests
retain newer native assertions instead of replaying old constructor/ancestor
fixtures; historical release notes, generated help and platform/job assumptions
are not byte-replay targets. Exact per-source and aggregate path inventories
are retained in the local audit evidence. New candidate-only production deltas
are `dbwrapper.cpp` (system MemEnv header), `qt/forms/debugwindow.ui` (peer
details), and `script/descriptor.{cpp,h}` plus `wallet/scriptpubkeyman.{cpp,h}`
(native local-key satisfaction for sweep destinations). Those changes follow
the accepted L6/L10/L11 responsibilities and source security boundaries.

Consensus/script-interpreter/block/kernel-chain files and embedded
LevelDB/secp256k1/crc32c/minisketch subtrees are unchanged from the new base.
`validation.cpp` from `bool CheckBlock(` to EOF is byte-identical to that base;
native `txgraph.{cpp,h}` is unchanged. This bounds source placement; the local
reject/valid-block tests remain the behavioral evidence. No new RDTS/BIP110
consensus rule is claimed.

### Implemented contract coverage and explicit decisions

These are candidate test entry points, not assertions that every configuration
passed. The prior accepted theme gates are historical matching evidence;
L15.2 owns documentation and preserved-evidence provenance checks. L16 owns
the remaining repairs and reduced candidate gates; independent terminal
acceptance is a subsequent root-owned review.

| Contract | Current disposition and candidate evidence |
| --- | --- |
| policy-parsing | Adapted strict scoped grammar; [unit tests](../src/test/policy_args_tests.cpp), [startup/precedence](../test/functional/feature_policy_args.py), [fuzz oracle](../src/test/fuzz/policy_args.cpp). |
| effective-policy | Adapted initialized-state schema; [report/admission parity](../test/functional/rpc_effective_policy.py), [mempool units](../src/test/mempool_tests.cpp). |
| policy-standardness | Adapted local limits/reasons; [transaction units](../src/test/transaction_tests.cpp), [sigop cases](../test/functional/mempool_sigoplimit.py), [local reject/valid blocks](../test/functional/mempool_roots_local_policy.py). |
| policy-carrier | Adapted detection/budgets/native graph weight; [carrier cases](../test/functional/mempool_datacarrier.py), [transaction units](../src/test/transaction_tests.cpp), [subdust interactions](../test/functional/mempool_subdust_fee_penalty.py). |
| policy-dust | Fixed rules retained and dynamic lifecycle repaired under explicit authorization; [dust thresholds](../test/functional/mempool_dust.py), [runtime](../test/functional/mempool_dynamic_dust.py), [publication units](../src/test/mempool_tests.cpp). |
| policy-subdust | Adapted graph deficits with actual rewards unchanged; [functional arithmetic/reward matrix](../test/functional/mempool_subdust_fee_penalty.py), [documented conservative signed budgets](feature-contracts/subdust.md). |
| policy-replacement | Adapted modes over native cluster economic checks; [RBF modes](../test/functional/mempool_rbf_policy.py), [TRUC](../test/functional/mempool_truc.py), [replacement units](../src/test/rbf_tests.cpp). |
| policy-coinage | Adapted active-coin bounded age/maturity; [enabled/disabled/reorg/valid-block cases](../test/functional/mempool_roots_local_policy.py), [miner units](../src/test/miner_tests.cpp). |
| policy-reuse | Adapted full-script staged/resident claims; [mempool units](../src/test/mempool_tests.cpp), [mode/reorg/replacement cases](../test/functional/mempool_graph_policy.py). |
| mining-template | Adapted priority/residual chunks; [miner units](../src/test/miner_tests.cpp), [template limits](../test/functional/mining_roots_template.py), [joint rewards](../test/functional/mempool_subdust_fee_penalty.py). |
| peer-controls | Adapted resources/permissions/clearnet controls; [bounded controls](../test/functional/p2p_roots_controls.py), [extras](../test/functional/p2p_compactblocks_extratxs.py), [permissions](../test/functional/p2p_permissions.py). |
| peer-health | Adapted current facts/unavailable reset; [RPC](../test/functional/rpc_net.py), [actual GUI tests](../src/qt/test/apptests.cpp). Non-Linux CPU/rendering remains unproved. |
| wallet-foundation | Adapted descriptor/SQLite and Unix FD boundary; [signer](../test/functional/wallet_signer.py), [multiwallet](../test/functional/wallet_multiwallet.py), [process units](../src/test/system_tests.cpp). Windows handle/fallback gaps remain explicit. |
| wallet-rbf | Adapted wallet-derived per-send choice; [confirmation/PSBT tests](../src/qt/test/wallettests.cpp), [send](../test/functional/wallet_send.py). |
| wallet-sweep | Adapted transient-key authorization and corrected local signing capability; [sweep](../test/functional/wallet_sweepprivkeys.py), [GUI](../src/qt/test/wallettests.cpp), [structural boundary checks](../ci/test/test_wallet_privacy_contract.py). No full memory-erasure/race claim. |
| wallet-coin-control | Adapted wallet facts/invalidation; [GUI](../src/qt/test/wallettests.cpp), [send](../test/functional/wallet_send.py), retained populated visual matrix. |
| runtime-config | Adapted text config and failure-safe writes; [getarg units](../src/test/getarg_tests.cpp), [configuration](../test/functional/feature_config_args.py), policy/effective-policy/sweep integrated precedence cases. |
| operator-stats | Dormant model/schema retained; [model units](../src/stats/test/stats_tests.cpp). Only [monotonic uptime](../test/functional/rpc_uptime.py) adopts upstream; stats RPC remains unavailable. |
| runtime-resources | Adapted preserving allocation/FD/GiB; [FS units](../src/test/fs_tests.cpp), [util units](../src/test/util_tests.cpp). Explicit accepted change: native RAM-aware cache default; unused advisory support omitted. |
| build-portability | Adapted Qt5/6/system probes; [LevelDB finder tests](../test/cmake/test_leveldb.py), [profile guide](roots-build-portability.md). L14 covers Qt5 fresh-process settings persistence including early language reads. |
| identity-desktop | Adapted Roots identity/assets/QR/taskbar conditions; [version tests](../test/cmake/test_identity.py), [QR/icon GUI tests](../src/qt/test/apptests.cpp), [receive/save](../src/qt/test/wallettests.cpp). Windows taskbar runtime unproved. |
| maintenance-ci | Adapted affected fast checks and nightly/manual assurance; [selector tests](../ci/test/test_roots_regressions.py), [CI budgets](../ci/README.md). Local fixtures are not hosted/platform passes. |
| maintenance-release | Retained/adapted immutable export/tag/source/archive/signature/new-draft safeguards; [tools and replay](../ci/release/README.md), maintained `ci/test` suite. No publishing/signing action authorized here. |
| maintenance-docs | Adapted actual help/contracts/credit; [help lint](../test/lint/check-doc.py), regenerated config/manual parity and local links. |
| maintenance-native | Adapted native APIs/test hooks; L14 preserves the configured compact-block benchmark and safe empty writes, including null-handle behavior. Reverted source work is not imported as consensus behavior. |

No whole feature contract is retired or silently dropped. Dormant statistics,
the unconsumed Tor-subprocess declaration and unused resource helpers are not
available services. Native v31 sweep fee APIs replace removed settxfee/paytxfee.
The source script-reuse collision/staging, signer fallback, sweep local-key and
config stream-failure gaps are documented corrected adaptations. Conservative
graph aggregate budgets can reject some representable final states; this
accepted restriction is explicit in the subdust contract rather than called
exact mathematical equivalence.

### Historical buildability evidence

Accepted-tree SHA256 comparison against durable theme snapshots matches
25/25 L6, 66/66 L7, 44/44 L8, 23/23 L9, 37/37 L10, 63/63 L11, 29/29 L12,
105/105 L13 and 8/8 L14 changed source paths. These snapshots bind the theme review
commands and binary hashes; this audit did not rebuild nine historical trees.
They establish the recorded profiles only:

| Theme | Matched historical coverage / limit |
| --- | --- |
| L6 | Headless wallet, IPC build and actual Qt5/6 build/CTest; IPC Python cases skipped without capnp, distribution system-library profiles not proved. |
| L7 | Daemon/native units, applicable functional and scoped sanitizer/fuzz targets. The user accepted this historical gap: the standalone wallet was not covered by the limited target build; L8 subsequently moved the script-size global from node to common to repair its link. Thus L7 is not claimed fully buildable across default executables. |
| L8 | Full configured headless wallet build, native/functional and bounded sanitizer/fuzz evidence after that link repair. |
| L9 | Full configured headless build plus current miner tests; inherited fixtures corrected with focused reruns after the frozen sweep. |
| L10 | Full configured headless build and real Qt5/6 health tests; benchmark profile was off; L14 subsequently repaired and verified its source assertion omission. |
| L11 | Full headless and Qt5/6 configured targets; bounded signer/wallet sanitizers and authorization/visual cases. |
| L12 | Full headless and Qt5/6 targets; bounded runtime/config/resource regressions. L14 subsequently restored and tested the source empty-write guard. |
| L13 | Full headless/native/CTest/functional, actual Qt5/6 suites and maintenance tests. New minimal-backend Save fixture failed initially; only the interaction fixture changed and both full Qt suites reran successfully. |
| L14 | Current full headless/native/CTest/functional and tool gates, Qt5/Qt6 full suites after early-startup regression repair, configured benchmark build and bounded stream sanitizer/benchmark checks. Exact provenance is retained in the repair snapshot. |

L13's frozen accepted evidence records 772 native cases, 156 CTest passes plus
one external-assets skip, 278 functional passes plus 17 prerequisite skips,
21 Qt result rows per major and 125 maintained tool cases. Its 32 reviewed
identity images and seven repeated manuals retain original hashes. Those
results are historical provenance at the L13 tree, not a new L15.2 gate. The
old pure dynamic-dust fuzz binary's matching arithmetic closure does not certify
the entire current candidate. Optional hosted CI, Windows/macOS/32-bit,
USDT/ZMQ/IPC interfaces, system-library distributions, extended/previous-release
fixtures and full modern sanitizer/fuzz profiles remain unexecuted locally.

### Closed repair findings and current evidence

L14 closes the three verified preservation omissions in one accepted repair
commit, without rewriting the eight earlier accepted themes:

1. **Qt5 display-unit persistence:** source
   `c5928880885a32510cfd716f4354117eeaf4d4dc` (Jaromil) registers
   `qRegisterMetaTypeStreamOperators<BitcoinUnit>()`. The candidate retains the
   source-style `OptionsModel` hook for standalone construction and additionally
   registers in `BitcoinApplication::RegisterMetaTypes` before translation
   settings are read. A constructor-only hook was insufficient because Qt5's
   earlier language read cached an invalid enum. The fresh-process regression
   writes language and each of four units, then reads language before model
   construction and checks the stored type/value before fallback. The enhanced
   Qt5 test first failed; final Qt5 and Qt6 full suites each passed 22 result
   rows with no skips, including eight writer/reader child processes per major.
2. **Benchmark profile:** source
   `4570a6a54770fbda04d9cd05197b6f4fd8f88546` (Jaromil) preserves the configured default.
   The compact-block benchmark now uses the Roots default 32,768 rather than
   asserting the old value 100. The configured benchmark profile builds and
   three one-iteration producer smokes pass. These are build/smoke evidence,
   not throughput or full benchmark campaign results.
3. **Safe empty AutoFile writes:** source
   `f5182f0e3a7c49c85abe6159af0a16d1b6ae8298` (Jaromil) retains the guard after null-handle
   validation. Empty writes no longer call `fwrite` or mark a valid stream
   written. Regression coverage preserves null-handle failure and empty-write
   behavior; the current Clang ASan/UBSan streams smoke passes 20 cases with
   leak detection disabled. It does not certify leaks or a full sanitizer suite.

The repaired candidate records 774 native cases, 156 CTest passes plus one
external-assets skip, 278 functional passes plus 17 prerequisite skips, 22 Qt
result rows per major, 125 maintained-tool cases and the three benchmark smokes.
Exact command records, all eight repair source hashes, thirteen binary hashes,
four build-cache hashes and fixture hashes are bound in the local L14 review
snapshot. Its Qt-startup correction reuse proof binds unchanged headless code,
eight binaries, three primary caches and 389 tracked functional fixtures; the
headless gates were not repeated merely for Qt-only corrections. L15.2
revalidates this provenance by hashing the unchanged sources, binaries, caches,
fixtures and recorded logs; it does not rerun those suites. Earlier GUI
captures and generated manuals retain their original artifact identities and
limitations; none are relabeled as new captures.

Changed-scope include lint passes. Global include lint has two explicitly
accepted preexisting findings: duplicate `QTimer` in
`src/qt/test/wallettests.cpp` and the `cpp_int` include allowlist finding in
`src/test/fuzz/tx_pool.cpp`. They remain disclosed for terminal review; no
production cleanup is hidden in this documentation commit. Optional hosted CI,
Windows/macOS/32-bit, USDT/ZMQ/IPC interfaces, system-library distributions,
extended/previous-release fixtures and full sanitizer/fuzz campaigns remain
unproved by these local gates.

The refreshed audit records exact range/aggregate artifacts, per-source
path/author/disposition accounting, historical source matches and protected
source comparisons. The L15.1 audit neither changes nor fetches any ref. Its
before/after local source, candidate and public ref snapshots match. Prior
publication was separately authorized: the root published the candidate and
exact-base `review/core-v31.1` review branch and opened
[draft PR 49](https://github.com/LuganoPlanB/bitcoin-roots/pull/49). Existing
public release/main/tag refs remain outside this execution's write scope; the
executor performs no push, release, promotion, signing or history rewrite.
L15 documentation/export checks establish evidence integrity only. The separate
L16 repairs and reduced candidate gates, then a fresh independent terminal
review, remain required before whole-plan acceptance.


## L15.2 provenance, observed failures and approved handoff

The evidence-only working tree is based on implementation HEAD
`8e7ffad5d932e07c0c80f3756f7c3a442786877e` and Core base
`9be056a8a72b624dae9623b2f7bded92c2a21c91`. The current reuse check compares
all eight L14 repair-source hashes, thirteen binary hashes, four build-cache
hashes, 453 fixture hashes (389 functional, 60 maintained-tool, one native-stream
and three Qt) and seventeen command-log hashes with the accepted repair snapshot.
Matched evidence retains its original commands, skips and profile limitations;
no full unit, functional, Qt or sanitizer suite was rerun for this documentation
change. The older dynamic-dust arithmetic-closure pass remains historical,
limited evidence and does not supersede the current failures below.

| Matched accepted evidence | Result and scope |
| --- | --- |
| Native unit suite | 774 passing cases. |
| CTest | 156 passes; one external-assets skip. |
| Functional suite | 278 passes; 17 prerequisite skips, detailed below. |
| Qt5 and Qt6 | 22 passing result rows per major, including fresh-process persistence probes; no skips. |
| Maintained tools | 125 passing cases. |
| Stream ASan/UBSan smoke | 20 passing cases with leak detection disabled; prior bounded stream evidence only. |
| Compact-block benchmark | Three one-iteration smokes; no throughput claim. |

The 17 functional skips comprise uncompiled benchmark and chainstate tools
(two), missing Python ZMQ (one), disabled USDT tracepoints (five), unavailable
previous releases (five), external routable-address prerequisites (two) and
uncompiled IPC (two). Previous-release cases are
`wallet_backwards_compatibility.py`, `mempool_compatibility.py`,
`feature_unsupported_utxo_db.py`, `feature_coinstatsindex_compatibility.py` and
`wallet_migration.py`; IPC cases are `interface_ipc.py` and
`interface_ipc_mining.py`. The earlier functional benchmark skip remains recorded
although a later configured benchmark build and three smokes passed.

### Existing current-target diagnostic results

Before the reduced-verification instruction, the current-source local fast run
completed all nine selected targets with five-second per-target budgets, seed 1,
maximum input length 4096 and a 2048 MiB RSS limit. Its preserved result record
reports **seven passes and two failures**, with no omitted target in that run:

| Target | Observed exit | Result |
| --- | --- | --- |
| `dynamic_dust_fee` | 1 | Fail: Boost multiprecision signed-bound conversion in the reference oracle reports unsigned-integer-overflow. |
| `policy_args` | 1 | Fail: decimal-validator char conversion reports implicit-integer-sign-change at `src/test/fuzz/policy_args.cpp:37`. |
| `process_message` | 0 | Pass within the bounded local run. |
| `psbt` | 0 | Pass within the bounded local run. |
| `script` | 0 | Pass within the bounded local run. |
| `string` | 0 | Pass within the bounded local run. |
| `subdust_fee_penalty` | 0 | Pass within the bounded local run. |
| `tx_pool` | 0 | Pass within the bounded local run. |
| `txgraph` | 0 | Pass within the bounded local run. |

These results belong to the existing local Clang 19 current-source build with
`fuzzer,address,undefined,float-divide-by-zero,integer` instrumentation,
`-ftrivial-auto-var-init=pattern` and IPC disabled. Hosted CI uses Clang 22;
matching sanitizer names do not establish compiler/profile equivalence. The
Boost diagnostic is in `cpp_int/misc.hpp:1303`, reached by the oracle conversion
at `src/test/fuzz/tx_pool.cpp:574`; it is not a demonstrated production-policy
failure. These observed failures are preserved, unresolved at the L15 tree,
and cannot be relabeled as passing by using the older arithmetic-only binary.
No failing input was replayed during this documentation reconciliation.

The preserved hosted observation for draft PR 49 is fuzz job `114814967702`
in run `38249608708`, exit 1, with no uploaded per-target diagnostics. The
historical hosted failing target remains unidentified. Existing inspection found
per-target output in container scratch while artifact upload searched a host
path; L16 owns diagnostic transport repair. The local failures do not establish
the cause of that hosted failure. Hosted lint success is separate from the two
accepted preexisting global include-lint findings disclosed above.

### Reduced verification and remaining ownership

On 2026-10-12 the user explicitly removed further fuzz execution, sanitizer
campaigns and saved-input reproducer replays from remaining work and acceptance.
No hosted substitute is authorized for those omitted actions. Reduced coverage
is an accepted limitation, not a passing result. The existing nine-target
results and failure logs remain evidence; no new instrumented pass is required
or asserted. Following the planned L16 repairs, the affected targets must be
reported as **not rerun under instrumentation**.

L16 separately owns safe signed-bound handling including `INT64_MIN` in the
independent dynamic-dust reference oracle, explicit decimal-validator character
conversion, host-visible failure artifacts and bounded console diagnostics.
Its required verification uses ordinary focused unit/maintenance/CI-helper tests,
source review, relevant lint, matched prior full-suite evidence and portable
export/replay parity. Neither production-policy changes nor broad sanitizer
suppression are part of this disposition. Final candidate acceptance belongs
to L16 followed by fresh terminal review; acceptance of L15 only approves the
accuracy and integrity of this documentation.

Current documentation checks verify the complete source/author ledger, 25
contract dispositions, local links, option documentation, whitespace and exact
single-document dirty scope with an empty index. An isolated disposable clone
snapshots the intended documentation tree for portable export/replay using
`git am -3 --keep-cr`: product paths match that snapshot, `.github/**` remains
excluded from the patch and at the Core base on replay, and Jaromil and
devjoinedthechat credit remains present. All temporary snapshot commits, refs
and the export fixture tag belong to that clone; candidate refs are unchanged.
After an eventual accepted L15 commit, documentation tree/artifact identity is
checked again. This is export integrity evidence, not release, signing,
promotion, hosted-matrix or whole-candidate acceptance.
