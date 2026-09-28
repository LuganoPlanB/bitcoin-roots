---
title: Principles
description: The Bitcoin Roots principles for Bitcoin consensus, local node policy, upstream development, and reviewable differences.
pageClass: bitcoin-roots-reading
head:
  - - meta
    - property: og:title
      content: Bitcoin Roots Principles
  - - meta
    - property: og:description
      content: The boundary between Bitcoin consensus and local node policy in Bitcoin Roots.
---

# Principles

Bitcoin Roots exists to preserve a clear boundary between Bitcoin consensus and
node policy.

Consensus is shared across the network. Policy belongs to the operator. Bitcoin
Roots provides conservative local controls without turning those preferences
into new rules for Bitcoin.

## 1. Bitcoin consensus is shared

A Bitcoin node independently determines whether blocks and transactions satisfy
Bitcoin consensus rules.

Bitcoin Roots follows Bitcoin Core-compatible consensus. It must accept blocks
that are valid under those rules, including blocks containing transactions that
its local mempool policy would not have accepted or relayed.

## 2. Policy belongs to the operator

Before confirmation, every node chooses which transactions to keep in memory
and which to relay.

These choices can reflect resource limits, security considerations, privacy
requirements, fee policy, or an operator’s preferred use of the network. They
remain local choices. They do not redefine Bitcoin validity for other nodes.

## 3. Consensus neutrality does not require policy neutrality

A node can apply conservative transaction relay and mempool rules while
remaining neutral at the consensus layer.

Bitcoin Roots supports stricter local policy where it is useful, but does not
use that policy to reject otherwise valid Bitcoin blocks.

## 4. Bitcoin Core remains the development trunk

Bitcoin Core is the principal upstream source for consensus, validation,
security, networking, and protocol development.

Suitable Bitcoin Core changes are reviewed and incorporated into Bitcoin Roots
releases. Staying connected to this development trunk reduces unnecessary
divergence and makes differences easier to inspect.

## 5. Selected Knots controls remain useful

Bitcoin Knots developed policy and operator controls that many node operators
found useful.

Bitcoin Roots preserves selected features where they improve transaction policy,
privacy, resource control, or node operation without changing Bitcoin consensus.

These features are selected individually. Bitcoin Roots does not claim automatic
compatibility with every past or future Bitcoin Knots change.

## 6. Consensus changes require exceptional scrutiny

Consensus changes affect every participant who wishes to remain on the same
network. They therefore require a much higher threshold than local policy
changes.

Bitcoin Roots does not enforce RDTS/BIP-110. Concerns about network use should
first be addressed through transparent node and miner policy unless a broadly
supported consensus change is justified through evidence, review, and careful
coordination.

No developer group, miner, company, or single software project can unilaterally
define agreement for the broader Bitcoin network.

## 7. Differences must be visible

Project-specific changes should be documented, reviewable, and testable.

Operators should be able to determine which behaviour comes from Bitcoin Core,
which policy features are preserved from Bitcoin Knots, and which changes are
specific to Bitcoin Roots.

## 8. Security takes priority over novelty

Full-node software handles security-critical data and may protect funds.

Bitcoin Roots should favour careful review, reproducible testing, predictable
operation, and understandable configuration over rapid feature accumulation.

## Practical consequences

In practice, these principles mean:

- Bitcoin Roots remains on the Bitcoin network.
- Bitcoin Roots follows Bitcoin Core-compatible block validity.
- Local policy may reject or decline to relay an unconfirmed transaction.
- The same transaction may later appear in a valid block.
- Bitcoin Roots must accept that valid block.
- Policy differences must not silently become consensus differences.
- Consensus-affecting changes must be prominently identified and reviewed.

## Continue

- [Compare implementations](/compare)
- [Read the transaction relay policy](/doc/policy/README)
- [View the Bitcoin Roots source](https://github.com/LuganoPlanB/bitcoin-roots)
