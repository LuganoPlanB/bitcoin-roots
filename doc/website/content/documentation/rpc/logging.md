---
title: "logging"
description: "Gets and sets the logging configuration."
rpcCategory: "control"
---

[RPC reference](./) · control

# logging

Gets and sets the logging configuration.

```sh
bitcoin-cli logging "[\"all\"]" "[\"http\"]"
```

## How it works

When called without an argument, returns the list of categories with status that are currently being debug logged or not.
When called with arguments, adds or removes categories from debug logging and return the lists above.
The arguments are evaluated in order "include", "exclude".
If an item is both included and excluded, it will thus end up being excluded.
The valid logging categories are: addrman, bench, blockstorage, cmpctblock, coindb, estimatefee, http, i2p, ipc, kernel, leveldb, mempool, mempoolrej, mining, net, privatebroadcast, proxy, prune, qt, rand, reindex, rpc, scan, selectcoins, tor, txpackages, txreconciliation, validation, walletdb, zmq
In addition, the following are available as category names with special meanings:
  - "all",  "1" : represent all logging categories.

## Arguments

**`include`** (array, optional)

The categories to add to debug logging

**`exclude`** (array, optional)

The categories to remove from debug logging

<details>
<summary>Argument formats</summary>

```text
1. include                    (json array, optional) The categories to add to debug logging
     [
       "include_category",    (string) the valid logging category
       ...
     ]
2. exclude                    (json array, optional) The categories to remove from debug logging
     [
       "exclude_category",    (string) the valid logging category
       ...
     ]
```

</details>

## Return value

```text
{                             (json object) keys are the logging categories, and values indicates its status
  "category" : true|false,    (boolean) if being debug logged or not. false:inactive, true:active
  ...
}
```
