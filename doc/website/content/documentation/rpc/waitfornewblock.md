---
title: "waitfornewblock"
description: "Waits for any new block and returns useful info about it."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# waitfornewblock

Waits for any new block and returns useful info about it.

```sh
bitcoin-cli waitfornewblock 1000
```

## How it works

Returns the current block on timeout or exit.

Make sure to use no RPC timeout (bitcoin-cli -rpcclienttimeout=0)

## Arguments

**`timeout`** (number, optional, default 0)

Time in milliseconds to wait for a response. 0 indicates no timeout.

**`current_tip`** (string, optional)

Method waits for the chain tip to differ from this.

## Return value

```text
{                    (json object)
  "hash" : "hex",    (string) The blockhash
  "height" : n       (numeric) Block height
}
```
