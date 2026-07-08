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
they may differ in policy, configuration, release process, and consensus
behaviour.

This page does not rank the projects. It identifies differences that matter when
choosing which software and network rules to run.

## Versions reviewed

- **Bitcoin Core:** [v31.1](https://github.com/bitcoin/bitcoin/releases/tag/v31.1)
- **Bitcoin Roots:** [29.3.0.roots20260507 at d18c044e6a](https://github.com/LuganoPlanB/bitcoin-roots/commit/d18c044e6a30da77ef5bee13423b6e5db6e6bc01)
- **Bitcoin Knots:** [v29.4.1.knots20260508](https://github.com/bitcoinknots/bitcoin/releases/tag/v29.4.1.knots20260508)
- **Date last verified:** 9 September 2026

The initial Bitcoin Roots commit records <code>29.3.knots20260507</code> as its
Knots lineage reference. Its direct parent carries
[<code>29.3.knots20250903</code> package metadata](https://github.com/LuganoPlanB/bitcoin-roots/blob/99ee26e9df0e63a5d1e0ab6bd46b1862ce67648b/CMakeLists.txt),
while the node-source tree closely matches the later Knots release line apart
from release and generated artifacts. The reference should therefore not be
interpreted as an exact Git parent.

## Comparison

<ComparisonTable />

### Sources

The table was checked against these primary sources:

- [Bitcoin Core v31.1 release](https://github.com/bitcoin/bitcoin/releases/tag/v31.1)
  and [source tree](https://github.com/bitcoin/bitcoin/tree/v31.1)
- [Bitcoin Roots current reviewed commit](https://github.com/LuganoPlanB/bitcoin-roots/commit/d18c044e6a30da77ef5bee13423b6e5db6e6bc01),
  [project README](https://github.com/LuganoPlanB/bitcoin-roots/blob/d18c044e6a30da77ef5bee13423b6e5db6e6bc01/README.md),
  and [version configuration](https://github.com/LuganoPlanB/bitcoin-roots/blob/d18c044e6a30da77ef5bee13423b6e5db6e6bc01/CMakeLists.txt)
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
Bitcoin Core <code>v31.1</code>. Operators should examine the consensus rules of
the specific release before upgrading or deploying it.

## Frequently asked questions

### Is Bitcoin Roots a new cryptocurrency?

No. Bitcoin Roots is full-node software intended to follow the Bitcoin network
and Bitcoin Core-compatible consensus. It does not introduce a new token.

### Is Bitcoin Roots a network fork?

Bitcoin Roots is a source-code fork from the Bitcoin Knots lineage. Its stated
purpose is not to create a competing Bitcoin chain.

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

- [Explore user-facing features](/features)
- [Read the principles](/principles)
- [Get started](/getting-started)
- [Read the transaction relay policy](/doc/policy/README)
- [View the Bitcoin Roots source](https://github.com/LuganoPlanB/bitcoin-roots)
