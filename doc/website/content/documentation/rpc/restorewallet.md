---
title: "restorewallet"
description: "Restores and loads a wallet from backup."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# restorewallet

Restores and loads a wallet from backup.

```sh
bitcoin-cli restorewallet "testwallet" "home\backups\backup-file.bak"
```

## How it works

The rescan is significantly faster if block filters are available
(using startup option "-blockfilterindex=1").

## Arguments

**`wallet_name`** (string, required)

The name that will be applied to the restored wallet

**`backup_file`** (string, required)

The backup file that will be used to restore the wallet.

**`load_on_startup`** (boolean, optional)

Save wallet name to persistent settings and load on startup. True to add wallet to startup list, false to remove, null to leave unchanged.

## Return value

```text
{                    (json object)
  "name" : "str",    (string) The wallet name if restored successfully.
  "warnings" : [     (json array, optional) Warning messages, if any, related to restoring and loading the wallet.
    "str",           (string)
    ...
  ]
}
```
