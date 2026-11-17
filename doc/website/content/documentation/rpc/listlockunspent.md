---
title: "listlockunspent"
description: "Returns list of temporarily unspendable outputs."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# listlockunspent

Returns list of temporarily unspendable outputs.

```sh
bitcoin-cli listlockunspent
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## How it works

See the lockunspent call to lock and unlock transactions for spending.

## Arguments

No arguments.

## Return value

```text
[                      (json array)
  {                    (json object)
    "txid" : "hex",    (string) The transaction id locked
    "vout" : n         (numeric) The vout value
  },
  ...
]
```
