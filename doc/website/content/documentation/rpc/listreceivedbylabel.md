---
title: "listreceivedbylabel"
description: "List received transactions by label."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# listreceivedbylabel

List received transactions by label.

```sh
bitcoin-cli listreceivedbylabel
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## Arguments

**`minconf`** (number, optional, default 1)

The minimum number of confirmations before payments are included.

**`include_empty`** (boolean, optional, default false)

Whether to include labels that haven't received any payments.

**`include_watchonly`** (boolean, optional, default false)

(DEPRECATED) No longer used

**`include_immature_coinbase`** (boolean, optional, default false)

Include immature coinbase transactions.

## Return value

```text
[                           (json array)
  {                         (json object)
    "amount" : n,           (numeric) The total amount received by addresses with this label
    "confirmations" : n,    (numeric) The number of confirmations of the most recent transaction included
    "label" : "str"         (string) The label of the receiving address. The default label is ""
  },
  ...
]
```
