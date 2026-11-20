# Bitcoin Roots v32.1-roots.3 candidate

This is an unpublished candidate, not a release announcement. No public tag,
downloadable package, deployment or publication is established by this text.
Source acceptance and release approval remain separate gates.

The direct base is the exact official Bitcoin Core v32.1 commit
`f0de87f040621468b956f01f526a43a010310501`. This maintenance candidate adds
chainstate tuning and static headless musl release packaging to published
[v32.1-roots.2](https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/v32.1-roots.2),
preserving that complete history and its three reviewed Core PR ports.
Roots.1 already folds the complete released Roots v31 line through
v31.1-roots.6 and its follow-up repairs into the owning semantic changes.
Bitcoin Knots `29.3.knots20260507` is historical
feature provenance, not the direct base or a release dependency. Historical
[v31.1-roots.6 notes](../../doc/release-notes/roots-v31.1-roots.6.md) are retained
unchanged and do not qualify this candidate.

## Changes since v32.1-roots.2

- On Linux, Bitcoin Roots asks the kernel to skip read-ahead on chainstate
  tables when built with the bundled LevelDB.
- The default in-memory UTXO cache is 192 MiB with optional indexes
  disabled. The total `-dbcache` default is 202 MiB, including the block
  index and chainstate database caches. Set `-dbcache` to choose a different
  budget.

- Add fully static, headless musl release targets for x86_64, ARM64, ARM
  hard-float and RISC-V64 through pinned `dyne/musl-action` and SDK v2.6.0.
  The additional assets are `bitcoin-roots-musl-x86_64.tar.gz`,
  `bitcoin-roots-musl-arm_64.tar.gz`, `bitcoin-roots-musl-arm_hf.tar.gz`
  and `bitcoin-roots-musl-riscv_64.tar.gz`.
