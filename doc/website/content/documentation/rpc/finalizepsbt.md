---
title: "finalizepsbt"
description: "Finalize the inputs of a PSBT."
rpcCategory: "rawtransactions"
---

[RPC reference](./) · rawtransactions

# finalizepsbt

Finalize the inputs of a PSBT.

```sh
bitcoin-cli finalizepsbt "psbt"
```

## How it works

If the transaction is fully signed, it will produce a
network serialized transaction which can be broadcast with sendrawtransaction. Otherwise a PSBT will be
created which has the final\_scriptSig and final\_scriptwitness fields filled for inputs that are complete.
Implements the Finalizer and Extractor roles.

## Arguments

**`psbt`** (string, required)

A base64 string of a PSBT

**`extract`** (boolean, optional, default true)

If true and the transaction is complete,
                             extract and return the complete transaction in normal network serialization instead of the PSBT.

## Return value

```text
{                             (json object)
  "psbt" : "str",             (string, optional) The base64-encoded partially signed transaction if not extracted
  "hex" : "hex",              (string, optional) The hex-encoded network transaction if extracted
  "complete" : true|false     (boolean) If the transaction has a complete set of signatures
}
```
