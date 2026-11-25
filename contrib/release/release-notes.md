This release builds directly on the official Bitcoin Core v32.1 commit
`f0de87f040621468b956f01f526a43a010310501`. You can now see which Roots
revision a peer is running. It also carries forward the faster Linux resync
and static headless packages introduced in v32.1-roots.3.

## Changes since v32.1-roots.3

### See the Roots revision in the node’s user agent

Ordinary peers now see the Roots revision alongside the Core version. For
example, a build with revision `roots.4` announces `/Satoshi:32.1.0-roots.4/`.
Run `bitcoin-cli getnetworkinfo` and look at `subversion` to see what your
node announces. With `-uacomment=my-node`, peers see `/Satoshi:32.1.0-roots.4(my-node)/`.

## Inherited from v32.1-roots.3

### Faster resync on Linux

When built with the bundled LevelDB, Roots asks the Linux kernel to skip
read-ahead on chainstate tables. Jaromil reported approximately 90% faster
resync in his tests. [Read the original report](https://x.com/jaromil/status/2123802611216224686).

### Fully static packages for four architectures

Choose one of these packages to run Roots without a graphical interface.
They are built with a pinned version of `dyne/musl-action` and SDK v2.6.0.

| Architecture | Package |
| --- | --- |
| x86_64 | `bitcoin-roots-musl-x86_64.tar.gz` |
| ARM64 | `bitcoin-roots-musl-arm_64.tar.gz` |
| ARM hard-float | `bitcoin-roots-musl-arm_hf.tar.gz` |
| RISC-V64 | `bitcoin-roots-musl-riscv_64.tar.gz` |

Each package includes the daemon, launcher, command-line client, transaction
and utility tools, and SQLite wallet tools, with ZeroMQ support. The headless
builds omit Qt and multiprocess IPC. Release checks cover all nine packages
and reject static executables with dynamic dependencies or the wrong architecture.
[Musl release profile](https://github.com/LuganoPlanB/bitcoin-roots/blob/v32.1-roots.3/ci/release/README.md#static-headless-musl-packages)

### Cache defaults

With optional indexes disabled, the in-memory UTXO cache defaults to
**192 MiB**. The total **`-dbcache` default is 202 MiB**, including the
block index and chainstate database caches.

### macOS builds

The macOS release packages build on GitHub Actions runners using CMake 4.5.

## Inherited from v32.1-roots.2

### Message signing for native SegWit and Taproot

`signmessage` and `verifymessage` support BIP322 simple proofs for native
P2WPKH, P2WSH and P2TR addresses. Your descriptor wallet needs enough keys
and scripts to complete the proof. Supported policies include multisig,
Miniscript, and both Taproot key and script paths. [Core PR #36809](https://github.com/bitcoin/bitcoin/pull/36809)

- New proofs use the `smp` prefix followed by a base64-encoded, serialised witness stack.
- Verification also accepts older simple proofs without the prefix. It works without a wallet or blockchain data.
- Invalid, unsupported or inconclusive proofs return `false`. Malformed base64 or witness serialisation returns RPC error `-3`.

P2SH and wrapped SegWit addresses, partial signatures, full proofs and proofs
of funds are unsupported. Transaction version, locktime and sequence are all
zero, so waiting won’t make an incompatible timelock work. A message proof
doesn’t establish a balance or payment. Existing P2PKH compact signatures
and the legacy-only `signmessagewithprivkey` work as before.
[Message-signing limits](https://plan-b.foundation/bitcoin-roots/wallet-privacy#sign-and-verify-a-message)

### Legacy wallet migration

When migrating a legacy Berkeley DB wallet, Roots now reads each record from
the beginning of a fresh stream. Unread bytes from the previous record can no
longer carry over. SQLite prefix reads keep their existing behaviour.
[Core PR #36871](https://github.com/bitcoin/bitcoin/pull/36871)

### More consistent automated tests

Each CI run shares one verified revision of its test data across jobs. The
jobs check the revision, licence and hashes. Missing or corrupt data stops
the run. [Core PR #36872](https://github.com/bitcoin/bitcoin/pull/36872)

### Consistent command-line options

Builds with and without wallet support recognise the same hidden option
names. The startup and permission rules for `-allow-dangerous-rpc` still apply.

## Upgrading to Core v32 with Bitcoin Roots

Roots keeps its configurable relay, mempool and mining policies. It follows
Bitcoin Core consensus and does not enforce RDTS/BIP110. Your node still
accepts valid blocks containing transactions rejected by your local policy.

- **Policy defaults.** Full RBF is the default. The data-carrier limit is 83 bytes in aggregate, with at most one OP_RETURN output per transaction.
- **Back up before migrating wallets.** Roots uses descriptor wallets. Keep independent, secure backups and follow Core’s migration process for legacy wallets.
- **Check your signing tools.** PSBT v2 is the default. The send dialog follows the wallet’s RBF preference. Signalling replacement does not guarantee acceptance.
- **Preview private-key sweeps.** Review the signed transaction before authorising broadcast. Sweeping supports confirmed funds in supported legacy and SegWit addresses without importing the keys. Taproot sweeps are unsupported.
- **Enable key and descriptor RPCs explicitly.** `exportaddresskey`, `listdescriptors`, `gethdkeys` and `derivehdkey` are disabled by default, including public-only requests. Use `--allow-dangerous-rpc` on the command line at startup. Wallet authentication and unlock requirements still apply. An exported secret can expose other addresses or the wallet’s master private key. [Wallet manual](https://plan-b.foundation/bitcoin-roots/wallet-privacy)
- **Update RPC integrations.** Read applied policy from `getmempoolinfo.policy`. Core v32 removed `settxfee`, `paytxfee` and `misbehavior_score`. The old top-level `fullrbf` field requires `-deprecatedrpc=fullrbf`.

Thanks to [@danielabrozzoni for the P2P review](https://gist.github.com/danielabrozzoni/142356f60b0e977fd74cc9decf6e3271)
and to devjoinedthechat for [mining ancestor accounting (#24)](https://github.com/LuganoPlanB/bitcoin-roots/pull/24)
and [RBF reporting (#25)](https://github.com/LuganoPlanB/bitcoin-roots/pull/25).
Thanks also to Jaromil, Plan-B Foundation, and the Bitcoin Core and Bitcoin Knots contributors.
