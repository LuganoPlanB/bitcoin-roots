---
title: "getblockhash"
description: "Returns hash of block in best-block-chain at height provided."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# getblockhash

Returns hash of block in best-block-chain at height provided.

```sh
bitcoin-cli getblockhash 1000
```

## Arguments

**`height`** (number, required)

The height index

## Return value

`string`. The block hash
