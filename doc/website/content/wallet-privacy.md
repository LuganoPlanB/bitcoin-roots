---
title: Wallet privacy tools
description: Bitcoin Roots wallet workflows, current key-disclosure permissions, and historical sweep and coin-control guidance.
pageClass: bitcoin-roots-reading
head:
  - - meta
    - property: og:title
      content: Bitcoin Roots wallet privacy tools
  - - meta
    - property: og:description
      content: Review wallet workflows, key-disclosure permissions, and the limits of sweep and coin-control tools.
---

# Wallet privacy tools

For network routing, private broadcasting, wallet settings, and their defaults,
see [Privacy settings](/privacy).

## Current v32.1 source

Read the current [wallet manual](/doc/wallet-privacy) and
[feature catalogue](/doc/roots-features) for the v32.1 source. Source availability
does not establish a published release; public downloads and patches are listed
in the [release patch browser](/patches/).

`exportaddresskey`, `listdescriptors`, `gethdkeys` and `derivehdkey` are disabled
by default, including public-only modes. Enabling them requires starting the
node with the explicit command-line option `--allow-dangerous-rpc` (or `=1`).
Configuration files, JSON settings and runtime settings cannot enable them;
omitting the option or using `=0` disables them. RPC authentication, selected-wallet
authorization and normal wallet unlock remain required. Private-key disclosure
can compromise other addresses or an HD subtree; review the complete manual
before enabling these methods.

## Historical sweep and coin-control guidance

The published `v29.4-roots.3` release adds two explicit wallet workflows. This
is release documentation. Verify the version and
built-in help of any binary before using these tools.

## Sweep a supported private key

`sweepprivkeys` scans confirmed outputs controlled by a supplied Base58 WIF key
and prepares a transaction to a spendable destination in the selected wallet.
A signed, non-broadcast preview is the default. Review the destination, inputs,
fee, transaction ID, and Replace-By-Fee state before choosing to broadcast.

The supported configuration is a descriptor wallet with local private keys.
P2PKH works with compressed and uncompressed WIF keys. Compressed keys also
support native and wrapped P2WPKH. Taproot and other script forms are not
scanned.

The supplied key is transient: it is not imported into the wallet, saved as a
descriptor or label, written to settings, or included in normal logs. The Qt
dialog masks the key and disables copy, cut, and context-menu actions. A sweep
can be cancelled until broadcast submission begins; once submission begins,
the dialog stays open until the result is known.

::: warning Keep your backup
A sweep is not key backup or wallet recovery. Keep an independent, secure
backup until the transaction confirms.
:::

## Inspect coin-control facts

The advanced coin-control view reports wallet facts for outputs in the normal
available-output list, including amount, effective value, confirmations,
address reuse, grouping, lock and spendability state, input size, fee, change,
and RBF state.

Locked outputs remain visible but cannot be selected. Unsafe and immature
outputs excluded by the wallet's normal availability filters are not added only
to display a status. Material wallet or chain changes clear stale selections so
the operator must review current inputs again.

## Privacy boundary

These tools do not add CoinJoin, payjoin, automatic consolidation, private-key
export, a transaction-history product, chain-analysis scoring, or a privacy
score. They do not provide an anonymity guarantee. They expose transaction
facts so the operator can make and verify an explicit choice.

Bitcoin Roots continues to follow Bitcoin Core-compatible consensus. These
wallet tools do not change block validity, the P2P wire protocol, transaction
serialization, or the wallet database format.

## Verify a candidate build

Use the executable itself as the authority:

```sh
bitcoin-cli --version
bitcoin-cli help sweepprivkeys
```

Review the release's complete
[wallet privacy manual](https://github.com/LuganoPlanB/bitcoin-roots/blob/v29.4-roots.3/doc/wallet-privacy.md)
and
[release notes](https://github.com/LuganoPlanB/bitcoin-roots/blob/v29.4-roots.3/doc/release-notes.md)
with the source and tests before using these workflows.
