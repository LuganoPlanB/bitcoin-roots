---
title: "listwallets"
description: "Returns a list of currently loaded wallets."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# listwallets

Returns a list of currently loaded wallets.

```sh
bitcoin-cli listwallets
```

## How it works

For full information on the wallet, use "getwalletinfo"

## Arguments

No arguments.

## Return value

```text
[           (json array)
  "str",    (string) the wallet name
  ...
]
```
