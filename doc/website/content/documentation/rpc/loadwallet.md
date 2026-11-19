---
title: "loadwallet"
description: "Loads a wallet from a wallet file or directory."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# loadwallet

Loads a wallet from a wallet file or directory.

```sh
bitcoin-cli loadwallet "walletname"
```

## How it works

Note that all wallet command-line options used when starting bitcoind will be
applied to the new wallet.

## Arguments

**`filename`** (string, required)

The path to the directory of the wallet to be loaded, either absolute or relative to the "wallets" directory. The "wallets" directory is set by the -walletdir option and defaults to the "wallets" folder within the data directory.

**`load_on_startup`** (boolean, optional)

Save wallet name to persistent settings and load on startup. True to add wallet to startup list, false to remove, null to leave unchanged.

## Return value

```text
{                    (json object)
  "name" : "str",    (string) The wallet name if loaded successfully.
  "warnings" : [     (json array, optional) Warning messages, if any, related to loading the wallet.
    "str",           (string)
    ...
  ]
}
```
