---
title: "rescanblockchain"
description: "Rescan the local blockchain for wallet related transactions."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# rescanblockchain

Rescan the local blockchain for wallet related transactions.

```sh
bitcoin-cli rescanblockchain 100000 120000
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## How it works

Note: Use "getwalletinfo" to query the scanning progress.
The rescan is significantly faster if block filters are available
(using startup option "-blockfilterindex=1").

## Arguments

**`start_height`** (number, optional, default 0)

block height where the rescan should start

**`stop_height`** (number, optional)

the last block height that should be scanned. If none is provided it will rescan up to the tip at return time of this call.

## Return value

```text
{                        (json object)
  "start_height" : n,    (numeric) The block height where the rescan started (the requested height or 0)
  "stop_height" : n      (numeric) The height of the last rescanned block. May be null in rare cases if there was a reorg and the call didn't scan any blocks because they were already scanned in the background.
}
```
