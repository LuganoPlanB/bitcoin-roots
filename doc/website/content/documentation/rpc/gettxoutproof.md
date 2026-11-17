---
title: "gettxoutproof"
description: "Returns a hex-encoded proof that \"txid\" was included in a block."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# gettxoutproof

Returns a hex-encoded proof that "txid" was included in a block.

```sh
bitcoin-cli gettxoutproof '["TXID"]'
```

## How it works

NOTE: By default this function only works sometimes. This is when there is an
unspent output in the utxo for this transaction. To make it always work,
you need to maintain a transaction index, using the -txindex command line option or
specify the block in which the transaction is included manually (by blockhash).

## Arguments

**`txids`** (array, required)

The txids to filter

**`blockhash`** (string, optional)

If specified, looks for txid in the block with this hash

<details>
<summary>Argument formats</summary>

```text
1. txids          (json array, required) The txids to filter
     [
       "txid",    (string) A transaction hash
       ...
     ]
2. blockhash      (string, optional) If specified, looks for txid in the block with this hash
```

</details>

## Return value

`string`. A string that is a serialized, hex-encoded data for the proof.
