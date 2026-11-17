---
title: "joinpsbts"
description: "Joins multiple distinct version 0 PSBTs with different inputs and outputs into one version 0 PSBT with inputs and outputs from all of the PSBTs No input in any of the PSBTs can be in more than one of the PSBTs."
rpcCategory: "rawtransactions"
---

[RPC reference](./) · rawtransactions

# joinpsbts

Joins multiple distinct version 0 PSBTs with different inputs and outputs into one version 0 PSBT with inputs and outputs from all of the PSBTs No input in any of the PSBTs can be in more than one of the PSBTs.

```sh
bitcoin-cli joinpsbts "psbt"
```

## Arguments

**`txs`** (array, required)

The base64 strings of partially signed transactions

<details>
<summary>Argument formats</summary>

```text
1. txs            (json array, required) The base64 strings of partially signed transactions
     [
       "psbt",    (string, required) A base64 string of a PSBT
       ...
     ]
```

</details>

## Return value

`string`. The base64-encoded partially signed transaction
