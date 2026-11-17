---
title: Principles
description: How Bitcoin Roots checks blocks, handles transactions and builds on Bitcoin Core.
pageClass: bitcoin-roots-reading
---

# Principles

## Every node checks the blocks

Bitcoin Roots follows the same rules as Bitcoin Core when it decides whether a block is valid. Those shared rules are called *consensus*. They let people run different software and still agree on Bitcoin’s history.

## You choose what your node relays

Before a transaction appears in a block, your node can hold it in its *mempool* and pass it on to peers. You can set limits on what it keeps and shares. These choices are your node’s *policy*.

For example, your node might decline to pass on a transaction carrying more data than you allow. If a miner later includes that transaction in a valid block, your node accepts the block. The project keeps those two decisions separate. Roots does not enforce RDTS/BIP-110.

## Where Roots comes from

Each Roots release starts with Bitcoin Core. Roots also carries selected wallet, networking and policy tools developed in Bitcoin Knots. The project publishes its changes as [release patches](/patches/) so anyone can see what changed and review the code.

Changes to the rules for valid blocks need broad agreement across Bitcoin. For local choices, the project favours settings people can understand, test and change for themselves.

[Compare Bitcoin Core and Bitcoin Roots](/compare)
