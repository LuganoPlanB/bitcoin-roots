---
title: "combinepsbt"
description: "Combine multiple partially signed Bitcoin transactions into one transaction."
rpcCategory: "rawtransactions"
---

[RPC reference](./) · rawtransactions

# combinepsbt

Combine multiple partially signed Bitcoin transactions into one transaction.

```sh
bitcoin-cli combinepsbt '["mybase64_1", "mybase64_2", "mybase64_3"]'
```

## How it works

Implements the Combiner role.

## Arguments

**`txs`** (array, required)

The base64 strings of partially signed transactions

<details>
<summary>Argument formats</summary>

```text
1. txs            (json array, required) The base64 strings of partially signed transactions
     [
       "psbt",    (string) A base64 string of a PSBT
       ...
     ]
```

</details>

## Return value

`string`. The base64-encoded partially signed transaction
