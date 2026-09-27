# Bitcoin Roots feature catalog

Bitcoin Roots is based directly on Bitcoin Core v30.3. It retains selected
operator and local-policy work whose historical lineage includes Bitcoin Knots
`29.3.knots20260507`; Knots is not an upstream dependency and its candidate
tree is not a list of shipped features.

This page is the public inventory for Roots-specific work. The published
`v29.4-roots.1` release is the historical baseline. This checkout is an unreleased
port onto Bitcoin Core v30.3 with follow-on additions identified below; it is
not a published release. Ordinary Bitcoin Core v30.3 functionality is documented
in the built help and the manuals in this directory. The commands below describe
the binary built from this tree; use `bitcoind -help` as the authority for the
exact options in a given build.

## Available in this checkout

Unless marked as an unreleased follow-on, the items in this section were
available in the published `v29.4-roots.1` baseline.

### Conservative, configurable local transaction policy

Roots retains configurable admission, relay, and mining-template policy. The
current daemon exposes controls including `-datacarrier`, `-datacarriercost`,
`-datacarriersize`, `-mempoolreplacement`, `-minrelaycoinblocks`,
`-minrelaymaturity`, `-permitbaremultisig`, `-subdustfeepenalty`,
`-blockmaxweight`, `-blockmintxfee`, and `-blockprioritysize`.

These are local node choices, not block-validity rules. A transaction rejected
by this policy can still be consensus-valid, and a valid block containing it
must remain acceptable. The retained regression coverage includes
[`mempool_dust.py`](/test/functional/mempool_dust.py),
[`mempool_subdust_fee_penalty.py`](/test/functional/mempool_subdust_fee_penalty.py),
[`mempool_sigoplimit.py`](/test/functional/mempool_sigoplimit.py), and
[`mempool_truc.py`](/test/functional/mempool_truc.py).

### Peer controls and bounded network resources

Roots carries selected peer-control and resource-bound work without changing the
Bitcoin P2P protocol. The current help includes compact-block reconstruction
bounds (`-blockreconstructionextratxn` and
`-blockreconstructionextratxnsize`), an orphan-transaction limit
(`-maxorphantx`), upload limiting (`-maxuploadtarget`), and Core-compatible peer
permission controls such as `-whitebind` and `-whitelist`.

The retained P2P coverage includes
[`p2p_disconnect_ban.py`](/test/functional/p2p_disconnect_ban.py),
[`p2p_eviction.py`](/test/functional/p2p_eviction.py),
[`p2p_filter.py`](/test/functional/p2p_filter.py), and
[`p2p_invalid_messages.py`](/test/functional/p2p_invalid_messages.py).
This is not a Network Watch or transaction/block activity feed.

The follow-on review retained these existing bounds and their coverage; it did
not add a separate P2P hardening feature or alter Bitcoin P2P wire behavior.

### Focused peer-health monitoring (unreleased follow-on)

The existing Debug window's Peers tab provides a bounded, live view of current
connections. Its sortable table shows connection age, direction, network,
minimum ping, and sent/received traffic. Selecting one peer shows supported
connection details including transport (v1 or v2), services, permissions,
connection type, current ping, and synchronization heights.

This uses the node's existing peer-stat refresh and does not retain history or
inspect transaction or block content. If optional node-state details cannot be
read during a refresh, the details pane reports them as unavailable instead of
showing values from an earlier peer. This Roots follow-on selection is covered
by [`src/qt/test/apptests.cpp`](/src/qt/test/apptests.cpp); it adds no new
network collector or monitoring application.

### Wallet and signing hardening

The reviewed Roots wallet selection includes database and SQLite error handling,
external-signer integration boundaries, and spend-path hardening. Descriptor
wallets with SQLite are the primary supported configuration. The relevant
regression coverage includes
[`wallet_signer.py`](/test/functional/wallet_signer.py),
[`wallet_multiwallet.py`](/test/functional/wallet_multiwallet.py), and wallet
unit tests under [`src/wallet/test/`](/src/wallet/test/).

Legacy Berkeley DB support is a separate build choice. This catalog makes no
claim that it has been tested on a particular host library; release-compatible
legacy testing requires Berkeley DB 4.8.

The follow-on review retained these existing wallet boundaries and their
coverage; it did not add a new database, flush, or backup behavior.

### Per-send Replace-By-Fee choice (unreleased follow-on)

