---
title: "gettxout"
description: "Returns details about an unspent transaction output."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# gettxout

Returns details about an unspent transaction output.

```sh
bitcoin-cli gettxout "txid" 1
```

## Arguments

**`txid`** (string, required)

The transaction id

**`n`** (number, required)

vout number

**`include_mempool`** (boolean, optional, default true)

Whether to include the mempool. Note that an unspent output that is spent in the mempool won't appear.

## Return value

If the UTXO was not found

`json null`

Otherwise

```text
{                             (json object)
  "bestblock" : "hex",        (string) The hash of the block at the tip of the chain
  "confirmations" : n,        (numeric) The number of confirmations
  "value" : n,                (numeric) The transaction value in BTC
  "scriptPubKey" : {          (json object)
    "asm" : "str",            (string) Disassembly of the output script
    "desc" : "str",           (string) Inferred descriptor for the output
    "hex" : "hex",            (string) The raw output script bytes, hex-encoded
    "type" : "str",           (string) The type, eg pubkeyhash
    "address" : "str"         (string, optional) The Bitcoin address (only if a well-defined address exists)
  },
  "coinbase" : true|false     (boolean) Coinbase or not
}
```
