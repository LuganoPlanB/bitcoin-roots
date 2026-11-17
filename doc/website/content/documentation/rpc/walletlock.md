---
title: "walletlock"
description: "Removes the wallet encryption key from memory, locking the wallet."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# walletlock

Removes the wallet encryption key from memory, locking the wallet.

```sh
bitcoin-cli walletlock
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## How it works

After calling this method, you will need to call walletpassphrase again
before being able to call any methods which require the wallet to be unlocked.

## Arguments

No arguments.

## Return value

`json null`