The Send Coins dialog provides an explicit Replace-By-Fee (BIP-125) choice for
each transaction. It starts with the wallet's `-walletrbf` preference, and the
confirmation shows whether the prepared transaction signals replacement. This
only signals a transaction's replaceability; it does not promise that a later
replacement will be accepted by peers or their local policy. This Roots
follow-on selection is covered by
[`src/qt/test/wallettests.cpp`](/src/qt/test/wallettests.cpp), including the
explicit overrides and saved-PSBT signaling; it is not broad coin control.

### Runtime and operator foundations

The `getmempoolstats` RPC returns the node's collected, non-interpolated
mempool samples, and the `uptime` RPC uses a monotonic uptime source. These
interfaces are intended for node operators and tooling; their exact schemas are
available through `bitcoin-cli help getmempoolstats` and `bitcoin-cli help
uptime`. The runtime also contains support for system-RAM detection, memory
pressure, and advisory I/O priority where the host platform provides it. Those
are implementation safeguards, not a promise of identical behavior or knobs on
every operating system.

The retained coverage includes
[`rpc_uptime.py`](/test/functional/rpc_uptime.py) and unit tests in
[`src/stats/test/`](/src/stats/test/) and [`src/test/`](/src/test/).

### Build and platform portability

Roots uses the CMake build system with selected portability and dependency
support for this release. Configuration can select Qt 5 or Qt 6, and exposes
optional support for QR code generation, external signers, a dedicated Tor
subprocess, and Windows taskbar progress when their build prerequisites and
platform conditions are met. The dependency and platform instructions are in
[`doc/dependencies.md`](/doc/dependencies.md),
[`doc/build-unix.md`](/doc/build-unix.md), and the platform-specific build
guides in [`doc/`](/doc/).

Availability depends on the actual CMake summary and installed dependencies.
This catalog does not claim that every optional feature or platform combination
has been tested by the current checkout.

### Roots identity and desktop application

The daemon, command-line tools, and Qt application identify themselves as
Bitcoin Roots `v30.3-roots.1`. This checkout is not a published release merely
because it has that build identity. The Qt application includes Roots branding
and icons. In the receive-request dialog, a QR image can be saved as a PNG. On
Windows GUI builds where taskbar progress is enabled, synchronization progress
is reflected in the taskbar. The Qt source and tests are in
[`src/qt/`](/src/qt/), including QR export coverage in
[`src/qt/test/apptests.cpp`](/src/qt/test/apptests.cpp).

The generated manuals and example configuration are available in
[`doc/man/`](/doc/man/) and
[`share/examples/bitcoin.conf`](/share/examples/bitcoin.conf).

## Release and maintenance infrastructure

These facilities are for maintainers and release operators, not new consensus
or wallet behavior. Release tags use the Roots form
`v<core-version>-roots.<positive-integer>` (with a corresponding release
candidate form). The tag-triggered release workflow validates the checked-out
source and archive inputs, produces a SHA512 manifest, and creates a draft
release only in its protected release environment. It intentionally does not
restore nightly, promotion, frozen-state, or generated-evidence control planes.

[`contrib/roots/README.md`](/contrib/roots/README.md) documents the Git-native
workflow: inspect the explicit Core base, use ordinary Git porting or rebase
operations, review with `git range-diff`, and validate an annotated tag and its
ancestry before publishing. Verify the official Core tag with an independently authenticated signer key
and record its signature result and trust basis; do not infer verification
from the presence of a local tag alone.

## Planned or under review

No additional operator feature from this follow-on series is available merely
because it was evaluated. The P2P and wallet reviews retained the existing
published-baseline behavior rather than adding new hardening or backup features.
Cross-feature coverage and catalog reconciliation are release-readiness work,
not operator features. Any later proposal remains under review until it has an
accepted implementation and tests.

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
- Tor "pairing" credentials, proxy credentials, control-port secrets, or
  private-key sharing surfaces;
- Tor endpoint sharing or a dedicated endpoint-sharing UI;
- private-key sweeping, or broad or privacy-aware coin control and payment
  history redesign;
- the candidate's broad Knots common-maintenance, consensus/script, wallet, or
  documentation overlays merely because they occurred in historical source;
- the discarded replay, frozen-state, generated-evidence, or promotion control
  plane; or
- a VitePress documentation overlay, tonal/base16 font dependency, or other
  separate visual identity layer.

For maintainers, [`contrib/roots/README.md`](/contrib/roots/README.md) documents
the Git-native maintenance workflow. For current build and runtime options, use
the generated manuals and the built binary help.
