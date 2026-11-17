---
title: "getreceivedbyaddress"
description: "Returns the total amount received by the given address in transactions with at least minconf confirmations."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# getreceivedbyaddress

Returns the total amount received by the given address in transactions with at least minconf confirmations.

```sh
bitcoin-cli getreceivedbyaddress "bc1q09vm5lfy0j5reeulh4x5752q25uqqvz34hufdl"
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## Arguments

**`address`** (string, required)

The bitcoin address for transactions.

**`minconf`** (number, optional, default 1)

Only include transactions confirmed at least this many times.

**`include_immature_coinbase`** (boolean, optional, default false)

Include immature coinbase transactions.

## Return value

`numeric`. The total amount in BTC received at this address.
