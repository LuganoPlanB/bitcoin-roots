---
title: "getblockfilter"
description: "Retrieve a BIP 157 content filter for a particular block."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# getblockfilter

Retrieve a BIP 157 content filter for a particular block.

```sh
bitcoin-cli getblockfilter "00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09" "basic"
```

## Requirements

- Enable the block filter index with `-blockfilterindex` and let it sync.

## Arguments

**`blockhash`** (string, required)

The hash of the block

**`filtertype`** (string, optional, default "basic")

The type name of the filter

## Return value

```text
{                      (json object)
  "filter" : "hex",    (string) the hex-encoded filter data
  "header" : "hex"     (string) the hex-encoded filter header
}
```
