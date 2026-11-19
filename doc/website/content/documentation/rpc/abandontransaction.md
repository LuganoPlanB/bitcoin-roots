---
title: "abandontransaction"
description: "Mark in-wallet transaction <txid> as abandoned This will mark this transaction and all its in-wallet descendants as abandoned which will allow for their inputs to be respent."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# abandontransaction

Mark in-wallet transaction &lt;txid&gt; as abandoned This will mark this transaction and all its in-wallet descendants as abandoned which will allow for their inputs to be respent.

```sh
bitcoin-cli abandontransaction "1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d"
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## How it works

It can be used to replace "stuck" or evicted transactions.
It only works on transactions which are not included in a block and are not currently in the mempool.
It has no effect on transactions which are already abandoned.

## Arguments

**`txid`** (string, required)

The transaction id

## Return value

`json null`
