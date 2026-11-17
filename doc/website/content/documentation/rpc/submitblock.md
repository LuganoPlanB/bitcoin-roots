---
title: "submitblock"
description: "Attempts to submit new block to network."
rpcCategory: "mining"
---

[RPC reference](./) · mining

# submitblock

Attempts to submit new block to network.

```sh
bitcoin-cli submitblock "mydata"
```

## How it works

See https://en.bitcoin.it/wiki/BIP\_0022 for full specification.

## Arguments

**`hexdata`** (string, required)

the hex-encoded block data to submit

**`dummy`** (string, optional, default ignored)

dummy value, for compatibility with BIP22. This value is ignored.

## Return value

If the block was accepted

`json null`

Otherwise

`string`. According to BIP22
