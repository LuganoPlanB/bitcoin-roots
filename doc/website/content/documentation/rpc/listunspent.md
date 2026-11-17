---
title: "listunspent"
description: "Returns array of unspent transaction outputs with between minconf and maxconf (inclusive) confirmations."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# listunspent

Returns array of unspent transaction outputs with between minconf and maxconf (inclusive) confirmations.

```sh
bitcoin-cli listunspent
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## How it works

Optionally filter to only include txouts paid to specified addresses.

## Arguments

**`minconf`** (number, optional, default 1)

The minimum confirmations to filter

**`maxconf`** (number, optional, default 9999999)

The maximum confirmations to filter

**`addresses`** (array, optional, default \[\])

The bitcoin addresses to filter

**`include_unsafe`** (boolean, optional, default true)

Include outputs that are not safe to spend
See description of "safe" attribute below.

**`query_options`** (object, optional)

<details>
<summary>Argument formats</summary>

```text
1. minconf           (numeric, optional, default=1) The minimum confirmations to filter
2. maxconf           (numeric, optional, default=9999999) The maximum confirmations to filter
3. addresses         (json array, optional, default=[]) The bitcoin addresses to filter
     [
       "address",    (string) bitcoin address
       ...
     ]
4. include_unsafe    (boolean, optional, default=true) Include outputs that are not safe to spend
                     See description of "safe" attribute below.
5. query_options     (json object, optional) Options object that can be used to pass named arguments, listed below.
```

</details>

<details>
<summary>Named arguments</summary>

```text
minimumAmount                (numeric or string, optional, default="0.00") Minimum value of each UTXO in BTC
maximumAmount                (numeric or string, optional, default=unlimited) Maximum value of each UTXO in BTC
maximumCount                 (numeric, optional, default=unlimited) Maximum number of UTXOs
minimumSumAmount             (numeric or string, optional, default=unlimited) Minimum sum value of all UTXOs in BTC
include_immature_coinbase    (boolean, optional, default=false) Include immature coinbase UTXOs
```

</details>

## Return value

```text
[                                (json array)
  {                              (json object)
    "txid" : "hex",              (string) the transaction id
    "vout" : n,                  (numeric) the vout value
    "address" : "str",           (string, optional) the bitcoin address
    "label" : "str",             (string, optional) The associated label, or "" for the default label
    "scriptPubKey" : "hex",      (string) the output script
    "amount" : n,                (numeric) the transaction output amount in BTC
    "confirmations" : n,         (numeric) The number of confirmations
    "ancestorcount" : n,         (numeric, optional) The number of in-mempool ancestor transactions, including this one (if transaction is in the mempool)
    "ancestorsize" : n,          (numeric, optional) The virtual transaction size of in-mempool ancestors, including this one (if transaction is in the mempool)
    "ancestorfees" : n,          (numeric, optional) The total fees of in-mempool ancestors (including this one) with fee deltas used for mining priority in sat (if transaction is in the mempool)
    "redeemScript" : "hex",      (string, optional) The redeem script if the output script is P2SH
    "witnessScript" : "str",     (string, optional) witness script if the output script is P2WSH or P2SH-P2WSH
    "spendable" : true|false,    (boolean) (DEPRECATED) Always true
    "solvable" : true|false,     (boolean) Whether we know how to spend this output, ignoring the lack of keys
    "reused" : true|false,       (boolean, optional) (only present if avoid_reuse is set) Whether this output is reused/dirty (sent to an address that was previously spent from)
    "desc" : "str",              (string, optional) (only when solvable) A descriptor for spending this output
    "parent_descs" : [           (json array) List of parent descriptors for the output script of this coin.
      "str",                     (string) The descriptor string.
      ...
    ],
    "safe" : true|false          (boolean) Whether this output is considered safe to spend. Unconfirmed transactions
                                 from outside keys and unconfirmed replacement transactions are considered unsafe
                                 and are not eligible for spending by fundrawtransaction and sendtoaddress.
  },
  ...
]
```
