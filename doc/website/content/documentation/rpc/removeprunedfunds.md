---
title: "removeprunedfunds"
description: "(DEPRECATED) This feature will be removed in the next major release."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# removeprunedfunds

(DEPRECATED) This feature will be removed in the next major release.

```sh
bitcoin-cli removeprunedfunds "a8d0c0184dde994a09ec054286f1ce581bebf46446a512166eae7628734ea0a5"
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## How it works

Start bitcoind with the \`-deprecatedrpc=removeprunedfunds\` option in order to use this.
Deletes the specified transaction from the wallet. Meant for use with pruned wallets and as a companion to importprunedfunds. This will affect wallet balances.

## Arguments

**`txid`** (string, required)

The hex-encoded id of the transaction you are deleting

## Return value

`json null`
