---
title: "waitforblockheight"
description: "Waits for (at least) block height and returns the height and hash of the current tip."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# waitforblockheight

Waits for (at least) block height and returns the height and hash of the current tip.

```sh
bitcoin-cli waitforblockheight 100 1000
```

## How it works

Returns the current block on timeout or exit.

Make sure to use no RPC timeout (bitcoin-cli -rpcclienttimeout=0)

## Arguments

**`height`** (number, required)

Block height to wait for.

**`timeout`** (number, optional, default 0)

Time in milliseconds to wait for a response. 0 indicates no timeout.

## Return value

```text
{                    (json object)
  "hash" : "hex",    (string) The blockhash
  "height" : n       (numeric) Block height
}
```
