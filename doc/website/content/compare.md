---
title: Compare Bitcoin Core and Bitcoin Roots
description: See what the two nodes share and where their wallet and transaction settings differ.
pageClass: bitcoin-roots-reading bitcoin-roots-compare
---

# Compare Core and Roots

Bitcoin Roots is built on Bitcoin Core. Both check blocks using the same Bitcoin rules. The differences show up mainly in how your node handles unconfirmed transactions and what you can do with the wallet.

## At a glance

<ComparisonTable />

## A closer look

A node’s *mempool* holds transactions waiting for confirmation. Each node decides which ones to accept and pass on. Even if your node skips a transaction, it will accept a valid block that includes it.

<ComparisonTable detailed />

For example, you can sign a note to show someone that you control a receiving address without sending any bitcoin. Your wallet needs the keys and scripts required to sign for that address. The recipient can check the note with `verifymessage`. See [how to sign and verify a message](/doc/wallet-privacy#sign-and-verify-a-message) for the supported proofs and their limits.

This comparison covers [Bitcoin Core v32.1](https://github.com/bitcoin/bitcoin/tree/v32.1) and [Bitcoin Roots v32.1-roots.2](https://github.com/LuganoPlanB/bitcoin-roots/releases/tag/v32.1-roots.2). For exact settings and limits, see the [Core policy options](https://github.com/bitcoin/bitcoin/blob/v32.1/src/init.cpp) and the [Roots feature catalogue](https://github.com/LuganoPlanB/bitcoin-roots/blob/v32.1-roots.2/doc/roots-features.md).
