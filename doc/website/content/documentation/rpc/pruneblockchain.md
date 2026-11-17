---
title: "pruneblockchain"
description: "Attempts to delete block and undo data up to a specified height or timestamp, if eligible for pruning."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# pruneblockchain

Attempts to delete block and undo data up to a specified height or timestamp, if eligible for pruning.

```sh
bitcoin-cli pruneblockchain 1000
```

## How it works

Requires \`-prune\` to be enabled at startup. While pruned data may be re-fetched in some cases (e.g., via \`getblockfrompeer\`), local deletion is irreversible.

## Arguments

**`height`** (number, required)

The block height to prune up to. May be set to a discrete height, or to a UNIX epoch time
                  to prune blocks whose block time is at least 2 hours older than the provided timestamp.

## Return value

`numeric`. Height of the last block pruned
