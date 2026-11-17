---
title: "submitpackage"
description: "Submit a package of raw transactions (serialized, hex-encoded) to local node."
rpcCategory: "rawtransactions"
rootsChange: "The vsize_adjusted field reports virtual size after local policy adjustments."
---

[RPC reference](./) · rawtransactions

# submitpackage

Submit a package of raw transactions (serialized, hex-encoded) to local node.

```sh
bitcoin-cli submitpackage '["raw-tx-without-unconfirmed-parents"]'
```

**New or changed in Roots**

The vsize\_adjusted field reports virtual size after local policy adjustments.

## How it works

The package will be validated according to consensus and mempool policy rules. If any transaction passes, it will be accepted to mempool.
This RPC is experimental and the interface may be unstable. Refer to doc/policy/packages.md for documentation on package policies.
Warning: successful submission does not mean the transactions will propagate throughout the network.

## Arguments

**`package`** (array, required)

An array of raw transactions.
The package must consist of a transaction with (some, all, or none of) its unconfirmed parents. A single transaction is permitted.
None of the parents may depend on each other. Parents that are already in mempool do not need to be present in the package.
The package must be topologically sorted, with the child being the last element in the array if there are multiple elements.

**`maxfeerate`** (number or string, optional, default "0.10")

Reject transactions whose fee rate is higher than the specified value, expressed in BTC/kvB.
Fee rates larger than 1BTC/kvB are rejected.
Set to 0 to accept any fee rate.

**`maxburnamount`** (number or string, optional, default "0.00")

Reject transactions with provably unspendable outputs (e.g. 'datacarrier' outputs that use the OP\_RETURN opcode) greater than the specified value, expressed in BTC.
If burning funds through unspendable outputs is desired, increase this value.
This check is based on heuristics and does not guarantee spendability of outputs.

<details>
<summary>Argument formats</summary>

```text
1. package          (json array, required) An array of raw transactions.
                    The package must consist of a transaction with (some, all, or none of) its unconfirmed parents. A single transaction is permitted.
                    None of the parents may depend on each other. Parents that are already in mempool do not need to be present in the package.
                    The package must be topologically sorted, with the child being the last element in the array if there are multiple elements.
     [
       "rawtx",     (string)
       ...
     ]
2. maxfeerate       (numeric or string, optional, default="0.10") Reject transactions whose fee rate is higher than the specified value, expressed in BTC/kvB.
                    Fee rates larger than 1BTC/kvB are rejected.
                    Set to 0 to accept any fee rate.
3. maxburnamount    (numeric or string, optional, default="0.00") Reject transactions with provably unspendable outputs (e.g. 'datacarrier' outputs that use the OP_RETURN opcode) greater than the specified value, expressed in BTC.
                    If burning funds through unspendable outputs is desired, increase this value.
                    This check is based on heuristics and does not guarantee spendability of outputs.
```

</details>

## Return value

```text
{                                   (json object)
  "package_msg" : "str",            (string) The transaction package result message. "success" indicates all transactions were accepted into or are already in the mempool.
  "tx-results" : {                  (json object) The transaction results keyed by wtxid. An entry is returned for every submitted wtxid.
    "wtxid" : {                     (json object) transaction wtxid
      "txid" : "hex",               (string) The transaction hash in hex
      "other-wtxid" : "hex",        (string, optional) The wtxid of a different transaction with the same txid but different witness found in the mempool. This means the submitted transaction was ignored.
      "vsize_adjusted" : n,         (numeric, optional) Policy virtual size including sigop and data-carrier cost adjustments.
      "vsize" : n,                  (numeric, optional) (DEPRECATED) Was previously erroneously described as the BIP 141 vsize, but is actually policy-adjusted vsize.
                                    Use vsize_bip141 to actually get that behavior or switch to the explicit vsize_adjusted for retained behavior.
      "vsize_bip141" : n,           (numeric, optional) Virtual transaction size as defined in BIP 141.
      "fees" : {                    (json object, optional) Transaction fees
        "base" : n,                 (numeric) transaction fee in BTC
        "effective-feerate" : n,    (numeric, optional) if the transaction was not already in the mempool, the effective feerate in BTC per KvB. For example, the package feerate and/or feerate with operator deltas and admission dust deficits.
        "effective-includes" : [    (json array, optional) if effective-feerate is provided, the wtxids of the transactions whose fees and vsizes are included in effective-feerate.
          "hex",                    (string) transaction wtxid in hex
          ...
        ]
      },
      "error" : "str"               (string, optional) Error string if rejected from mempool, or "package-not-validated" when the package aborts before any per-tx processing.
    },
    ...
  },
  "replaced-transactions" : [       (json array, optional) List of txids of replaced transactions
    "hex",                          (string) The transaction id
    ...
  ]
}
```
