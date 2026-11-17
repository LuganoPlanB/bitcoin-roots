---
title: "getmininginfo"
description: "Returns a json object containing mining-related information."
rpcCategory: "mining"
---

[RPC reference](./) · mining

# getmininginfo

Returns a json object containing mining-related information.

```sh
bitcoin-cli getmininginfo
```

## Arguments

No arguments.

## Return value

```text
{                                (json object)
  "blocks" : n,                  (numeric) The current block
  "currentblockweight" : n,      (numeric, optional) The block weight (including reserved weight for block header, txs count and coinbase tx) of the last assembled block (only present if a block was ever assembled)
  "currentblocktx" : n,          (numeric, optional) The number of block transactions (excluding coinbase) of the last assembled block (only present if a block was ever assembled)
  "bits" : "hex",                (string) The current nBits, compact representation of the block difficulty target
  "difficulty" : n,              (numeric) The current difficulty
  "target" : "hex",              (string) The current target
  "networkhashps" : n,           (numeric) The network hashes per second
  "pooledtx" : n,                (numeric) The size of the mempool
  "blockmintxfee" : n,           (numeric) Minimum feerate of packages selected for block inclusion in BTC/kvB
  "chain" : "str",               (string) current network name (main, test, testnet4, signet, regtest)
  "signet_challenge" : "hex",    (string, optional) The block challenge (aka. block script), in hexadecimal (only present if the current network is a signet)
  "next" : {                     (json object) The next block
    "height" : n,                (numeric) The next height
    "bits" : "hex",              (string) The next target nBits
    "difficulty" : n,            (numeric) The next difficulty
    "target" : "hex"             (string) The next target
  },
  "warnings" : [                 (json array) any network and blockchain warnings (run with `-deprecatedrpc=warnings` to return the latest warning as a single string)
    "str",                       (string) warning
    ...
  ]
}
```
