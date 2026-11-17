---
title: "getreceivedbylabel"
description: "Returns the total amount received by addresses with <label> in transactions with at least [minconf] confirmations."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# getreceivedbylabel

Returns the total amount received by addresses with &lt;label&gt; in transactions with at least \[minconf\] confirmations.

```sh
bitcoin-cli getreceivedbylabel ""
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## Arguments

**`label`** (string, required)

The selected label, may be the default label using "".

**`minconf`** (number, optional, default 1)

Only include transactions confirmed at least this many times.

**`include_immature_coinbase`** (boolean, optional, default false)

Include immature coinbase transactions.

## Return value

`numeric`. The total amount in BTC received for this label.
