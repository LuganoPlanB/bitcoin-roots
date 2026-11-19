---
title: "waitforblock"
description: "Waits for a specific new block and returns useful info about it."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# waitforblock

Waits for a specific new block and returns useful info about it.

```sh
bitcoin-cli waitforblock "0000000000079f8ef3d2c688c244eb7a4570b24c9ed7b4a8c619eb02596f8862" 1000
```

## How it works

Returns the current block on timeout or exit.

Make sure to use no RPC timeout (bitcoin-cli -rpcclienttimeout=0)

## Arguments

**`blockhash`** (string, required)

Block hash to wait for.

**`timeout`** (number, optional, default 0)

Time in milliseconds to wait for a response. 0 indicates no timeout.

## Return value

```text
{                    (json object)
  "hash" : "hex",    (string) The blockhash
  "height" : n       (numeric) Block height
}
```
