---
title: "encryptwallet"
description: "Encrypts the wallet with 'passphrase'."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# encryptwallet

Encrypts the wallet with 'passphrase'.

```sh
bitcoin-cli encryptwallet "my pass phrase"
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## How it works

This is for first time encryption.
After this, any calls that interact with private keys such as sending or signing
will require the passphrase to be set prior to making these calls.
Use the walletpassphrase call for this, and then walletlock call.
If the wallet is already encrypted, use the walletpassphrasechange call.
\*\* IMPORTANT \*\*
For security reasons, the encryption process will generate a new HD seed, resulting
in the creation of a fresh set of active descriptors. Therefore, it is crucial to
securely back up the newly generated wallet file using the backupwallet RPC.

## Arguments

**`passphrase`** (string, required)

The pass phrase to encrypt the wallet with. It must be at least 1 character, but should be long.

## Return value

`string`. A string with further instructions
