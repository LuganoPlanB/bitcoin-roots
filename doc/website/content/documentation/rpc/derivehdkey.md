---
title: "derivehdkey"
description: "Derive extended public or private key from HD key in the wallet at a given path."
rpcCategory: "wallet"
rootsChange: "All modes require --allow-dangerous-rpc on the command line at startup."
---

[RPC reference](./) · wallet

# derivehdkey

Derive extended public or private key from HD key in the wallet at a given path.

```sh
bitcoin-cli derivehdkey m/87h/0h/0h
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.
- Start the node with `--allow-dangerous-rpc` to enable this command.
- Unlock an encrypted wallet with [walletpassphrase](./walletpassphrase) first.

**New or changed in Roots**

All modes require --allow-dangerous-rpc on the command line at startup.

## How it works

Derivation uses wallet private key material.
Disabled entirely by default, including public-only mode; requires --allow-dangerous-rpc on the command line at startup.

Requires wallet passphrase to be set with walletpassphrase call if wallet is encrypted.

## Arguments

**`path`** (string, required)

BIP 32 derivation path with at least one hardened step.

**`options`** (object, optional)

<details>
<summary>Argument formats</summary>

```text
1. path       (string, required) BIP 32 derivation path with at least one hardened step.
2. options    (json object, optional) Options object that can be used to pass named arguments, listed below.
```

</details>

<details>
<summary>Named arguments</summary>

```text
private    (boolean, optional, default=false) Show private key
hdkey      (string, optional, default=Either the HD key of an unused(KEY) descriptor, or any other active descriptor.) The HD key that the wallet knows the private key of, listed using 'gethdkeys', to use for derivation
```

</details>

## Return value

```text
{                      (json object)
  "origin" : "str",    (string) Fingerprint and path for use in descriptors
  "xpub" : "str",      (string) The extended public key
  "xprv" : "str"       (string, optional) The extended private key if "private" is true
}
```
