---
title: "importprunedfunds"
description: "Imports funds without rescan."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# importprunedfunds

Imports funds without rescan.

```sh
bitcoin-cli importprunedfunds "RAW_TRANSACTION_HEX" "PROOF_HEX"
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## How it works

Corresponding address or script must previously be included in wallet. Aimed towards pruned wallets. The end-user is responsible to import additional transactions that subsequently spend the imported outputs or rescan after the point in the blockchain the transaction is included.

## Arguments

**`rawtransaction`** (string, required)

A raw transaction in hex funding an already-existing address in wallet

**`txoutproof`** (string, required)

The hex output from gettxoutproof that contains the transaction

## Return value

`json null`
