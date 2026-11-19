---
title: "utxoupdatepsbt"
description: "Updates all segwit inputs and outputs in a PSBT with data from output descriptors, the UTXO set, txindex, or the mempool."
rpcCategory: "rawtransactions"
---

[RPC reference](./) · rawtransactions

# utxoupdatepsbt

Updates all segwit inputs and outputs in a PSBT with data from output descriptors, the UTXO set, txindex, or the mempool.

```sh
bitcoin-cli utxoupdatepsbt "psbt"
```

## Arguments

**`psbt`** (string, required)

A base64 string of a PSBT

**`descriptors`** (array, optional)

An array of either strings or objects

<details>
<summary>Argument formats</summary>

```text
1. psbt                          (string, required) A base64 string of a PSBT
2. descriptors                   (json array, optional) An array of either strings or objects
     [
       "",                       (string) An output descriptor
       {                         (json object) An object with an output descriptor and extra information
         "desc": "str",          (string, required) An output descriptor
         "range": n or [n,n],    (numeric or array, optional, default=1000) Up to what index HD chains should be explored (either end or [begin,end])
       },
       ...
     ]
```

</details>

## Return value

`string`. The base64-encoded partially signed transaction with inputs updated
