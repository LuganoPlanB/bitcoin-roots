---
title: "prioritisetransaction"
description: "Accepts the transaction into mined blocks at a higher (or lower) priority"
rpcCategory: "mining"
rootsChange: "The dust check uses the active dust relay rate. Fee deltas that would overflow are rejected."
---

[RPC reference](./) · mining

# prioritisetransaction

Accepts the transaction into mined blocks at a higher (or lower) priority

```sh
bitcoin-cli prioritisetransaction "txid" 0.0 10000
```

**New or changed in Roots**

The dust check uses the active dust relay rate. Fee deltas that would overflow are rejected.

## Arguments

**`txid`** (string, required)

The transaction id.

**`dummy`** (number, optional)

API-Compatibility for previous API. Must be zero or null.
                  DEPRECATED. For forward compatibility use named arguments and omit this parameter.

**`fee_delta`** (number, required)

The fee value (in satoshis) to add (or subtract, if negative).
                  Note, that this value is not a fee rate. It is a value to modify absolute fee of the TX.
                  The fee is not actually paid, only the algorithm for selecting transactions into a block
                  considers the transaction as it would have paid a higher (or lower) fee.

## Return value

`boolean`. Returns true
