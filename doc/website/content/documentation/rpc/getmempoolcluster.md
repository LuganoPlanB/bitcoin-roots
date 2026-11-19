---
title: "getmempoolcluster"
description: "Returns mempool data for given cluster"
rpcCategory: "blockchain"
rootsChange: "Cluster and chunk weights include local policy adjustments."
---

[RPC reference](./) · blockchain

# getmempoolcluster

Returns mempool data for given cluster

```sh
bitcoin-cli getmempoolcluster txid
```

**New or changed in Roots**

Cluster and chunk weights include local policy adjustments.

## Arguments

**`txid`** (string, required)

The txid of a transaction in the cluster

## Return value

```text
{                           (json object)
  "clusterweight" : n,      (numeric) total policy weight including sigop and data-carrier cost adjustments
  "txcount" : n,            (numeric) number of transactions
  "chunks" : [              (json array) chunks in this cluster (in mining order)
    {                       (json object)
      "chunkfee" : n,       (numeric) modified policy fees of the transactions in this chunk, including operator deltas and admission dust deficits
      "chunkweight" : n,    (numeric) policy weight including sigop and data-carrier cost adjustments of all transactions in this chunk
      "txs" : [             (json array) transactions in this chunk in mining order
        "hex",              (string) transaction id
        ...
      ]
    },
    ...
  ]
}
```
