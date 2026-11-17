---
title: "gettxspendingprevout"
description: "Scans the mempool (and the txospenderindex, if available) to find transactions spending any of the given outputs"
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# gettxspendingprevout

Scans the mempool (and the txospenderindex, if available) to find transactions spending any of the given outputs

```sh
bitcoin-cli gettxspendingprevout "[{\"txid\":\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\",\"vout\":3}]"
```

## Arguments

**`outputs`** (array, required)

The transaction outputs that we want to check, and within each, the txid (string) vout (numeric).

**`options`** (object, optional)

<details>
<summary>Argument formats</summary>

```text
1. outputs                 (json array, required) The transaction outputs that we want to check, and within each, the txid (string) vout (numeric).
     [
       {                   (json object)
         "txid": "hex",    (string, required) The transaction id
         "vout": n,        (numeric, required) The output number
       },
       ...
     ]
2. options                 (json object, optional) Options object that can be used to pass named arguments, listed below.
```

</details>

<details>
<summary>Named arguments</summary>

```text
mempool_only          (boolean, optional, default=true if txospenderindex unavailable, otherwise false) If false and mempool lacks a relevant spend, use txospenderindex (throws an exception if not available).
return_spending_tx    (boolean, optional, default=false) If true, return the full spending tx.
```

</details>

## Return value

```text
[                              (json array)
  {                            (json object)
    "txid" : "hex",            (string) the transaction id of the checked output
    "vout" : n,                (numeric) the vout value of the checked output
    "spendingtxid" : "hex",    (string, optional) the transaction id of the mempool transaction spending this output (omitted if unspent)
    "spendingtx" : "hex",      (string, optional) the transaction spending this output (only if return_spending_tx is set, omitted if unspent)
    "blockhash" : "hex"        (string, optional) the hash of the spending block (omitted if unspent or the spending tx is not confirmed)
  },
  ...
]
```
