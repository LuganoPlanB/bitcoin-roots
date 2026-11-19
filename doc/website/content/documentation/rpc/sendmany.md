---
title: "sendmany"
description: "Send multiple times."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# sendmany

Send multiple times.

```sh
bitcoin-cli sendmany "" "{\"bc1q09vm5lfy0j5reeulh4x5752q25uqqvz34hufdl\":0.01,\"bc1q02ad21edsxd23d32dfgqqsz4vv4nmtfzuklhy3\":0.02}"
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.
- Unlock an encrypted wallet with [walletpassphrase](./walletpassphrase) first.

## How it works

Amounts are double-precision floating point numbers.
Requires wallet passphrase to be set with walletpassphrase call if wallet is encrypted.

## Arguments

**`dummy`** (string, optional, default "\\"\\"")

Must be set to "" for backwards compatibility.

**`amounts`** (object, required)

The addresses and amounts

**`minconf`** (number, optional)

Ignored dummy value

**`comment`** (string, optional)

A comment

**`subtractfeefrom`** (array, optional)

The addresses.
The fee will be equally deducted from the amount of each selected address.
Those recipients will receive less bitcoins than you enter in their corresponding amount field.
If no addresses are specified here, the sender pays the fee.

**`replaceable`** (boolean, optional, default wallet default)

Signal that this transaction can be replaced by a transaction (BIP 125)

**`conf_target`** (number, optional, default wallet -txconfirmtarget)

Confirmation target in blocks

**`estimate_mode`** (string, optional, default "unset")

The fee estimate mode, must be one of (case insensitive):
unset, economical, conservative
unset means no mode set (economical mode is used if the transaction is replaceable;
otherwise, conservative mode is used).
economical mode potentially returns a lower fee rate estimate.
conservative potentially returns a higher fee rate estimate.

**`fee_rate`** (number or string, optional, default not set, fall back to wallet fee estimation)

Specify a fee rate in sat/vB.

**`verbose`** (boolean, optional, default false)

If true, return extra information about the transaction.

<details>
<summary>Argument formats</summary>

```text
1. dummy                     (string, optional, default="\"\"") Must be set to "" for backwards compatibility.
2. amounts                   (json object, required) The addresses and amounts
     {
       "address": amount,    (numeric or string, required) The bitcoin address is the key, the numeric amount (can be string) in BTC is the value
       ...
     }
3. minconf                   (numeric, optional) Ignored dummy value
4. comment                   (string, optional) A comment
5. subtractfeefrom           (json array, optional) The addresses.
                             The fee will be equally deducted from the amount of each selected address.
                             Those recipients will receive less bitcoins than you enter in their corresponding amount field.
                             If no addresses are specified here, the sender pays the fee.
     [
       "address",            (string) Subtract fee from this address
       ...
     ]
6. replaceable               (boolean, optional, default=wallet default) Signal that this transaction can be replaced by a transaction (BIP 125)
7. conf_target               (numeric, optional, default=wallet -txconfirmtarget) Confirmation target in blocks
8. estimate_mode             (string, optional, default="unset") The fee estimate mode, must be one of (case insensitive):
                             unset, economical, conservative
                             unset means no mode set (economical mode is used if the transaction is replaceable;
                             otherwise, conservative mode is used).
                             economical mode potentially returns a lower fee rate estimate.
                             conservative potentially returns a higher fee rate estimate.

9. fee_rate                  (numeric or string, optional, default=not set, fall back to wallet fee estimation) Specify a fee rate in sat/vB.
10. verbose                  (boolean, optional, default=false) If true, return extra information about the transaction.
```

</details>

## Return value

if verbose is not set or set to false

```text
"hex"    (string) The transaction id for the send. Only 1 transaction is created regardless of
         the number of addresses.
```

if verbose is set to true

```text
{                          (json object)
  "txid" : "hex",          (string) The transaction id for the send. Only 1 transaction is created regardless of
                           the number of addresses.
  "fee_reason" : "str"     (string) The reason the wallet selected this fee rate (e.g. fee rate estimator, mempool minimum, fallback, or minimum required).
}
```
