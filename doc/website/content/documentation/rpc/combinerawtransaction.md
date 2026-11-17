---
title: "combinerawtransaction"
description: "Combine multiple partially signed transactions into one transaction."
rpcCategory: "rawtransactions"
---

[RPC reference](./) · rawtransactions

# combinerawtransaction

Combine multiple partially signed transactions into one transaction.

```sh
bitcoin-cli combinerawtransaction '["myhex1", "myhex2", "myhex3"]'
```

## How it works

The combined transaction may be another partially signed transaction or a
fully signed transaction.

## Arguments

**`txs`** (array, required)

The hex strings of partially signed transactions

<details>
<summary>Argument formats</summary>

```text
1. txs                 (json array, required) The hex strings of partially signed transactions
     [
       "hexstring",    (string) A hex-encoded raw transaction
       ...
     ]
```

</details>

## Return value

`string`. The hex-encoded raw transaction with signature(s)
