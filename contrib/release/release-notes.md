# Bitcoin Roots v31.1-roots.1

This candidate release-note template does not announce publication.

Bitcoin Roots v31.1-roots.1 is based directly on the official Bitcoin Core
v31.1 release (`9be056a8a72b624dae9623b2f7bded92c2a21c91`). It carries the
reviewed Roots semantic series onto that base, preserving the Core 31.1
implementation and fixes. Bitcoin Knots `29.3.knots20260507` describes
historical provenance for selected policy features; it is not the upstream
base or a release dependency.

Roots remains compatible with Bitcoin Core consensus and does not enforce
RDTS/BIP110. Relay, mempool and mining-template restrictions remain local and
configurable; a consensus-valid block remains acceptable even when its
transactions are rejected by local policy.

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
`git am -3 --keep-cr <bitcoin-roots-31.1-roots.1.patch>`. It deliberately
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

The source port was accepted with reduced verification. Matching prior unit,
functional, CTest and Qt evidence was retained alongside current ordinary
maintenance and policy-oracle checks. Earlier instrumented checks included
failures; repaired targets were not rerun under instrumentation. No further
fuzz execution, sanitizer campaigns or saved-input reproducer replays were
performed, and no new hosted product-test pass is claimed.

The release pipeline runs ordinary release metadata tests and fresh package
builds. Its platform jobs disable unit, functional and fuzz test execution;
package builds do not close the source-verification gaps. Optional-platform,
rendering and comprehensive race coverage remain incomplete. See the
[port review and verification record](https://github.com/LuganoPlanB/bitcoin-roots/blob/9aaf62146fae1a89d65121468633d5c1701699e9/doc/roots-v31-port-review.md)
for the retained evidence, failures and limitations.

## Acknowledgements

Thank you to [devjoinedthechat](https://github.com/devjoinedthechat) for
[priority-selected ancestor accounting (#24)](https://github.com/LuganoPlanB/bitcoin-roots/pull/24)
and [configured RBF reporting (#25)](https://github.com/LuganoPlanB/bitcoin-roots/pull/25).
Both contributions and their regressions are preserved in the 31.1 port.
Thanks also to Jaromil and the Bitcoin Core contributors whose work this
release builds on.

## Port review and changelog

- [Reviewed semantic port (#49)](https://github.com/LuganoPlanB/bitcoin-roots/pull/49).
- [Full changelog from v29.4-roots.4](https://github.com/LuganoPlanB/bitcoin-roots/compare/v29.4-roots.4...v31.1-roots.1).
- [Official Bitcoin Core v31.1](https://github.com/bitcoin/bitcoin/releases/tag/v31.1).
