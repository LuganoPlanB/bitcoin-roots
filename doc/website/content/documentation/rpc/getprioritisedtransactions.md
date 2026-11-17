---
title: "getprioritisedtransactions"
description: "Returns a map of all user-created (see prioritisetransaction) fee deltas by txid, and whether the tx is present in mempool."
rpcCategory: "mining"
---

[RPC reference](./) · mining

# getprioritisedtransactions

Returns a map of all user-created (see prioritisetransaction) fee deltas by txid, and whether the tx is present in mempool.

```sh
bitcoin-cli getprioritisedtransactions
```

## Arguments

No arguments.

## Return value

```text
{                                 (json object) prioritisation keyed by txid
  "<transactionid>" : {           (json object)
    "fee_delta" : n,              (numeric) transaction fee delta in satoshis
    "in_mempool" : true|false,    (boolean) whether this transaction is currently in mempool
    "modified_fee" : n            (numeric, optional) modified fee in satoshis. Only returned if in_mempool=true
  },
  ...
}
```
