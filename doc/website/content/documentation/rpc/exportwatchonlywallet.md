---
title: "exportwatchonlywallet"
description: "Creates a wallet file at the specified destination containing a watchonly version of the current wallet."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# exportwatchonlywallet

Creates a wallet file at the specified destination containing a watchonly version of the current wallet.

```sh
bitcoin-cli exportwatchonlywallet "/path/to/export.dat"
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## How it works

This watchonly wallet contains the wallet's public descriptors, its transactions, and address book data. Descriptors that use hardened derivation will only have a limited number of derived keys included in the export due to hardened derivation requiring private keys. Descriptors with unhardened derivation do not have this limitation. The watchonly wallet can be imported into another node using 'restorewallet'.

## Arguments

**`destination`** (string, required)

The path to the filename the exported watchonly wallet will be saved to

## Return value

```text
{                             (json object)
  "exported_file" : "str"     (string) The full path that the file has been exported to
}
```
