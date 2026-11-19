---
title: "signmessage"
description: "Sign a message for a wallet address."
rpcCategory: "wallet"
rootsChange: "Sign a BIP322 simple message proof with a supported native SegWit or Taproot wallet address."
---

[RPC reference](./) · wallet

# signmessage

Sign a message for a wallet address.

```sh
bitcoin-cli signmessage "bc1q09vm5lfy0j5reeulh4x5752q25uqqvz34hufdl" "my message"
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.
- Unlock an encrypted wallet with [walletpassphrase](./walletpassphrase) first.

**New or changed in Roots**

Sign a BIP322 simple message proof with a supported native SegWit or Taproot wallet address.

## How it works

P2PKH uses the legacy compact format; native P2WPKH, P2WSH and P2TR use BIP322 simple proofs.
BIP322 signing requires a complete satisfaction using the wallet's available descriptor keys, scripts and other data. Partial proofs are not returned.
BIP322 simple proofs fix transaction version, locktime and input sequence to zero.
Timelock requirements incompatible with these fields cannot be satisfied by waiting or importing more keys.
Signing may still succeed through another satisfiable script branch or a Taproot key path.
Full signatures, proofs of funds and P2SH (including wrapped SegWit) are unsupported. signmessagewithprivkey remains legacy-only.
A message proof does not establish a current balance or a past payment.
Requires wallet passphrase to be set with walletpassphrase call if wallet is encrypted.

## Arguments

**`address`** (string, required)

The wallet's P2PKH, native P2WPKH, P2WSH or P2TR address.

**`message`** (string, required)

The message to create a signature of.

## Return value

`string`. Legacy base64 compact signature, or smp followed by a base64-encoded serialized witness stack.
