---
title: "unloadwallet"
description: "Unloads the wallet referenced by the request endpoint or the wallet_name argument."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# unloadwallet

Unloads the wallet referenced by the request endpoint or the wallet\_name argument.

```sh
bitcoin-cli unloadwallet wallet_name
```

## How it works

If both are specified, they must be identical.

## Arguments

**`wallet_name`** (string, optional, default the wallet name from the RPC endpoint)

The name of the wallet to unload. If provided both here and in the RPC endpoint, the two must be identical.

**`load_on_startup`** (boolean, optional)

Save wallet name to persistent settings and load on startup. True to add wallet to startup list, false to remove, null to leave unchanged.

## Return value

```text
{                   (json object)
  "warnings" : [    (json array, optional) Warning messages, if any, related to unloading the wallet.
    "str",          (string)
    ...
  ]
}
```
