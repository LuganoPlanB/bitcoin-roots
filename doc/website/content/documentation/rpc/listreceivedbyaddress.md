---
title: "listreceivedbyaddress"
description: "List balances by receiving address."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# listreceivedbyaddress

List balances by receiving address.

```sh
bitcoin-cli listreceivedbyaddress
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## Arguments

**`minconf`** (number, optional, default 1)

The minimum number of confirmations before payments are included.

**`include_empty`** (boolean, optional, default false)

Whether to include addresses that haven't received any payments.

**`include_watchonly`** (boolean, optional, default false)

(DEPRECATED) No longer used

**`address_filter`** (string, optional)

If present and non-empty, only return information on this address.

**`include_immature_coinbase`** (boolean, optional, default false)

Include immature coinbase transactions.

## Return value

```text
[                           (json array)
  {                         (json object)
    "address" : "str",      (string) The receiving address
    "amount" : n,           (numeric) The total amount in BTC received by the address
    "confirmations" : n,    (numeric) The number of confirmations of the most recent transaction included
    "label" : "str",        (string) The label of the receiving address. The default label is ""
    "txids" : [             (json array)
      "hex",                (string) The ids of transactions received with the address
      ...
    ]
  },
  ...
]
```
