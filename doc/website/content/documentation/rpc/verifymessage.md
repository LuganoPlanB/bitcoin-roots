---
title: "verifymessage"
description: "Verify a signed message."
rpcCategory: "util"
rootsChange: "Check a BIP322 simple message proof without loading a wallet."
---

[RPC reference](./) · util

# verifymessage

Verify a signed message.

```sh
bitcoin-cli verifymessage "bc1q09vm5lfy0j5reeulh4x5752q25uqqvz34hufdl" "signature" "my message"
```

## Requirements

- You can verify a message without a wallet or a synced blockchain.

**New or changed in Roots**

Check a BIP322 simple message proof without loading a wallet.

## How it works

P2PKH uses legacy compact signatures; native P2WPKH, P2WSH and P2TR use BIP322 simple proofs.
Full signatures, proofs of funds and P2SH (including wrapped SegWit) are unsupported. No wallet or chain data is required.
A valid message proof does not establish a current balance or a past payment.

## Arguments

**`address`** (string, required)

The bitcoin address to use for the signature.

**`signature`** (string, required)

Legacy base64 compact signature, or a BIP322 simple proof: smp followed by a base64-encoded serialized witness stack. Older unprefixed simple proofs are also accepted. Malformed base64 or witness serialization raises an error.

**`message`** (string, required)

The message that was signed.

## Return value

`boolean`. True for a verified signature; false for invalid, unsupported or inconclusive proofs.
