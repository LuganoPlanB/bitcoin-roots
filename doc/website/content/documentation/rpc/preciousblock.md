---
title: "preciousblock"
description: "Treats a block as if it were received before others with the same work."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# preciousblock

Treats a block as if it were received before others with the same work.

```sh
bitcoin-cli preciousblock "blockhash"
```

## How it works

A later preciousblock call can override the effect of an earlier one.

The effects of preciousblock are not retained across restarts.

## Arguments

**`blockhash`** (string, required)

the hash of the block to mark as precious

## Return value

`json null`
