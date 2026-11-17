---
title: "createmultisig"
description: "Creates a multi-signature address with n signatures of m keys required."
rpcCategory: "util"
---

[RPC reference](./) · util

# createmultisig

Creates a multi-signature address with n signatures of m keys required.

```sh
bitcoin-cli createmultisig 2 "[\"03789ed0bb717d88f7d321a368d905e7430207ebbd82bd342cf11ae157a7ace5fd\",\"03dbc6764b8884a92e871274b87583e6d5c2a58819473e17e107ef3f6aa5a61626\"]"
```

## How it works

It returns a json object with the address and redeemScript.

## Arguments

**`nrequired`** (number, required)

The number of required signatures out of the m keys.

**`keys`** (array, required)

The hex-encoded public keys.

**`address_type`** (string, optional, default "legacy")

The address type to use. Options are "legacy", "p2sh-segwit", and "bech32".

<details>
<summary>Argument formats</summary>

```text
1. nrequired       (numeric, required) The number of required signatures out of the m keys.
2. keys            (json array, required) The hex-encoded public keys.
     [
       "key",      (string) The hex-encoded public key
       ...
     ]
3. address_type    (string, optional, default="legacy") The address type to use. Options are "legacy", "p2sh-segwit", and "bech32".
```

</details>

## Return value

```text
{                            (json object)
  "address" : "str",         (string) The value of the new multisig address.
  "redeemScript" : "hex",    (string) The string value of the hex-encoded redemption script.
  "descriptor" : "str",      (string) The descriptor for this multisig
  "warnings" : [             (json array, optional) Any warnings resulting from the creation of this multisig
    "str",                   (string)
    ...
  ]
}
```
