---
title: Wallet privacy tools
description: Choose which coins to spend, review a sweep, and understand who can use a spending key.
pageClass: bitcoin-roots-reading
---

# Wallet privacy tools

## Choose which coins to spend

When you pay someone, your wallet selects coins from earlier payments to cover the amount and the fee. Spending two coins together can reveal that they belong to the same person. **Coin control** lets you review the selection and pick the coins yourself.

Roots shows the value left after fees, whether an address has been reused, and which coins are locked. Locked coins stay visible but cannot be selected. If the selection changes while you’re reviewing it, check the inputs and fee again before signing. These tools do not add CoinJoin or payjoin.

## Move coins from a private key

A **sweep** moves confirmed coins from a supported private key into your wallet. Roots can show you a signed preview before it sends anything. Check the receiving address, amount and fee. You can cancel until broadcast begins. Keep an independent backup of the original key until the transfer confirms.

Sweeping requires a wallet that can sign locally. Only certain address types are supported, and combining coins in a sweep can link them on the blockchain. See the [wallet manual](/doc/wallet-privacy) for supported keys and addresses.

## Access to private keys

Anyone who obtains a spending key may be able to spend the coins it controls. The commands `exportaddresskey`, `listdescriptors`, `gethdkeys` and `derivehdkey` are disabled by default. They require the startup option `--allow-dangerous-rpc`. Configuration files and settings cannot enable them. Wallet permissions and unlock requirements still apply.

For how your node connects to peers, see [privacy settings](/privacy).
