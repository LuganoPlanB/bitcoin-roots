---
title: "sweepprivkeys"
description: "Sweep confirmed UTXOs from supplied Base58 WIF private keys to a spendable address in this wallet."
rpcCategory: "wallet"
rootsChange: "Preview a sweep from private keys and authorize the reviewed transaction separately."
---

[RPC reference](./) · wallet

# sweepprivkeys

Sweep confirmed UTXOs from supplied Base58 WIF private keys to a spendable address in this wallet.

```sh
bitcoin-cli sweepprivkeys '["cV..."]' bcrt1... false
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

**New or changed in Roots**

Preview a sweep from private keys and authorize the reviewed transaction separately.

## How it works

All valid WIF keys support P2PKH. Compressed keys also support native P2WPKH and wrapped P2SH-P2WPKH. Taproot and other script forms are not scanned.
The destination must be controlled and spendable by this selected wallet; watch-only and private-key-disabled wallets are rejected. Supplied keys are never imported or saved. Keep an independent backup until the transaction confirms.
With broadcast=false (the default), the result is a signed preview and no transaction is submitted. The fee reflects current wallet fee settings and chain state. Set broadcast=true only after reviewing a fresh preview; broadcast can fail if inputs, fee policy, or chain state changed.

## Arguments

**`privkeys`** (array, required)

Valid Base58 WIF private keys

**`destination`** (string, required)

A spendable address controlled by this selected wallet

**`broadcast`** (boolean, optional, default false)

Submit the signed transaction after a preview has been reviewed

<details>
<summary>Argument formats</summary>

```text
1. privkeys             (json array, required) Valid Base58 WIF private keys
     [
       "privatekey",    (string) private key
       ...
     ]
2. destination          (string, required) A spendable address controlled by this selected wallet
3. broadcast            (boolean, optional, default=false) Submit the signed transaction after a preview has been reviewed
```

</details>

## Return value

```text
{                              (json object)
  "hex" : "hex",               (string) Signed transaction
  "txid" : "hex",              (string) Transaction id
  "amount" : n,                (numeric) Total value of swept inputs
  "fee" : n,                   (numeric) Transaction fee
  "inputs" : n,                (numeric) Number of swept inputs
  "broadcast" : true|false     (boolean) Whether the transaction was submitted for broadcast
}
```
