---
title: "getmempoolancestors"
description: "If txid is in the mempool, returns all in-mempool ancestors."
rpcCategory: "blockchain"
rootsChange: "The vsize_adjusted field reports virtual size after local policy adjustments."
---

[RPC reference](./) · blockchain

# getmempoolancestors

If txid is in the mempool, returns all in-mempool ancestors.

```sh
bitcoin-cli getmempoolancestors "mytxid"
```

**New or changed in Roots**

The vsize\_adjusted field reports virtual size after local policy adjustments.

## Arguments

**`txid`** (string, required)

The transaction id (must be in mempool)

**`verbose`** (boolean, optional, default false)

True for a json object, false for array of transaction ids

## Return value

for verbose = false

```text
[           (json array)
  "hex",    (string) The transaction id of an in-mempool ancestor transaction
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
