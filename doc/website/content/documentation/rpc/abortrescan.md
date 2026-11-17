---
title: "abortrescan"
description: "Stops current wallet rescan triggered by an RPC call, e.g."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# abortrescan

Stops current wallet rescan triggered by an RPC call, e.g.

```sh
bitcoin-cli abortrescan
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## How it works

by a rescanblockchain call.
Note: Use "getwalletinfo" to query the scanning progress.

## Arguments

No arguments.

## Return value

`boolean`. Whether the abort was successful
