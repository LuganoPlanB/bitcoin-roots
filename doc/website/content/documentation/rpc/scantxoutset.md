---
title: "scantxoutset"
description: "Scans the unspent transaction output set for entries that match certain output descriptors."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# scantxoutset

Scans the unspent transaction output set for entries that match certain output descriptors.

```sh
bitcoin-cli scantxoutset start '["raw(76a91411b366edfc0a8b66feebae5c2e25a7b6a5d1cf3188ac)#fm24fxxy"]'
```

## How it works

Examples of output descriptors are:
    addr(&lt;address&gt;)                      Outputs whose output script corresponds to the specified address (does not include P2PK)
    raw(&lt;hex script&gt;)                    Outputs whose output script equals the specified hex-encoded bytes
    combo(&lt;pubkey&gt;)                      P2PK, P2PKH, P2WPKH, and P2SH-P2WPKH outputs for the given pubkey
    pkh(&lt;pubkey&gt;)                        P2PKH outputs for the given pubkey
    sh(multi(&lt;n&gt;,&lt;pubkey&gt;,&lt;pubkey&gt;,...)) P2SH-multisig outputs for the given threshold and pubkeys
    tr(&lt;pubkey&gt;)                         P2TR
    tr(&lt;pubkey&gt;,&\#123;pk(&lt;pubkey&gt;)&\#125;)          P2TR with single fallback pubkey in tapscript
    rawtr(&lt;pubkey&gt;)                      P2TR with the specified key as output key rather than inner
    wsh(and\_v(v:pk(&lt;pubkey&gt;),after(2)))  P2WSH miniscript with mandatory pubkey and a timelock

In the above, &lt;pubkey&gt; either refers to a fixed public key in hexadecimal notation, or to an xpub/xprv optionally followed by one
or more path elements separated by "/", and optionally ending in "/\*" (unhardened), or "/\*'" or "/\*h" (hardened) to specify all
unhardened or hardened child keys.
In the latter case, a range needs to be specified by below if different from 1000.
For more information on output descriptors, see the documentation in the doc/descriptors.md file.

## Arguments

**`action`** (string, required)

The action to execute
"start" for starting a scan
"abort" for aborting the current scan (returns true when abort was successful)
"status" for progress report (in %) of the current scan

**`scanobjects`** (array, optional)

Array of scan objects. Required for "start" action
Every scan object is either a string descriptor or an object:

<details>
<summary>Argument formats</summary>

```text
1. action                        (string, required) The action to execute
                                 "start" for starting a scan
                                 "abort" for aborting the current scan (returns true when abort was successful)
                                 "status" for progress report (in %) of the current scan
2. scanobjects                   (json array, optional) Array of scan objects. Required for "start" action
                                 Every scan object is either a string descriptor or an object:
     [
       "descriptor",             (string) An output descriptor
       {                         (json object) An object with output descriptor and metadata
         "desc": "str",          (string, required) An output descriptor
         "range": n or [n,n],    (numeric or array, optional, default=1000) The range of HD chain indexes to explore (either end or [begin,end])
       },
       ...
     ]
```

</details>

## Return value

when action=='start'; only returns after scan completes

```text
{                                 (json object)
  "success" : true|false,         (boolean) Whether the scan was completed
  "txouts" : n,                   (numeric) The number of unspent transaction outputs scanned
  "height" : n,                   (numeric) The block height at which the scan was done
  "bestblock" : "hex",            (string) The hash of the block at the tip of the chain
  "unspents" : [                  (json array)
    {                             (json object)
      "txid" : "hex",             (string) The transaction id
      "vout" : n,                 (numeric) The vout value
      "scriptPubKey" : "hex",     (string) The output script
      "desc" : "str",             (string) A specialized descriptor for the matched output script
      "amount" : n,               (numeric) The total amount in BTC of the unspent output
      "coinbase" : true|false,    (boolean) Whether this is a coinbase output
      "height" : n,               (numeric) Height of the unspent transaction output
      "blockhash" : "hex",        (string) Blockhash of the unspent transaction output
      "confirmations" : n         (numeric) Number of confirmations of the unspent transaction output when the scan was done
    },
    ...
  ],
  "total_amount" : n              (numeric) The total amount of all found unspent outputs in BTC
}
```

when action=='abort'

`boolean`. True if scan will be aborted (not necessarily before this RPC returns), or false if there is no scan to abort

when action=='status' and a scan is currently in progress

```text
{                    (json object)
  "progress" : n     (numeric) Approximate percent complete
}
```

when action=='status' and no scan is in progress - possibly already completed

`json null`
