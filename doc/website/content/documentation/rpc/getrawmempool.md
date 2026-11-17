---
title: "getrawmempool"
description: "Returns all transaction ids in memory pool as a json array of string transaction ids."
rpcCategory: "blockchain"
rootsChange: "The vsize_adjusted field reports virtual size after local policy adjustments."
---

[RPC reference](./) · blockchain

# getrawmempool

Returns all transaction ids in memory pool as a json array of string transaction ids.

```sh
bitcoin-cli getrawmempool true
```

**New or changed in Roots**

The vsize\_adjusted field reports virtual size after local policy adjustments.

## How it works

Hint: use getmempoolentry to fetch a specific transaction from the mempool.

## Arguments

**`verbose`** (boolean, optional, default false)

True for a json object, false for array of transaction ids

**`mempool_sequence`** (boolean, optional, default false)

If verbose=false, returns a json object with transaction list and mempool sequence number attached.

## Return value

for verbose = false

```text
[           (json array)
  "hex",    (string) The transaction id
  ...
]
```

for verbose = true

```text
{                                  (json object)
  "transactionid" : {              (json object)
    "vsize" : n,                   (numeric) (DEPRECATED) Was previously erroneously described as the BIP 141 vsize, but is actually policy-adjusted vsize.
                                   Use vsize_bip141 to actually get that behavior or switch to the explicit vsize_adjusted for retained behavior.
    "vsize_bip141" : n,            (numeric) Virtual transaction size as defined in BIP 141.
                                   This is different from actual serialized size for witness transactions as witness data is discounted.
    "vsize_adjusted" : n,          (numeric) Policy virtual size including sigop and data-carrier cost adjustments.
    "weight" : n,                  (numeric) transaction weight as defined in BIP 141.
    "time" : xxx,                  (numeric) local time transaction entered pool in seconds since 1 Jan 1970 GMT
    "height" : n,                  (numeric) block height when transaction entered pool
    "descendantcount" : n,         (numeric) number of in-mempool descendant transactions (including this one)
    "descendantsize" : n,          (numeric) virtual transaction size of in-mempool descendants (including this one)
    "ancestorcount" : n,           (numeric) number of in-mempool ancestor transactions (including this one)
    "ancestorsize" : n,            (numeric) virtual transaction size of in-mempool ancestors (including this one)
    "chunkweight" : n,             (numeric) Policy weight including sigop and data-carrier cost adjustments of this transaction's chunk
    "wtxid" : "hex",               (string) hash of serialized transaction, including witness data
    "fees" : {                     (json object)
      "base" : n,                  (numeric) transaction fee, denominated in BTC
      "modified" : n,              (numeric) transaction fee with operator deltas and admission dust deficit used for policy, denominated in BTC
      "ancestor" : n,              (numeric) transaction fees of in-mempool ancestors (including this one) with operator deltas and admission dust deficits used for policy, denominated in BTC
      "descendant" : n,            (numeric) transaction fees of in-mempool descendants (including this one) with operator deltas and admission dust deficits used for policy, denominated in BTC
      "chunk" : n                  (numeric) transaction fees of chunk, denominated in BTC
    },
    "depends" : [                  (json array) unconfirmed transactions used as inputs for this transaction
      "hex",                       (string) parent transaction id
      ...
    ],
    "spentby" : [                  (json array) unconfirmed transactions spending outputs from this transaction
      "hex",                       (string) child transaction id
      ...
    ],
    "unbroadcast" : true|false     (boolean) Whether this transaction is currently unbroadcast (initial broadcast not yet acknowledged by any peers)
  },
  ...
}
```

for verbose = false and mempool\_sequence = true

```text
{                            (json object)
  "txids" : [                (json array)
    "hex",                   (string) The transaction id
    ...
  ],
  "mempool_sequence" : n     (numeric) The mempool sequence value.
}
```
