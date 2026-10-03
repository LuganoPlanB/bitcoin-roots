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

Bitcoin Core, Bitcoin Roots, and Bitcoin Knots share a common code lineage, but
they differ in policy, configuration, release process, wallet tools, and, for
some releases, consensus behaviour.

This page does not rank the projects. It identifies differences that matter when
choosing which software and network rules to run.

## Versions reviewed

- **Bitcoin Roots:** [v29.4-roots.2](https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/v29.4-roots.2)
- **Bitcoin Knots feature lineage:** [v29.3.knots20260507](https://github.com/bitcoinknots/bitcoin/releases/tag/v29.3.knots20260507)
- **Bitcoin Core direct base:** [v29.4](https://github.com/bitcoin/bitcoin/releases/tag/v29.4)
- **Current Knots consensus note:** [v29.4.1.knots20260508](https://github.com/bitcoinknots/bitcoin/releases/tag/v29.4.1.knots20260508)
- **Date last verified:** 28 September 2026

Bitcoin Roots v29.4-roots.2 starts directly from Bitcoin Core v29.4 and applies
a reviewed Roots patch. The patch preserves selected behaviour whose historical
lineage includes Bitcoin Knots v29.3.knots20260507; Knots is not an upstream
dependency and its full feature set is not imported. The table compares the
matched v29.4 Core base and the Knots lineage release for feature presence. It
mentions Knots v29.4.1 separately where that later release changes consensus.

## Comparison

<ComparisonTable />

The table is a feature inventory, not a claim that similarly named controls have
identical defaults or implementation details. “Included” means the reviewed
version exposes the described capability. For exact defaults and accepted
values, use that version's built-in help. It lists every operator-facing
Knots-lineage capability maintained in the released Roots feature catalog;
internal wallet, build, and platform hardening is grouped rather than presented
as dozens of indistinguishable implementation rows.

### Wallet scope

All three reviewed code lines include wallet-side coin selection and the Qt coin
control dialog. Bitcoin Roots v29.4-roots.2 additionally makes the effective
per-send RBF choice explicit and initializes it from the wallet default.

Private-key sweeping and the advanced privacy-aware coin-control view are
deliberately **not** included in the published v29.4-roots.2 release. They are
implemented and tested in the intended v29.4-roots.3 candidate, with wallet-owned
data and transaction contracts and Qt presenting the choices and consequences.
That candidate status is not a release announcement. [Review the candidate
wallet tools](/wallet-privacy) before using a build that contains them.

### Sources

The table was checked against these primary sources:

- [Bitcoin Roots v29.4-roots.2 release](https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/v29.4-roots.2),
  [feature catalog](https://github.com/LuganoPlanB/bitcoin-roots/blob/v29.4-roots.2/doc/roots-features.md),
  and [source tree](https://github.com/LuganoPlanB/bitcoin-roots/tree/v29.4-roots.2)
- [Bitcoin Core v29.4 release](https://github.com/bitcoin/bitcoin/releases/tag/v29.4)
  and [source tree](https://github.com/bitcoin/bitcoin/tree/v29.4)
- [Bitcoin Knots v29.3.knots20260507 release](https://github.com/bitcoinknots/bitcoin/releases/tag/v29.3.knots20260507)
  and [source tree](https://github.com/bitcoinknots/bitcoin/tree/v29.3.knots20260507)
- [Bitcoin Roots initial lineage commit](https://github.com/LuganoPlanB/bitcoin-roots/commit/07580114c35e870e242621316ec8cd051a938787)
  and [its direct parent version metadata](https://github.com/LuganoPlanB/bitcoin-roots/blob/99ee26e9df0e63a5d1e0ab6bd46b1862ce67648b/CMakeLists.txt)
- [Bitcoin Knots v29.4.1.knots20260508 release](https://github.com/bitcoinknots/bitcoin/releases/tag/v29.4.1.knots20260508)
  and [source tree](https://github.com/bitcoinknots/bitcoin/tree/v29.4.1.knots20260508)
- [BIP-110 specification](https://github.com/bitcoin/bips/blob/master/bip-0110.mediawiki)

## Which project may fit?

### Bitcoin Core

The appropriate baseline for operators who want the reference implementation,
its standard policy defaults, and the smallest number of project-specific
differences.

### Bitcoin Roots

Intended for operators who want Bitcoin Core-compatible consensus together with
selected conservative policy, privacy, resource, and operational controls
inherited from the Bitcoin Knots lineage.

### Bitcoin Knots

An alternative implementation with additional policy and configuration choices.
Version <code>v29.4.1.knots20260508</code> is not consensus-compatible with
Bitcoin Core <code>v29.4</code>. Operators should examine the consensus rules of
the specific release before upgrading or deploying it.

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
