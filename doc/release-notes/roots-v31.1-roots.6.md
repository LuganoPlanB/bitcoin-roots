# Bitcoin Roots v31.1-roots.6

This candidate release-note template does not announce publication.

Bitcoin Roots v31.1-roots.6 is based directly on the official Bitcoin Core
v31.1 release (`9be056a8a72b624dae9623b2f7bded92c2a21c91`). It carries the
reviewed Roots semantic series onto that base, preserving the Core 31.1
implementation and fixes. Bitcoin Knots `29.3.knots20260507` describes
historical provenance for selected policy features; it is not the upstream
base or a release dependency.

Roots remains compatible with Bitcoin Core consensus and does not enforce
RDTS/BIP110. Relay, mempool and mining-template restrictions remain local and
configurable; a consensus-valid block remains acceptable even when its
transactions are rejected by local policy.

## Changes from v31.1-roots.5

- Generalize `exportaddresskey ADDRESS [key_type]` for recognized local
  descriptor-wallet addresses and scripts: P2PKH, native/wrapped P2WPKH,
  P2SH/P2WSH and nested wrappers, multisig/Miniscript, and Taproot
  `tr()`/`rawtr()` internal, leaf, output and MuSig2 participant keys.
- Return a canonical `keys` array with full public-key identities,
  fixed-width `private_key_hex`, `compressed` and sorted `roles`. Original
  P2PKH/native P2WPKH exports in default `wallet` mode retain their top-level
  `private_key_hex` and `compressed` compatibility aliases. Consumers of other
  constructions must read `keys`; keys retain their stored scalar identity.
- Add optional `taproot_output` mode for effective Taproot key-path secrets
  when local keys and exact commitment metadata are available. Omitted/null
  `key_type` selects `wallet`. MuSig2 participant keys are not aggregate keys;
  partial local ownership does not promise that the wallet can spend alone.

### Migration and secret scope

Normal selected-wallet authorization and encrypted-wallet unlock remain
required. Watch-only, private-key-disabled and external-signer wallets remain
ineligible. Export uses known descriptor indices and metadata; it does not
search unknown paths, recover missing scripts/commitments or recognize every
arbitrary script. It does not import keys, advance keypools or change balances.
Broader export does not expand `sweepprivkeys` scanning or Quietproof's normal
native-P2WPKH proof workflow.

