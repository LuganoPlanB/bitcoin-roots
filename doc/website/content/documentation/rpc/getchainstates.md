---
title: "getchainstates"
description: "Return information about chainstates."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# getchainstates

Return information about chainstates.

```sh
bitcoin-cli getchainstates
```

## Arguments

No arguments.

## Return value

```text
{                                      (json object)
  "headers" : n,                       (numeric) the number of headers seen so far
  "chainstates" : [                    (json array) list of the chainstates ordered by work, with the most-work (active) chainstate last
    {                                  (json object)
      "blocks" : n,                    (numeric) number of blocks in this chainstate
      "bestblockhash" : "hex",         (string) blockhash of the tip
      "bits" : "hex",                  (string) nBits: compact representation of the block difficulty target
      "target" : "hex",                (string) The difficulty target
      "difficulty" : n,                (numeric) difficulty of the tip
      "verificationprogress" : n,      (numeric) progress towards the network tip
      "snapshot_blockhash" : "hex",    (string, optional) the base block of the snapshot this chainstate is based on, if any
      "coins_db_cache_bytes" : n,      (numeric) size of the coinsdb cache
      "coins_tip_cache_bytes" : n,     (numeric) size of the coinstip cache
      "validated" : true|false         (boolean) whether the chainstate is fully validated. True if all blocks in the chainstate were validated, false if the chain is based on a snapshot and the snapshot has not yet been validated.
    },
    ...
  ]
}
```
