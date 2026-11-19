---
title: "migratewallet"
description: "Migrate the wallet to a descriptor wallet."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# migratewallet

Migrate the wallet to a descriptor wallet.

```sh
bitcoin-cli migratewallet
```

## How it works

A new wallet backup will need to be made.

The migration process will create a backup of the wallet before migrating. This backup
file will be named &lt;wallet name&gt;-&lt;timestamp&gt;.legacy.bak and can be found in the directory
for this wallet. In the event of an incorrect migration, the backup can be restored using restorewallet.
Encrypted wallets must have the passphrase provided as an argument to this call.

This RPC may take a long time to complete. Increasing the RPC client timeout is recommended.

## Arguments

**`wallet_name`** (string, optional, default the wallet name from the RPC endpoint)

The name of the wallet to migrate. If provided both here and in the RPC endpoint, the two must be identical.

**`passphrase`** (string, optional)

The wallet passphrase

**`load_wallet`** (boolean, optional, default true)

Load the wallet after migration.

## Return value

```text
{                              (json object)
  "wallet_name" : "str",       (string) The name of the primary migrated wallet
  "watchonly_name" : "str",    (string, optional) The name of the migrated wallet containing the watchonly scripts
  "solvables_name" : "str",    (string, optional) The name of the migrated wallet containing solvable but not watched scripts
  "backup_path" : "str"        (string) The location of the backup of the original wallet
}
```
