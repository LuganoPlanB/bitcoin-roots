Bitcoin Roots v29.4-roots.3 candidate notes
============================================

This is candidate documentation for the intended `v29.4-roots.3` release, not
a release announcement. The published `v29.4-roots.2` release remains the
baseline; verify any executable with `bitcoin-cli --version` and
`bitcoin-cli help sweepprivkeys` before operating it.

Wallet privacy tools
--------------------

- `sweepprivkeys` performs an explicit, operator-controlled sweep of confirmed
  UTXOs for supported Base58 WIF keys into a spendable destination in the
  selected wallet. The supported configuration is a descriptor wallet with
  local private keys. P2PKH is supported for compressed and uncompressed WIF;
  compressed WIF also supports native and wrapped P2WPKH. It does not scan
  Taproot or other script forms, import supplied keys, or act as a backup or
  wallet-recovery mechanism.
- A non-broadcast signed preview is the default. Review the destination, input
  total, fee, transaction ID, and Replace-By-Fee state before requesting a
  broadcast. Chain, fee, and relay conditions can change after a preview.
- The Qt sweep dialog masks the key, disables copy/cut/context-menu actions,
  uses transient secure key handling, and can cancel an outstanding scan or
  preview. Broadcast submission has a final authorization boundary and cannot
  be cancelled once it begins. These safeguards reduce secret exposure; they do
  not guarantee that a displayed key or all GUI/host memory copies can be
  erased.
- Send Coins coin control shows wallet-reported amount, effective value,
  confirmations, reuse, grouping, lock/spendability, input-size, fee, change,
  and RBF facts. Locked outputs remain visible but cannot be selected; unsafe
  and immature outputs excluded by the normal availability filters are not
  added only to display a status. This is not a privacy score, an anonymity
  guarantee, CoinJoin, payjoin, automatic consolidation, key export, or a
  chain-analysis dashboard.

Compatibility and verification
------------------------------

Bitcoin Roots remains compatible with Bitcoin Core v29.4 consensus and P2P
wire behavior. This candidate introduces no wallet-format migration. Descriptor
wallets with local private keys are the supported sweep configuration;
watch-only, private-key-disabled, and external-signer wallets are not eligible
sweep destinations. Legacy-wallet behavior requires a compatible Berkeley DB
build and is outside the verified candidate matrix. Back up wallet material
before any upgrade or sweep, and use the built help, a non-broadcast preview,
and the focused wallet tests as the verification record for a particular build.

The Bitcoin Core v29.4 notes below are retained as upstream compatibility
context. Their availability statements do not announce a Bitcoin Roots release.

Bitcoin Core version 29.4 is now available from:

  <https://bitcoincore.org/bin/bitcoin-core-29.4/>

This release includes various bug fixes and performance
improvements, as well as updated translations.

Please report bugs using the issue tracker at GitHub:

  <https://github.com/bitcoin/bitcoin/issues>

To receive security and update notifications, please subscribe to:

  <https://bitcoincore.org/en/list/announcements/join/>

How to Upgrade
==============

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes in some cases), then run the
installer (on Windows) or just copy over `/Applications/Bitcoin-Qt` (on macOS)
or `bitcoind`/`bitcoin-qt` (on Linux).

Upgrading directly from a version of Bitcoin Core that has reached its EOL is
possible, but it might take some time if the data directory needs to be migrated. Old
wallet versions of Bitcoin Core are generally supported.

Compatibility
==============

Bitcoin Core is supported and tested on operating systems using the
Linux Kernel 3.17+, macOS 13+, and Windows 10+. Bitcoin
Core should also work on most other Unix-like systems but is not as
frequently tested on them. It is not recommended to use Bitcoin Core on
unsupported systems.

Notable changes
===============

This release fixes an issue where the chainstate database would repeatedly
rewrite large portions of itself, causing excessive disk reads and writes
during normal operation.

### Validation

- #35209 validation: correct lifetime of precomputed tx data
- #35465 coins: compact chainstate regularly

### Leveldb

- #61(bitcoin-core/leveldb): Disable seek compaction

### Net

- #34093 netif: fix compilation warning in QueryDefaultGatewayImpl()

### Wallet

- #35228 wallet: use outpoint when estimating input size

### Build

- #34228 depends: Unset SOURCE_DATE_EPOCH in gen_id script
- #34848 cmake: Migrate away from deprecated SQLite3 target

### Test

- #34918 fuzz: [refactor] Remove unused g_setup pointers

### Doc

- #34510 doc: fix broken bpftrace installation link
- #34561 wallet: rpc: manpage: fix example missing `fee_rate` argument
- #34671 doc: Update Guix install for Debian/Ubuntu
- #35283 doc: mention -DWITH_ZMQ=ON in BSD build guides

### CI

- #35202 ci: restore sockets in i686, no IPC job
- #35378 ci: switch runners from cirrus to warpbuild
- #35408 ci: 35378 followups

### Misc

- #35175 multi_index: fix compilation failure with boost >= 1.91

Credits
=======

Thanks to everyone who directly contributed to this release:

- andrewtoth
- Cory Fields
- Daniel Pfeifer
- darosior
- fanquake
- Hennadii Stepanov
- jayvaliya
- junbyjun1238
- Lőrinc
- MarcoFalke
- SomberNight
- ToRyVand
- willcl-ark

As well as to everyone that helped with translations on
[Transifex](https://explore.transifex.com/bitcoin/bitcoin/).
