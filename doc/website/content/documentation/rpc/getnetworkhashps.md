---
title: "getnetworkhashps"
description: "Returns the estimated network hashes per second based on the last n blocks."
rpcCategory: "mining"
---

[RPC reference](./) · mining

# getnetworkhashps

Returns the estimated network hashes per second based on the last n blocks.

```sh
bitcoin-cli getnetworkhashps
```

## How it works

Pass in \[blocks\] to override \# of blocks, -1 specifies since last difficulty change.
Pass in \[height\] to estimate the network speed at the time when a certain block was found.

## Arguments

**`nblocks`** (number, optional, default 120)

The number of previous blocks to calculate estimate from, or -1 for blocks since last difficulty change.

**`height`** (number, optional, default -1)

To estimate at the time of the given height.

## Return value

`numeric`. Hashes per second estimated
