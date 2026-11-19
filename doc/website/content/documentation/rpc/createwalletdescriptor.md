---
title: "createwalletdescriptor"
description: "Creates the wallet's descriptor for the given address type."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# createwalletdescriptor

Creates the wallet's descriptor for the given address type.

```sh
bitcoin-cli createwalletdescriptor bech32m
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.
- Unlock an encrypted wallet with [walletpassphrase](./walletpassphrase) first.

## How it works

The address type must be one that the wallet does not already have a descriptor for.
Requires wallet passphrase to be set with walletpassphrase call if wallet is encrypted.

## Arguments

**`type`** (string, required)

The address type the descriptor will produce. Options are "legacy", "p2sh-segwit", "bech32", "bech32m".

**`options`** (object, optional)

<details>
<summary>Argument formats</summary>

```text
1. type       (string, required) The address type the descriptor will produce. Options are "legacy", "p2sh-segwit", "bech32", "bech32m".
2. options    (json object, optional) Options object that can be used to pass named arguments, listed below.
```

</details>

<details>
<summary>Named arguments</summary>

```text
internal    (boolean, optional, default=Both external and internal will be generated unless this parameter is specified) Whether to only make one descriptor that is internal (if parameter is true) or external (if parameter is false)
hdkey       (string, optional, default=The HD key used by all other active descriptors) The HD key that the wallet knows the private key of, listed using 'gethdkeys', to use for this descriptor's key
```

</details>

## Return value

```text
{                (json object)
  "descs" : [    (json array) The public descriptors that were added to the wallet
    "str",       (string)
    ...
  ]
}
```
