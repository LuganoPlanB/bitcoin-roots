---
title: "savemempool"
description: "Dumps the mempool to disk."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# savemempool

Dumps the mempool to disk.

```sh
bitcoin-cli savemempool
```

## How it works

It will fail until the previous dump is fully loaded.

## Arguments

No arguments.

## Return value

```text
{                        (json object)
  "filename" : "str"     (string) the directory and file where the mempool was saved
}
```
