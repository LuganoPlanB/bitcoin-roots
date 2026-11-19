---
title: "abortprivatebroadcast"
description: "Abort private broadcast attempts for a transaction currently being privately broadcast."
rpcCategory: "rawtransactions"
---

[RPC reference](./) · rawtransactions

# abortprivatebroadcast

Abort private broadcast attempts for a transaction currently being privately broadcast.

```sh
bitcoin-cli abortprivatebroadcast "id"
```

## How it works

The transaction will be removed from the private broadcast queue.
This method is only available when running with -privatebroadcast enabled.

## Arguments

**`id`** (string, required)

A transaction identifier to abort. It will be matched against both txid and wtxid for all transactions in the private broadcast queue.
If the provided id matches a txid that corresponds to multiple transactions with different wtxids, multiple transactions will be removed and returned.

## Return value

```text
{                               (json object)
  "removed_transactions" : [    (json array) Transactions removed from the private broadcast queue
    {                           (json object)
      "txid" : "hex",           (string) The transaction hash in hex
      "wtxid" : "hex",          (string) The transaction witness hash in hex
      "hex" : "hex"             (string) The serialized, hex-encoded transaction data
    },
    ...
  ]
}
```
