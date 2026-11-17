---
title: "submitheader"
description: "Decode the given hexdata as a header and submit it as a candidate chain tip if valid."
rpcCategory: "mining"
---

[RPC reference](./) · mining

# submitheader

Decode the given hexdata as a header and submit it as a candidate chain tip if valid.

```sh
bitcoin-cli submitheader "aabbcc"
```

## How it works

Throws when the header is invalid.

## Arguments

**`hexdata`** (string, required)

the hex-encoded block header data

## Return value

`json null`. None