- Include the daemon, launcher, CLI, transaction/utility and SQLite wallet
  tools with ZeroMQ. These packages omit Qt and multiprocess IPC. Packaging
  rejects wrong-architecture or dynamically linked executables; the release
  manifest/draft requires all nine packages. See the
  [musl release profile](../../ci/release/README.md#static-headless-musl-packages)
  for checks and runtime coverage limits. Actual builds remain a release gate.
  ARM hard-float uses static non-PIE linking because of the pinned SDK's
  static-PIE atomic-operation link limitation; the 64-bit profiles use static PIE.

## Inherited from v32.1-roots.2

- [Core PR #36872](https://github.com/bitcoin/bitcoin/pull/36872): select one
  fresh qa-assets revision per consuming Actions run and share verified unit
  vectors/full-replay corpora through same-run tar artifacts. Consumers check
  revision, license and hashes; missing/corrupt prepared inputs fail without
  network fallback. Shared container inputs remain read-only, full-fuzz copies
  remain independently writable and fast fuzz retains its download-free bounds.
- [Core PR #36871](https://github.com/bitcoin/bitcoin/pull/36871): advancing a
  read-only Berkeley DB cursor replaces reused key/value streams rather than
  appending unread record data. SQLite prefix coverage retains the same output
  contract. This preserves the supported migration path without restoring an
  obsolete Berkeley DB wallet engine.
- [Core PR #36809](https://github.com/bitcoin/bitcoin/pull/36809): `signmessage`
  and `verifymessage` support BIP322 simple proofs for native P2WPKH, P2WSH
  and P2TR. Descriptor-wallet signing requires a complete satisfaction with
  available keys/scripts, including supported multisig/Miniscript and Taproot
  key/script paths. Output is `smp` plus a base64 serialized witness stack;
  verification also accepts older unprefixed simple proofs without wallet or
  chain data. Invalid/unsupported/inconclusive proofs return `false`;
  malformed base64 or witness serialization raises error -3.
  P2SH/wrapped SegWit, partial/full signatures and proofs of funds are
  unsupported. Version, locktime and sequence are zero, so incompatible
  timelocks cannot be satisfied by waiting. P2PKH compact signatures and
  legacy-only `signmessagewithprivkey` are unchanged. A message proof does not
  establish a balance or payment. See [operator limits](../../doc/wallet-privacy.md#sign-and-verify-a-message).
- Keep the no-wallet hidden option list aligned with wallet-enabled
  `-allow-dangerous-rpc` registration. Wallet-enabled authorization, command-line
  restrictions and flags remain unchanged.

## Preserved policy and network behavior

Roots remains compatible with Bitcoin Core consensus and does not enforce
RDTS/BIP110. Relay, mempool and mining-template restrictions are local and
configurable: consensus-valid blocks containing locally rejected transactions
remain acceptable.

- Retain scoped policy parsing, local standardness/coin-age/reuse/replacement
  controls, independent subdust arithmetic, initialized dynamic-dust lifecycle
  and effective-policy reporting. Core v32 graph/package invariants remain
  native. Full RBF remains the default, with explicit Never/OptIn policies and
  v3/TRUC qualification.
  Applied settings are reported by `getmempoolinfo.policy`; the legacy top-level
  `fullrbf` field requires native v32 `-deprecatedrpc=fullrbf` compatibility.
- Default data-carrier policy remains 83 aggregate bytes with full recognized
  input/witness counting and a cost of 1 vbyte per detected data byte. More than
  one OP_RETURN output retains `multi-op-return` rejection. Nonstandard/bare
  carrier controls and consensus rules remain separate. Current help describes
  the aggregate counting and single-output boundary.
- Preserve priority-template selection, dependency ordering, priority-selected
  ancestor accounting, resource limits and native residual chunks. GBT and
  mining IPC consume the same applied policy.
- Keep the compact reconstruction count default at Core's 100, not the old
  Roots value of 32,768. The 10 decimal MB cap charges transaction dynamic
  storage; slot/object overhead and shared ownership are separate. Either zero
  limit disables the cache. Cache retention does not change admission or
  consensus.
- Preserve permission and address-relay repairs, manual block-fetch tracking,
  current-connection peer facts and GUI refresh/reset behavior. Consumers must
  not expect the removed `misbehavior_score` field. Bare whitelist grants do
  not imply `addr`; address-based outbound grants apply to manual connections.

## Wallet migration and disclosure

The port keeps descriptor wallets with SQLite and native external-signer
boundaries. It does not restore a Berkeley DB build option. Follow the supported
Core legacy-wallet migration path and retain independent secure backups.

Core v32 sends create PSBT v2 by default. The GUI reports actual reconstructed
signaling for PSBT v0/v2 and starts per-send RBF from the wallet preference.
Signaling does not promise replacement acceptance. Native `settxfee`/`paytxfee`
removal is retained; sweep uses fee estimation and configured fallback.

`sweepprivkeys` and the Qt6 Sweep Private Key dialog retain transient WIF input,
confirmed P2PKH and compressed-key native/wrapped P2WPKH scanning, selected-wallet
local signing and signed-preview default. They do not scan Taproot or import
keys. Submission requires the exact reviewed preview transaction and final
authorization; after that boundary cancellation cannot undo broadcast.

`exportaddresskey`, `listdescriptors`, `gethdkeys` and `derivehdkey` are disabled
entirely by default, including public-only modes. Enable them only by starting
the node with `--allow-dangerous-rpc` (or `=1`). Configuration files, JSON and
runtime settings cannot enable the permission. Omission or `=0` disables it;
invalid values and negated forms are rejected. RPC authentication and wallet
unlock requirements remain in force. This also applies to the Qt RPC console.

With that explicit startup opt-in, `exportaddresskey ADDRESS [key_type]` retains the released roots.6
contract for recognized local descriptor-wallet scripts: P2PKH/P2WPKH,
P2SH/P2WSH wrappers, multisig/Miniscript and Taproot internal/leaf/direct-output
and MuSig2 participant keys. The sorted `keys` array is canonical. Original
P2PKH/native-P2WPKH top-level aliases remain compatible; omitted/null `key_type`
selects `wallet`, while `taproot_output` requires an available effective output
secret and exact commitment metadata. Participant keys are not aggregate keys.

Normal selected-wallet authorization and unlock are required. Watch-only,
private-key-disabled and external-signer wallets cannot export local secrets.
Partial ownership does not promise an independent spend. Export does not recover
unknown derivation paths, scripts or commitments or advance keypools/balances.

An exported scalar may control other addresses, **including an HD master private
key**. A non-hardened child secret together with the ancestor xpub can compromise
the private subtree. Protect authenticated local RPC and use protected stdin
for transfers, keeping secrets out of command arguments, environment variables,
logs and files. Host/client/Qt copies may persist; complete memory erasure is
not promised. Export is not backup, does not broaden sweep's script coverage
and does not establish additional Quietproof proof support. Read the complete
[wallet manual](../../doc/wallet-privacy.md) before operating on funds.

## Current build and operator interfaces

Qt >=6.2 is supported; Qt5 and libevent dependencies are not restored. HTTP,
IPC, PSBT and index/resource behavior follows the native v32 interfaces where
it satisfies the retained contract. Embedded libraries remain default;
explicit system LevelDB/secp256k1 builds must meet the current API probes.
Windows optional MiniUPnPc discovery does not establish UPnP runtime support.

Read/write configuration, initialized effective policy and native monotonic
`uptime` remain available. Statistics source is dormant: `getmempoolstats` is
not registered and no samples are collected. No Network Watch, privacy score,
CoinJoin/payjoin, automatic consolidation or anonymity guarantee is introduced.

Product help/version/About output identifies Bitcoin Roots and Plan-B
Foundation, with separate historical Bitcoin Core attribution. Wire identity
remains `Satoshi`; numeric protocol/version identifiers are unchanged.
See the [feature catalog](../../doc/roots-features.md) and
[build profile guide](../../doc/roots-build-portability.md).

## Verification and release boundary

Owning-theme evidence, retained failures and remaining limitations are recorded
in the [port review](../../doc/roots-v32-port-review.md). Current manuals and
the example configuration are generated from current candidate help through
isolated regtest wrappers; optional-build and debug-only help omissions must
not be interpreted as removed features. No old manual is reused as current help.

Local maintenance, archive, synthetic-signing/draft and mocked transport tests
do not establish hosted CI or actual cross-platform builds. Full candidate,
compatibility, integration and final accepted-SHA portable replay remain their
own gates. Earlier source snapshots and binary results apply only to their
recorded inputs; later source changes require renewed affected verification.

The portable patch is exported from the exact Core base and excludes
`.github/**`. Local replay uses `git am -3 --keep-cr` with required commit
signing and product blob/mode/symlink parity, while Core hosting files remain
unchanged. Release machinery targets Linux x86_64/aarch64, macOS x86_64/arm64
and Windows x86_64 packages, plus the four static headless musl targets above.
Package builds disable source test suites and do not
close source-verification gaps.

Only a separately authorized tag push reaches required SHA512 manifest signing
and new-draft creation. Every existing draft/public release blocks replacement.
Manifest signing is separate from Git-tag and platform-code signing. The
permanent release tag requires the maintainer's signing key; disposable annotated
rehearsal tags do not establish that signature. Platform packaging uses ad-hoc
macOS app signatures without Developer ID/notarization and no Windows
Authenticode signature; verify the
actual future artifacts and signing disclosures independently. No such run,
signature or draft is claimed here. Upstream Core-tag signature verification
was explicitly waived for this port; source identity/ancestry checks remain
required and the waived check is not recorded as passed.

## Acknowledgements

Thanks to [@danielabrozzoni](https://github.com/danielabrozzoni) for the
[P2P review](https://gist.github.com/danielabrozzoni/142356f60b0e977fd74cc9decf6e3271)
behind retained permission and reporting corrections.

Thank you to [devjoinedthechat](https://github.com/devjoinedthechat) for
[priority-selected ancestor accounting (#24)](https://github.com/LuganoPlanB/bitcoin-roots/pull/24)
and [configured RBF reporting (#25)](https://github.com/LuganoPlanB/bitcoin-roots/pull/25).
Their contributions and regressions are preserved. Thanks also to Jaromil,
Plan-B Foundation and the Bitcoin Core contributors. Historical releases,
source credits and assets remain immutable.
