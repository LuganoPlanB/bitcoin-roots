---
title: "keypoolrefill"
description: "Refills each descriptor keypool in the wallet up to the specified number of new keys."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# keypoolrefill

Refills each descriptor keypool in the wallet up to the specified number of new keys.

```sh
bitcoin-cli keypoolrefill
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.
- Unlock an encrypted wallet with [walletpassphrase](./walletpassphrase) first.

## How it works

By default, descriptor wallets have 4 active ranged descriptors ("legacy", "p2sh-segwit", "bech32", "bech32m"), each with 1000 entries.

Requires wallet passphrase to be set with walletpassphrase call if wallet is encrypted.

## Arguments

**`newsize`** (number, optional, default 1000, or as set by -keypool)

The new keypool size

## Return value

`json null`
