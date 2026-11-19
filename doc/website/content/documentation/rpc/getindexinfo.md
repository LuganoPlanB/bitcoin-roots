---
title: "getindexinfo"
description: "Returns the status of one or all available indices currently running in the node."
rpcCategory: "util"
---

[RPC reference](./) · util

# getindexinfo

Returns the status of one or all available indices currently running in the node.

```sh
bitcoin-cli getindexinfo
```

## Arguments

**`index_name`** (string, optional)

Filter results for an index with a specific name.

## Return value

```text
{                               (json object)
  "name" : {                    (json object) The name of the index
    "synced" : true|false,      (boolean) Whether the index is synced or not
    "best_block_height" : n     (numeric) The block height to which the index is synced
  },
  ...
}
```
