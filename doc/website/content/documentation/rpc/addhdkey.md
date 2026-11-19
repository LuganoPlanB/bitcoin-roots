---
title: "addhdkey"
description: "Add a BIP 32 HD key to the wallet that can be used with 'createwalletdescriptor'"
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# addhdkey

Add a BIP 32 HD key to the wallet that can be used with 'createwalletdescriptor'

```sh
bitcoin-cli addhdkey xprv
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## Arguments

**`hdkey`** (string, optional, default Automatically generated new key)

The BIP 32 extended private key to add. If none is provided, a randomly generated one will be added.

## Return value

```text
{                    (json object)
  "xpub" : "str"     (string) The xpub of the HD key that was added to the wallet
}
```
