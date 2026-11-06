---
title: Compare Bitcoin node implementations
description: A versioned comparison of Bitcoin Core, Bitcoin Roots, and Bitcoin Knots based on primary project sources.
pageClass: bitcoin-roots-reading bitcoin-roots-compare
head:
  - - meta
    - property: og:title
      content: Compare Bitcoin node implementations
  - - meta
    - property: og:description
      content: A sourced comparison of Bitcoin Core, Bitcoin Roots, and Bitcoin Knots.
---

# Compare Bitcoin node implementations

Compare the latest published Bitcoin Roots and Bitcoin Core releases, with
Roots-only operator features first. Bitcoin Knots remains a separately versioned
comparison column.

## Versions reviewed

Latest published releases checked on **6 November 2026**:

- **Bitcoin Roots:** [v32.1-roots.1](https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/untagged-3450aebe1f5e3a73703b)
- **Bitcoin Core:** [v32.1](https://github.com/bitcoin/bitcoin/releases/tag/v32.1)
- **Bitcoin Knots:** [v29.4.3.knots20260508](https://github.com/bitcoinknots/bitcoin/releases/tag/v29.4.3.knots20260508)

Bitcoin Roots v32.1-roots.1 starts directly from Bitcoin Core v32.1. Selected
policy, wallet and operator features have historical Knots lineage; Knots is
not the direct Roots base or a dependency. Each table column describes the
release named in its heading, rather than mixing historical and current versions.

## Comparison

<ComparisonTable />

“Roots-only” means the described interface or workflow is present in the reviewed
Roots release and absent from both other reviewed releases. Additional controls
shared with Knots follow, then the consensus, resource and wallet comparisons.
Shared names do not imply identical defaults or behavior; consult each version's
built-in help for accepted settings. The [Roots feature catalogue](/doc/roots-features)
and [effective-policy contract](/doc/feature-contracts/effective-policy) describe
the current Roots boundaries.

### Wallet scope

Private-key sweeping and advanced privacy-aware coin control are shipped in
v32.1-roots.1. Sweeping uses a transient WIF key, scans supported confirmed
P2PKH/P2WPKH outputs, and defaults to a non-broadcast preview. It does not scan
Taproot outputs. Advanced coin control exposes wallet facts and clears stale
selections; these manual tools do not add CoinJoin, payjoin or an anonymity score.

Address-specific key export is a separate workflow. `exportaddresskey`,
`listdescriptors`, `gethdkeys` and `derivehdkey` require the explicit startup
command-line option `--allow-dangerous-rpc`; normal wallet authorization and
unlock requirements still apply. Read the [current wallet manual](/doc/wallet-privacy)
for supported scripts, permissions and key-disclosure consequences.

### Current-version differences

Core v32.1 already provides monotonic uptime and the native v32 wallet, mempool,
resource and build foundations inherited by Roots. These are shared features.
Conversely, `getmempoolstats` is not available in Roots v32.1-roots.1, and the
old `-maxorphantx` count option is absent from both v32.1 columns. The table
keeps these differences explicit instead of carrying forward v29 claims.

### Sources

The comparison uses these primary release and source references:

- **Roots:** [v32.1-roots.1 release](https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/untagged-3450aebe1f5e3a73703b),
  [released source](https://github.com/LuganoPlanB/bitcoin-roots/tree/f0393f2688656ba544b40fe508db909152201c18),
  [feature catalogue](https://github.com/LuganoPlanB/bitcoin-roots/blob/f0393f2688656ba544b40fe508db909152201c18/doc/roots-features.md),
  [effective-policy schema](https://github.com/LuganoPlanB/bitcoin-roots/blob/f0393f2688656ba544b40fe508db909152201c18/doc/feature-contracts/effective-policy.md)
  and [wallet manual](https://github.com/LuganoPlanB/bitcoin-roots/blob/f0393f2688656ba544b40fe508db909152201c18/doc/wallet-privacy.md).
- **Core:** [official downloads](https://bitcoincore.org/en/download/),
  [v32.1 release](https://github.com/bitcoin/bitcoin/releases/tag/v32.1),
  [released source](https://github.com/bitcoin/bitcoin/tree/f0de87f040621468b956f01f526a43a010310501),
  [policy options](https://github.com/bitcoin/bitcoin/blob/v32.1/src/init.cpp),
  [Qt send dialog](https://github.com/bitcoin/bitcoin/blob/v32.1/src/qt/sendcoinsdialog.cpp)
  and [GUI dependency requirements](https://github.com/bitcoin/bitcoin/blob/v32.1/doc/dependencies.md).
- **Knots:** [official downloads](https://bitcoinknots.org/),
  [v29.4.3 release notes](https://github.com/bitcoinknots/bitcoin/releases/tag/v29.4.3.knots20260508),
  [released source](https://github.com/bitcoinknots/bitcoin/tree/a7c4617524c334f542484bfef3919b9f12573840),
  [policy options](https://github.com/bitcoinknots/bitcoin/blob/a7c4617524c334f542484bfef3919b9f12573840/src/init.cpp),
  [coin-control view](https://github.com/bitcoinknots/bitcoin/blob/a7c4617524c334f542484bfef3919b9f12573840/src/qt/forms/coincontroldialog.ui)
  and [Qt version selection](https://github.com/bitcoinknots/bitcoin/blob/a7c4617524c334f542484bfef3919b9f12573840/CMakeLists.txt).

## Which project may fit?

### Bitcoin Core

The appropriate baseline for operators who want the reference implementation,
its standard policy defaults, and the smallest number of project-specific
differences.

### Bitcoin Roots

Intended for operators who want Bitcoin Core-compatible consensus together with
the advanced wallet tools, explicit key-disclosure permissions and effective-policy
reporting shown above, alongside selected conservative Knots-lineage policy and
operator controls.

### Bitcoin Knots

An alternative implementation with additional policy and configuration choices.
The reviewed <code>v29.4.3.knots20260508</code> release includes BLAKE2b
proof-of-work, RDTS and extended coinbase-maturity rules, and does not follow
Bitcoin Core v32.1 consensus. Review its linked release notes before choosing
the network rules to run.

## Frequently asked questions

### Is Bitcoin Roots a new cryptocurrency?

No. Bitcoin Roots is full-node software intended to follow the Bitcoin network
and Bitcoin Core-compatible consensus. It does not introduce a new token.

### Is Bitcoin Roots a network fork?

Bitcoin Roots is a source-code fork built directly on Bitcoin Core, with
selected behavior retained from the Bitcoin Knots lineage. Its stated purpose
is not to create a competing Bitcoin chain.

### Does stricter policy change Bitcoin consensus?

No. Mempool and transaction relay policy govern unconfirmed transactions handled
by the local node. They do not determine whether a block is valid.

### Can a transaction rejected by Bitcoin Roots appear in a valid block?

Yes. A miner may include a consensus-valid transaction that Bitcoin Roots did
not accept into its local mempool. Bitcoin Roots must accept the block if the
block satisfies Bitcoin consensus rules.

### Why preserve features from Bitcoin Knots?

Bitcoin Knots developed useful controls for transaction relay policy, resource
use, privacy, and node operation. Bitcoin Roots preserves selected features
where they remain compatible with its consensus commitment.

### Why does Bitcoin Roots reject RDTS/BIP-110?

Bitcoin Roots does not consider subjective transaction policy a sufficient basis
for changing Bitcoin consensus rules. It keeps conservative filtering at the
node-policy layer while following Bitcoin Core-compatible block validity.

### Does Bitcoin Roots guarantee “no spam”?

No. “Spam” is not an objective consensus category. Bitcoin Roots can apply
conservative local relay and mempool policy, but it must still accept
consensus-valid blocks.

## Continue

- [Read the principles](/principles)
- [Get started](/getting-started)
- [Read the transaction relay policy](/doc/policy/README)
- [View the Bitcoin Roots source](https://github.com/LuganoPlanB/bitcoin-roots)
