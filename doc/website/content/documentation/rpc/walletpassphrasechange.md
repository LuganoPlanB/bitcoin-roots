---
title: "walletpassphrasechange"
description: "Changes the wallet passphrase from 'oldpassphrase' to 'newpassphrase'."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# walletpassphrasechange

Changes the wallet passphrase from 'oldpassphrase' to 'newpassphrase'.

```sh
bitcoin-cli walletpassphrasechange "old one" "new one"
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## Arguments

**`oldpassphrase`** (string, required)

The current passphrase

**`newpassphrase`** (string, required)

The new passphrase

## Return value

`json null`
