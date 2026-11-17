---
title: "createrawtransaction"
description: "Create a transaction spending the given inputs and creating new outputs."
rpcCategory: "rawtransactions"
---

[RPC reference](./) · rawtransactions

# createrawtransaction

Create a transaction spending the given inputs and creating new outputs.

```sh
bitcoin-cli createrawtransaction "[{\"txid\":\"myid\",\"vout\":0}]" "[{\"address\":0.01}]"
```

## How it works

Outputs can be addresses or data.
Returns hex-encoded raw transaction.
Note that the transaction's inputs are not signed, and
it is not stored in the wallet or transmitted to the network.

## Arguments

**`inputs`** (array, required)

The inputs

**`outputs`** (array or object, required)

The outputs specified as key-value pairs.
Each key may only appear once, i.e. there can only be one 'data' output, and no address may be duplicated.
At least one output of either type must be specified.
For compatibility reasons, a dictionary, which holds the key-value pairs directly, is also
                             accepted as second parameter.

**`locktime`** (number, optional, default 0)

Raw locktime. Non-0 value also locktime-activates inputs

**`replaceable`** (boolean, optional, default true)

Marks this transaction as BIP125-replaceable.
Allows this transaction to be replaced by a transaction with higher fees. If provided, it is an error if explicit sequence numbers are incompatible.

**`version`** (number, optional, default 2)

Transaction version

<details>
<summary>Argument formats</summary>

```text
1. inputs                      (json array, required) The inputs
     [
       {                       (json object)
         "txid": "hex",        (string, required) The transaction id
         "vout": n,            (numeric, required) The output number
         "sequence": n,        (numeric, optional, default=depends on the value of the 'replaceable' and 'locktime' arguments) The sequence number
       },
       ...
     ]
2. outputs                     (json array, required) The outputs specified as key-value pairs.
                               Each key may only appear once, i.e. there can only be one 'data' output, and no address may be duplicated.
                               At least one output of either type must be specified.
                               For compatibility reasons, a dictionary, which holds the key-value pairs directly, is also
                               accepted as second parameter.
     [
       {                       (json object)
         "address": amount,    (numeric or string, required) A key-value pair. The key (string) is the bitcoin address, the value (float or string) is the amount in BTC
         ...
       },
       {                       (json object)
         "data": "hex",        (string, required) A key-value pair. The key must be "data", the value is hex-encoded data that becomes a part of an OP_RETURN output
       },
       ...
     ]
3. locktime                    (numeric, optional, default=0) Raw locktime. Non-0 value also locktime-activates inputs
4. replaceable                 (boolean, optional, default=true) Marks this transaction as BIP125-replaceable.
                               Allows this transaction to be replaced by a transaction with higher fees. If provided, it is an error if explicit sequence numbers are incompatible.
5. version                     (numeric, optional, default=2) Transaction version
```

</details>

## Return value

`string`. hex string of the transaction
