---
title: "getbalance"
description: "Returns the total available balance."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# getbalance

Returns the total available balance.

```sh
bitcoin-cli getbalance
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## How it works

The available balance is what the wallet considers currently spendable, and is
thus affected by options which limit spendability such as -spendzeroconfchange.

## Arguments

**`dummy`** (string, optional)

Remains for backward compatibility. Must be excluded or set to "\*".

**`minconf`** (number, optional, default 0)

Only include transactions confirmed at least this many times.

**`include_watchonly`** (boolean, optional, default false)

No longer used

**`avoid_reuse`** (boolean, optional, default true)

(only available if avoid\_reuse wallet flag is set) Do not include balance in dirty outputs; addresses are considered dirty if they have previously been used in a transaction.

## Return value

`numeric`. The total amount in BTC received for this wallet.
