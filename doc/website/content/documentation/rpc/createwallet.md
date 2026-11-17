---
title: "createwallet"
description: "Creates and loads a new wallet."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# createwallet

Creates and loads a new wallet.

```sh
bitcoin-cli createwallet "testwallet"
```

## Arguments

**`wallet_name`** (string, required)

The name for the new wallet. If this is a path, the wallet will be created at the path location.

**`disable_private_keys`** (boolean, optional, default false)

Disable the possibility of private keys (only watchonlys are possible in this mode).

**`blank`** (boolean, optional, default false)

Create a blank wallet. A blank wallet has no keys.

**`passphrase`** (string, optional)

Encrypt the wallet with this passphrase.

**`avoid_reuse`** (boolean, optional, default false)

Keep track of coin reuse, and treat dirty and clean coins differently with privacy considerations in mind.

**`descriptors`** (boolean, optional, default true)

If set, must be "true"

**`load_on_startup`** (boolean, optional)

Save wallet name to persistent settings and load on startup. True to add wallet to startup list, false to remove, null to leave unchanged.

**`external_signer`** (boolean, optional, default false)

Use an external signer such as a hardware wallet. Requires -signer to be configured. Wallet creation will fail if keys cannot be fetched. Requires disable\_private\_keys and descriptors set to true.

## Return value

```text
{                    (json object)
  "name" : "str",    (string) The wallet name if created successfully. If the wallet was created using a full path, the wallet_name will be the full path.
  "warnings" : [     (json array, optional) Warning messages, if any, related to creating and loading the wallet.
    "str",           (string)
    ...
  ]
}
```
