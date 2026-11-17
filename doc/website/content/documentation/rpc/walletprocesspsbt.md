---
title: "walletprocesspsbt"
description: "Update a PSBT with input information from our wallet and then sign inputs that we can sign for."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# walletprocesspsbt

Update a PSBT with input information from our wallet and then sign inputs that we can sign for.

```sh
bitcoin-cli walletprocesspsbt "psbt"
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.
- Unlock an encrypted wallet with [walletpassphrase](./walletpassphrase) first.

## How it works

Requires wallet passphrase to be set with walletpassphrase call if wallet is encrypted.

## Arguments

**`psbt`** (string, required)

The transaction base64 string

**`sign`** (boolean, optional, default true)

Also sign the transaction when updating (requires wallet to be unlocked)

**`sighashtype`** (string, optional, default DEFAULT for Taproot, ALL otherwise)

The signature hash type to sign with if not specified by the PSBT. Must be one of
       "DEFAULT"
       "ALL"
       "NONE"
       "SINGLE"
       "ALL\|ANYONECANPAY"
       "NONE\|ANYONECANPAY"
       "SINGLE\|ANYONECANPAY"

**`bip32derivs`** (boolean, optional, default true)

Include BIP 32 derivation paths for public keys if we know them

**`finalize`** (boolean, optional, default true)

Also finalize inputs if possible

## Return value

```text
{                             (json object)
  "psbt" : "str",             (string) The base64-encoded partially signed transaction
  "complete" : true|false,    (boolean) If the transaction has a complete set of signatures
  "hex" : "hex"               (string, optional) The hex-encoded network transaction if complete
}
```