An exported scalar can control more than the requested address, **including
an HD master private key** when the address directly uses a stored root.
Reused keys and pre-derivation MuSig2 participants can control many outputs;
a non-hardened child secret plus the ancestor xpub can compromise its subtree.
Use protected authenticated local RPC and protected stdin for transfers.
Scripts, cosigners, control blocks, preimages or timelocks may still be needed;
export is not a wallet backup. See the
[wallet manual](https://github.com/LuganoPlanB/bitcoin-roots/blob/v31.1-roots.6/doc/wallet-privacy.md#export-a-spending-key-for-one-address)
for the complete response, mode and disclosure contract.

## Preserved changes in v31.1-roots.5

- Restore Bitcoin Core's default compact-block extra transaction count of 100,
  replacing the Roots default of 32,768. The configurable
  `-blockreconstructionextratxnsize` budget remains available with its existing
  10 decimal MB default. Either limit can still disable the cache when set to
  zero. This changes local cache retention, not mempool admission or consensus.
- Attribute Roots copyright notices to Plan-B Foundation for 2026, including
  version output, splash-screen text, packaging metadata and generated manuals.
  Preserve the separate Bitcoin Core attribution and its historical years.
- Improve license-text line breaks in version and About output.

## Preserved changes in v31.1-roots.4

- Clarify compact-block reconstruction cache accounting in daemon/GUI help,
  manuals and example configuration. The default 10 decimal MB limits charged
  transaction dynamic storage, with separate slot/object overhead and shared
  ownership. Either cache limit can be set to zero to disable this cache.
  Defaults, policy admission, relay and consensus behavior are unchanged.
- Add native and P2P regression coverage for exact cache limits, eviction,
  shared ownership, replacements and valid-block reconstruction after a cache
  miss. Rejected transactions remain separate from mempool admission.
- Add an offline Core/Roots benchmark harness and a review of 30 policy/cache
  modifications. The controlled measurements support retaining existing
  controls; they do not establish mainnet latency or unique cache RSS savings.
  See the [review](https://github.com/LuganoPlanB/bitcoin-roots/blob/v31.1-roots.4/doc/mempool-policy-review.md).

Known benchmark-tool limitation: `prepare --resume` does not validate all retained
exported source files. Use a fresh preparation directory for new experiments.
The published review's source exports were independently checked and are unaffected.

## Preserved changes in v31.1-roots.3

- Add `exportaddresskey ADDRESS` to export one local descriptor-wallet P2PKH or
  native P2WPKH spending key as fixed-width `private_key_hex` plus `compressed`.
  Normal wallet unlock is required. The result is a spending secret;
  non-hardened child-key disclosure plus the corresponding ancestor xpub can
  compromise that ancestor's private subtree. See the
  [wallet manual](https://github.com/LuganoPlanB/bitcoin-roots/blob/v31.1-roots.3/doc/wallet-privacy.md#export-a-spending-key-for-one-address)
  for the contract and protected transfer guidance. Quietproof's normal proof
  workflow remains native P2WPKH only.

## Preserved changes in v31.1-roots.2

- `getpeerinfo` no longer returns the deprecated `misbehavior_score` field.
  Consumers must stop expecting it; discouragement and disconnection enforcement
  are unchanged.
- Bare `-whitelist` and `-whitebind` no longer grant `addr`. Their defaults are
  `download,noban,mempool,relay`, subject to `-whitelistrelay` and
  `-whitelistforcerelay`. Grant `addr` explicitly, or use `all`, only for peers
  trusted to bypass GETADDR response caching and address-processing rate limits.
  Message-size/validity limits, inbound-only GETADDR and one response per
  connection still apply. Socket and range permissions remain additive.
- Outgoing address-based whitelist grants now apply only to manual connections
  (`addnode` and `connect`). Automatically selected outbound peers receive none
  of these grants. Automatic outbound loopback connections also lose implicit
  Bloom permission; inbound and manual loopback retain it unless explicitly
  disabled. Onion connections retain their address-based whitelist suppression;
  explicit socket permissions remain distinct.
- The `getpeerinfo` permissions array now includes `forceinbound` for explicit
  grants and `all`. Its implied `noban`/`download` permissions and eviction
  behavior are unchanged. `forced_inbound` separately reports whether accepting
  a connection required freeing a slot.
- `getblockfrompeer` still requires an existing block header, as already
  established in the v31 port. A rejected duplicate request preserves the
  original in-flight tracking; a request from another peer may replace it.
  Prune-mode safeguards remain unchanged. roots.2 does not introduce the
  resolution of the older nullable-header review finding.

## Preserved and adapted Roots behavior

- Conservative policy controls, including the 83-byte default data-carrier
  script limit, configurable carrier cost, disabled bare-pubkey/bare-multisig
  relay by default, dynamic dust controls and priority mining. The default
  minimum relay fee remains 1000 sat/kvB. Consult built daemon help for the
  complete configuration and defaults.
- Priority mining accounts for ancestors already selected in the priority
  phase before evaluating fee-paying descendants. The regression coverage
  includes low-fee one/two-parent packages and consensus-valid block acceptance.
- `getmempoolinfo.fullrbf` reports the configured replacement policy. Full RBF
  remains the default; opt-in mode retains the Core TRUC/v3 qualification.
- Guarded private-key sweep through `sweepprivkeys` and the GUI, together with
  advanced coin-control facts and an explicit per-send RBF choice. These tools
  were already available on the published 29.4 line, including
  v29.4-roots.4; the 31.1 release adapts and preserves them.
- Sweep scans confirmed P2PKH outputs and, for compressed WIF keys, native and
  wrapped P2WPKH. Taproot and other script forms are not scanned. Destinations
  must be spendable by the selected wallet. Keys are not imported or saved;
  keep an independent backup until confirmation. Preview cancellation and
  wallet updates invalidate transient GUI state; submission must finish once
  its final broadcast authorization boundary is crossed.
- Peer-health details in the existing GUI Peers tab, bounded peer/network
  resources, monotonic `uptime`, and Roots branding remain
  available. No Network Watch, privacy score or anonymity guarantee is added.
  The retained statistics model is dormant; `getmempoolstats` is not exposed
  by this candidate.

## Core 31.1 wallet and build compatibility

The port uses Core 31.1 descriptor wallets and SQLite. It does not restore a
Berkeley DB build option; follow Core's supported legacy-wallet migration
instructions before using descriptor-wallet tools. Core's native IPC and
kernel/chainstate interfaces are retained. Qt 6 is the default GUI dependency,
with optional Qt 5 support when configured.

The release machinery targets Linux x86_64/aarch64, macOS x86_64/arm64 and
Windows x86_64 packages with enabled wallet and GUI support. Source and package
validation is tied to the canonical `roots/31.1` release tip, rather than a
promotion commit on `main`.

The portable patch applies directly to official Core v31.1 with
`git am -3 --keep-cr <bitcoin-roots-31.1-roots.6.patch>`. It deliberately
excludes `.github/**`; every other product path must match the release tag.
The permanent-tag workflow requires a detached signature for the SHA512
manifest using release key `5EADD53F2CD1F0B7AEEE920D25FC5C29CD528E32`
before creating a draft. This template does not establish that signing or
package verification has completed. The annotated Git tag is unsigned;
macOS applications use ad-hoc signatures without Developer ID signing or
notarization, and Windows executables have no Authenticode signature.
Manifest signing is separate from Git-tag and platform-code signing; verify
the final release's stated signing status and all eight assets independently.

## Verification scope

The historical cache-review branch passed independent whole-branch review and
local native, eleven selected functional, 216 maintenance and seven benchmark-harness tests.
Generated documentation matched across two runs. External script assets,
missing `lief` Python lint and three unchanged include-lint findings remain
limitations. Integration with the published roots.3 line and the roots.4 release
identity require current candidate CI; prior branch checks alone do not establish
that result. Fresh platform packaging and signed-draft verification remain
separate release gates.

The original source port was accepted with reduced verification. Matching prior
unit, functional, CTest and Qt evidence was retained alongside current ordinary
maintenance and policy-oracle checks. Earlier instrumented checks included
failures; repaired targets were not rerun under instrumentation at that stage.
No further fuzz execution, sanitizer campaigns or saved-input reproducer replays were
performed for that acceptance, and it claimed no new hosted product-test pass.

For the original roots.3 address-key export change, local checks passed the
full native unit suite (777 normal passes and one pass warning about absent external script
vectors), 25 affected functional cases, all 22 Qt test rows, and 216 maintenance
tests. Nine bounded fuzz targets passed with the documented CI sanitizer
runtime settings. External script vectors and full Python lint dependencies
were unavailable locally; hosted CI remains a separate release gate. These
checks do not establish exhaustive sanitizer or cross-platform coverage.

For the roots.6 generalization, local headless verification passed 158 CTest
cases with one external-script-assets skip, 23 selected Roots functional
variants, separate full export RPC and CLI regressions, and 153 maintenance cases.
Parity mutation/restoration checks exercised odd-Y WIF reload and shared
x-only HD-root regressions. These feature checks preceded the release identity
update; current candidate release qualification remains a separate gate.
GUI, IPC, Windows/macOS, full sanitizer/fuzz profiles and hosted CI were not
run for this generalization. Missing optional lint tools and unchanged global
lint findings remain disclosed in the candidate handoff.

Six headless manuals and the example configuration are regenerated from
the roots.6 candidate's current binaries. The checked-in Qt manual retains
its roots.5 provenance until a matching GUI build is available; it must not
be treated as roots.6 GUI verification. Optional help sections absent from
the no-IPC/no-ZeroMQ/no-USDT profile do not imply removed support.

The release pipeline runs ordinary release metadata tests and fresh package
builds. Its platform jobs disable unit, functional and fuzz test execution;
package builds do not close the source-verification gaps. Optional-platform,
rendering and comprehensive race coverage remain incomplete. See the
[port review and verification record](https://github.com/LuganoPlanB/bitcoin-roots/blob/9aaf62146fae1a89d65121468633d5c1701699e9/doc/roots-v31-port-review.md)
for the retained evidence, failures and limitations.

## Acknowledgements

Thanks to [@danielabrozzoni](https://github.com/danielabrozzoni) for the
[P2P review that prompted these corrections](https://gist.github.com/danielabrozzoni/142356f60b0e977fd74cc9decf6e3271).
The adjacent `forceinbound` reporting and loopback Bloom checks were added
during our follow-up audit.

Thank you to [devjoinedthechat](https://github.com/devjoinedthechat) for
[priority-selected ancestor accounting (#24)](https://github.com/LuganoPlanB/bitcoin-roots/pull/24)
and [configured RBF reporting (#25)](https://github.com/LuganoPlanB/bitcoin-roots/pull/25).
Both contributions and their regressions are preserved in the 31.1 port.
Thanks also to Jaromil and the Bitcoin Core contributors whose work this
release builds on.

## Port review and changelog

- [Reviewed semantic port (#49)](https://github.com/LuganoPlanB/bitcoin-roots/pull/49).
- [Prior release changelog from v29.4-roots.4](https://github.com/LuganoPlanB/bitcoin-roots/compare/v29.4-roots.4...v31.1-roots.1).
- [Migration changelog from v31.1-roots.1](https://github.com/LuganoPlanB/bitcoin-roots/compare/v31.1-roots.1...v31.1-roots.2).
- [Changes from v31.1-roots.2](https://github.com/LuganoPlanB/bitcoin-roots/compare/v31.1-roots.2...v31.1-roots.3).
- [Changes from v31.1-roots.3](https://github.com/LuganoPlanB/bitcoin-roots/compare/v31.1-roots.3...v31.1-roots.4).
- [Changes from v31.1-roots.4](https://github.com/LuganoPlanB/bitcoin-roots/compare/v31.1-roots.4...v31.1-roots.5).
- [Changes from v31.1-roots.5](https://github.com/LuganoPlanB/bitcoin-roots/compare/v31.1-roots.5...v31.1-roots.6) (available after publication).
- [Official Bitcoin Core v31.1](https://github.com/bitcoin/bitcoin/releases/tag/v31.1).
