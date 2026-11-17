---
title: "getbalances"
description: "Returns an object with all balances in BTC."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# getbalances

Returns an object with all balances in BTC.

```sh
bitcoin-cli getbalances
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## Arguments

No arguments.

## Return value

```text
{                               (json object)
  "mine" : {                    (json object) balances from outputs that the wallet can sign
    "trusted" : n,              (numeric) trusted balance (outputs created by the wallet or confirmed outputs)
    "untrusted_pending" : n,    (numeric) untrusted pending balance (outputs created by others that are in the mempool)
    "immature" : n,             (numeric) balance from immature coinbase outputs
    "nonmempool" : n,           (numeric) sum of coins that are spent by transactions not in the mempool (usually an over-estimate due to not accounting for change or spends that conflict with each other)
    "used" : n                  (numeric, optional) (only present if avoid_reuse is set) balance from coins sent to addresses that were previously spent from (potentially privacy violating)
  },
  "lastprocessedblock" : {      (json object) hash and height of the block this information was generated on
    "hash" : "hex",             (string) hash of the block this information was generated on
    "height" : n                (numeric) height of the block this information was generated on
  }
}
```
