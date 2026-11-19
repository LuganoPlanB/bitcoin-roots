---
title: "signrawtransactionwithkey"
description: "Sign inputs for raw transaction (serialized, hex-encoded)."
rpcCategory: "rawtransactions"
---

[RPC reference](./) · rawtransactions

# signrawtransactionwithkey

Sign inputs for raw transaction (serialized, hex-encoded).

```sh
bitcoin-cli signrawtransactionwithkey "myhex" "[\"key1\",\"key2\"]"
```

## How it works

The second argument is an array of base58-encoded private
keys that will be the only keys used to sign the transaction.
The third optional argument (may be null) is an array of previous transaction outputs that
this transaction depends on but may not yet be in the block chain.

## Arguments

**`hexstring`** (string, required)

The transaction hex string

**`privkeys`** (array, required)

The base58-encoded private keys for signing

**`prevtxs`** (array, optional)

The previous dependent transaction outputs

**`sighashtype`** (string, optional, default DEFAULT for Taproot, ALL otherwise)

The signature hash type. Must be one of:
       "DEFAULT"
       "ALL"
       "NONE"
       "SINGLE"
       "ALL\|ANYONECANPAY"
       "NONE\|ANYONECANPAY"
       "SINGLE\|ANYONECANPAY"

<details>
<summary>Argument formats</summary>

```text
1. hexstring                        (string, required) The transaction hex string
2. privkeys                         (json array, required) The base58-encoded private keys for signing
     [
       "privatekey",                (string) private key in base58-encoding
       ...
     ]
3. prevtxs                          (json array, optional) The previous dependent transaction outputs
     [
       {                            (json object)
         "txid": "hex",             (string, required) The transaction id
         "vout": n,                 (numeric, required) The output number
         "scriptPubKey": "hex",     (string, required) output script
         "redeemScript": "hex",     (string, optional) (required for P2SH) redeem script
         "witnessScript": "hex",    (string, optional) (required for P2WSH or P2SH-P2WSH) witness script
         "amount": amount,          (numeric or string, optional) (required for Segwit inputs) the amount spent
       },
       ...
     ]
4. sighashtype                      (string, optional, default=DEFAULT for Taproot, ALL otherwise) The signature hash type. Must be one of:
                                    "DEFAULT"
                                    "ALL"
                                    "NONE"
                                    "SINGLE"
                                    "ALL|ANYONECANPAY"
                                    "NONE|ANYONECANPAY"
                                    "SINGLE|ANYONECANPAY"
```

</details>

## Return value

```text
{                             (json object)
  "hex" : "hex",              (string) The hex-encoded raw transaction with signature(s)
  "complete" : true|false,    (boolean) If the transaction has a complete set of signatures
  "errors" : [                (json array, optional) Script verification errors (if there are any)
    {                         (json object)
      "txid" : "hex",         (string) The hash of the referenced, previous transaction
      "vout" : n,             (numeric) The index of the output to spent and used as input
      "witness" : [           (json array)
        "hex",                (string)
        ...
      ],
      "scriptSig" : "hex",    (string) The hex-encoded signature script
      "sequence" : n,         (numeric) Script sequence number
      "error" : "str"         (string) Verification or signing error related to the input
    },
    ...
  ]
}
```
