---
title: User-facing features
description: The controls, tools, and release details that users can see in Bitcoin Roots beyond Bitcoin Core 29.3.
pageClass: bitcoin-roots-reading bitcoin-roots-features
head:
  - - meta
    - property: og:title
      content: User-facing features in Bitcoin Roots
  - - meta
    - property: og:description
      content: A source-based guide to the visible policy controls and operator tools in Bitcoin Roots.
---

# What Bitcoin Roots adds for users

Bitcoin Roots keeps the familiar full-node, wallet, command-line, and Qt
experiences of Bitcoin Core. Its most visible additions are finer control over
local transaction policy and several operator tools in the graphical
interface.

This guide compares the current Bitcoin Roots source with **Bitcoin Core 29.3**.
It describes what a user can see and operate, not every internal source-code
difference.

::: info Provenance matters
Most features on this page are **Inherited from Bitcoin Knots**. Bitcoin Roots
preserves them because they are useful to node operators while keeping its own
consensus commitment. “Adds” here means included beyond the Bitcoin Core 29.3
baseline, not originally invented by the Roots project.
:::

## Practical overview

- **Mempool controls:** Find them in **Settings → Options → Mempool**. Set
  replacement, capacity, expiry, and related unconfirmed-transaction behaviour.
- **Spam filtering controls:** Find them in **Settings → Options → Spam
  filtering**. Choose which unconfirmed transactions your node stores, relays,
  or offers to miners.
- **Mining policy controls:** Find them in **Settings → Options → Mining**. Set
  transaction-fee and block-template limits for local mining.
- **Network Watch:** Open **Window → Watch network activity** to inspect
  peer-to-peer transaction and block activity as it arrives.
- **Mempool Statistics:** Open **Window → Mempool Statistics** to follow
  mempool size, transaction count, and fee conditions over time.
- **Block Visualizer:** Open **Window → Block Visualizer** to explore the
  composition of blocks and inspect their transactions.
- **Pairing:** Open **Pairing** in the main window or node window to expose the
  node's Tor onion address and, when QR support is built, a pairing code.
- **Sweep private key:** Open **File → Sweep private key** to move funds
  controlled by an imported private key into an open wallet.

## Policy controls in Options

### Mempool

Use the dedicated **Mempool** tab to tune unconfirmed-transaction behaviour and
resource use. Its controls cover transaction replacement, incremental relay
fees, address reuse, TRUC handling, orphan transactions, maximum mempool size,
and mempool expiry.

### Spam filtering

Use the **Spam filtering** tab to choose which unconfirmed transactions your
node accepts, relays, and offers to miners. The controls include:

- unknown scripts and witness versions;
- parasite and non-bitcoin token or asset protocols;
- fee, dust, coin-age, and confirmation thresholds;
- script, signature-operation, ancestor, and descendant limits;
- bare, anchor, data-only, and embedded-data transaction patterns.

This tab is inherited from Bitcoin Knots. It provides individual policy choices
rather than a single all-or-nothing “spam” switch.

### Mining

Use the **Mining** tab to set the minimum transaction fee for a local block
template, maximum block size and weight, and the area reserved for coin-age
priority. Mempool and Spam filtering choices also influence which transactions
the local template builder can select.

::: warning Policy is not consensus
Everything in these tabs governs **local mempool, relay, and mining-template
policy**. Changing it does not change Bitcoin consensus. A transaction your
node refuses to relay can still be valid, and Bitcoin Roots must accept a valid
block containing it.
:::

## Tools in the graphical interface

### Network Watch

Open **Window → Watch network activity** for a live view of transactions and
blocks observed by the node. It is useful for quick inspection; use the debug
log or RPC interface for detailed diagnosis.

### Mempool Statistics

Open **Window → Mempool Statistics** to chart memory use, transaction count, and
the current minimum fee condition over selectable time windows.

### Block Visualizer

Open **Window → Block Visualizer** to inspect the composition of a selected
block or follow the active chain tip. Point to a transaction to see its details.

## Wallet and connection tools

### Sweep private key

Open **File → Sweep private key** to transfer funds controlled by a supplied
private key into an open wallet. Sweeping moves the funds; it does not add the
external key to the wallet's ongoing key set.

Treat any exposed private key as sensitive. Confirm the destination wallet and
keep the key out of screenshots, clipboard history, and support messages.

### Pairing

Open **Pairing** to let another application or device discover the node over
Tor. When Tor is connected, the page presents the node's onion address. Builds
with QR-code support also show a scannable `bitcoin-p2p://` pairing URI.

Pairing is marked experimental in the application. Its address format may
change, and an upgrade may require pairing again.

## What release users notice

Official release builds use the Bitcoin Roots name and root-shaped Bitcoin mark.
The release tag appears in command-line version output, manual pages, Windows
metadata, and the Qt About and splash interfaces. Archives unpack into a
versioned `bitcoin-roots-…` directory so their contents stay together.

Project release builds also enable QR-code support, including the Pairing page.

## What Roots does not change

Bitcoin Roots does not introduce a separate coin, network, or blockchain. It
does not turn subjective transaction filtering into block-invalidity rules, and
it does not enforce RDTS/BIP-110 consensus rules.

The separation is intentional:

- **Consensus** decides whether a block is valid.
- **Local policy** decides which unconfirmed transactions this node stores,
  relays, and selects for its own block templates.

For the detailed boundary, read the [transaction relay policy
documentation](/doc/policy/README). For a project-level view, see the
[implementation comparison](/compare) and [Bitcoin Roots principles](/principles).

## Check a specific build

Exact controls and defaults can change between tagged releases. For a specific
build, use its Help and Options interfaces as the authority. Developers can
trace the visible controls through
[`optionsdialog.cpp`](https://github.com/LuganoPlanB/bitcoin-roots/blob/main/src/qt/optionsdialog.cpp)
and the graphical tools through
[`bitcoingui.cpp`](https://github.com/LuganoPlanB/bitcoin-roots/blob/main/src/qt/bitcoingui.cpp).

This overview was checked against the current Roots source, the supplied Bitcoin
Core 29.3 baseline, and the inherited Knots history.
