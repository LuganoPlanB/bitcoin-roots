---
title: "backupwallet"
description: "Safely copies the current wallet file to the specified destination, which can either be a directory or a path with a filename."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# backupwallet

Safely copies the current wallet file to the specified destination, which can either be a directory or a path with a filename.

```sh
bitcoin-cli backupwallet "backup.dat"
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## Arguments

**`destination`** (string, required)

The destination directory or file

## Return value

`json null`
