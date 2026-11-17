---
title: "sendtoaddress"
description: "Send an amount to a given address."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# sendtoaddress

Send an amount to a given address.

```sh
bitcoin-cli sendtoaddress "bc1q09vm5lfy0j5reeulh4x5752q25uqqvz34hufdl" 0.1
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.
- Unlock an encrypted wallet with [walletpassphrase](./walletpassphrase) first.

## How it works

Requires wallet passphrase to be set with walletpassphrase call if wallet is encrypted.

## Arguments

**`address`** (string, required)

The bitcoin address to send to.

**`amount`** (number or string, required)

The amount in BTC to send. eg 0.1

**`comment`** (string, optional)

A comment used to store what the transaction is for.
This is not part of the transaction, just kept in your wallet.

**`comment_to`** (string, optional)

A comment to store the name of the person or organization
to which you're sending the transaction. This is not part of the
transaction, just kept in your wallet.

**`subtractfeefromamount`** (boolean, optional, default false)

The fee will be deducted from the amount being sent.
The recipient will receive less bitcoins than you enter in the amount field.

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

**`avoid_reuse`** (boolean, optional, default true)

(only available if avoid\_reuse wallet flag is set) Avoid spending from dirty addresses; addresses are considered
dirty if they have previously been used in a transaction. If true, this also activates avoidpartialspends, grouping outputs by their addresses.

**`fee_rate`** (number or string, optional, default not set, fall back to wallet fee estimation)

Specify a fee rate in sat/vB.

**`verbose`** (boolean, optional, default false)

If true, return extra information about the transaction.

<details>
<summary>Argument formats</summary>

```text
1. address                  (string, required) The bitcoin address to send to.
2. amount                   (numeric or string, required) The amount in BTC to send. eg 0.1
3. comment                  (string, optional) A comment used to store what the transaction is for.
                            This is not part of the transaction, just kept in your wallet.
4. comment_to               (string, optional) A comment to store the name of the person or organization
                            to which you're sending the transaction. This is not part of the
                            transaction, just kept in your wallet.
5. subtractfeefromamount    (boolean, optional, default=false) The fee will be deducted from the amount being sent.
                            The recipient will receive less bitcoins than you enter in the amount field.
6. replaceable              (boolean, optional, default=wallet default) Signal that this transaction can be replaced by a transaction (BIP 125)
7. conf_target              (numeric, optional, default=wallet -txconfirmtarget) Confirmation target in blocks
8. estimate_mode            (string, optional, default="unset") The fee estimate mode, must be one of (case insensitive):
                            unset, economical, conservative
                            unset means no mode set (economical mode is used if the transaction is replaceable;
                            otherwise, conservative mode is used).
                            economical mode potentially returns a lower fee rate estimate.
                            conservative potentially returns a higher fee rate estimate.

9. avoid_reuse              (boolean, optional, default=true) (only available if avoid_reuse wallet flag is set) Avoid spending from dirty addresses; addresses are considered
                            dirty if they have previously been used in a transaction. If true, this also activates avoidpartialspends, grouping outputs by their addresses.
10. fee_rate                (numeric or string, optional, default=not set, fall back to wallet fee estimation) Specify a fee rate in sat/vB.
11. verbose                 (boolean, optional, default=false) If true, return extra information about the transaction.
```

</details>

## Return value

if verbose is not set or set to false

`string`. The transaction id.

if verbose is set to true

```text
{                          (json object)
  "txid" : "hex",          (string) The transaction id.
  "fee_reason" : "str"     (string) The reason the wallet selected this fee rate (e.g. fee rate estimator, mempool minimum, fallback, or minimum required).
}
```
