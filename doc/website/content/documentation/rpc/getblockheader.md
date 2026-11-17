---
title: "getblockheader"
description: "If verbose is false, returns a string that is serialized, hex-encoded data for blockheader 'hash'."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# getblockheader

If verbose is false, returns a string that is serialized, hex-encoded data for blockheader 'hash'.

```sh
bitcoin-cli getblockheader "00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09"
```

## How it works

If verbose is true, returns an Object with information about blockheader &lt;hash&gt;.

## Arguments

**`blockhash`** (string, required)

The block hash

**`verbose`** (boolean, optional, default true)

true for a json object, false for the hex-encoded data

## Return value

for verbose = true

```text
{                                 (json object)
  "hash" : "hex",                 (string) the block hash (same as provided)
  "confirmations" : n,            (numeric) The number of confirmations, or -1 if the block is not on the main chain
  "height" : n,                   (numeric) The block height or index
  "version" : n,                  (numeric) The block version
  "versionHex" : "hex",           (string) The block version formatted in hexadecimal
  "merkleroot" : "hex",           (string) The merkle root
  "time" : xxx,                   (numeric) The block time expressed in UNIX epoch time
  "mediantime" : xxx,             (numeric) The median block time expressed in UNIX epoch time
  "nonce" : n,                    (numeric) The nonce
  "bits" : "hex",                 (string) nBits: compact representation of the block difficulty target
  "target" : "hex",               (string) The difficulty target
  "difficulty" : n,               (numeric) The difficulty
  "chainwork" : "hex",            (string) Expected number of hashes required to produce the current chain
  "nTx" : n,                      (numeric) The number of transactions in the block
  "previousblockhash" : "hex",    (string, optional) The hash of the previous block (if available)
  "nextblockhash" : "hex"         (string, optional) The hash of the next block (if available)
}
```

for verbose=false

`string`. A string that is serialized, hex-encoded data for block 'hash'
