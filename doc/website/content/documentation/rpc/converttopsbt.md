---
title: "converttopsbt"
description: "Converts a network serialized transaction to a PSBT."
rpcCategory: "rawtransactions"
---

[RPC reference](./) · rawtransactions

# converttopsbt

Converts a network serialized transaction to a PSBT.

```sh
bitcoin-cli converttopsbt "rawtransaction"
```

## How it works

This should be used only with createrawtransaction and fundrawtransaction
createpsbt and walletcreatefundedpsbt should be used for new applications.

## Arguments

**`hexstring`** (string, required)

The hex string of a raw transaction

**`permitsigdata`** (boolean, optional, default false)

If true, any signatures in the input will be discarded and conversion
                              will continue. If false, RPC will fail if any signatures are present.

**`iswitness`** (boolean, optional, default depends on heuristic tests)

Whether the transaction hex is a serialized witness transaction.
If iswitness is not present, heuristic tests will be used in decoding.
If true, only witness deserialization will be tried.
If false, only non-witness deserialization will be tried.
This boolean should reflect whether the transaction has inputs
(e.g. fully valid, or on-chain transactions), if known by the caller.

**`psbt_version`** (number, optional, default 2)

The PSBT version number to use.

## Return value

`string`. The resulting raw transaction (base64-encoded string)
