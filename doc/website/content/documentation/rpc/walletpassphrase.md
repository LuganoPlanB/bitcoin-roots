---
title: "walletpassphrase"
description: "Stores the wallet decryption key in memory for 'timeout' seconds."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# walletpassphrase

Stores the wallet decryption key in memory for 'timeout' seconds.

```sh
bitcoin-cli walletpassphrase "my pass phrase" 60
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## How it works

This is needed prior to performing transactions related to private keys such as sending bitcoins

Note:
Issuing the walletpassphrase command while the wallet is already unlocked will set a new unlock
time that overrides the old one.

## Arguments

**`passphrase`** (string, required)

The wallet passphrase

**`timeout`** (number, required)

The time to keep the decryption key in seconds; capped at 100000000 (~3 years).

## Return value

`json null`
