---
title: "scanblocks"
description: "Return relevant blockhashes for given descriptors (requires blockfilterindex)."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# scanblocks

Return relevant blockhashes for given descriptors (requires blockfilterindex).

```sh
bitcoin-cli scanblocks start '["addr(bcrt1q4u4nsgk6ug0sqz7r3rj9tykjxrsl0yy4d0wwte)"]' 300000
```

## Requirements

- Enable the block filter index with `-blockfilterindex` and let it sync.

## How it works

This call may take several minutes. Make sure to use no RPC timeout (bitcoin-cli -rpcclienttimeout=0)

## Arguments

**`action`** (string, required)

The action to execute
"start" for starting a scan
"abort" for aborting the current scan (returns true when abort was successful)
"status" for progress report (in %) of the current scan

**`scanobjects`** (array, optional)

Array of scan objects. Required for "start" action
Every scan object is either a string descriptor or an object:

**`start_height`** (number, optional, default 0)

Height to start to scan from

**`stop_height`** (number, optional, default chain tip)

Height to stop to scan

**`filtertype`** (string, optional, default "basic")

The type name of the filter

**`options`** (object, optional)

<details>
<summary>Argument formats</summary>

```text
1. action                        (string, required) The action to execute
                                 "start" for starting a scan
                                 "abort" for aborting the current scan (returns true when abort was successful)
                                 "status" for progress report (in %) of the current scan
2. scanobjects                   (json array, optional) Array of scan objects. Required for "start" action
                                 Every scan object is either a string descriptor or an object:
     [
       "descriptor",             (string) An output descriptor
       {                         (json object) An object with output descriptor and metadata
         "desc": "str",          (string, required) An output descriptor
         "range": n or [n,n],    (numeric or array, optional, default=1000) The range of HD chain indexes to explore (either end or [begin,end])
       },
       ...
     ]
3. start_height                  (numeric, optional, default=0) Height to start to scan from
4. stop_height                   (numeric, optional, default=chain tip) Height to stop to scan
5. filtertype                    (string, optional, default="basic") The type name of the filter
6. options                       (json object, optional) Options object that can be used to pass named arguments, listed below.
```

</details>

<details>
<summary>Named arguments</summary>

```text
filter_false_positives    (boolean, optional, default=false) Filter false positives (slower and may fail on pruned nodes). Otherwise they may occur at a rate of 1/M
```

</details>

## Return value

when action=='status' and no scan is in progress - possibly already completed

`json null`

When action=='start'; only returns after scan completes

```text
{                              (json object)
  "from_height" : n,           (numeric) The height we started the scan from
  "to_height" : n,             (numeric) The height we ended the scan at
  "relevant_blocks" : [        (json array) Blocks that may have matched a scanobject.
    "hex",                     (string) A relevant blockhash
    ...
  ],
  "completed" : true|false     (boolean) true if the scan process was not aborted
}
```

when action=='status' and a scan is currently in progress

```text
{                          (json object)
  "progress" : n,          (numeric) Approximate percent complete
  "current_height" : n     (numeric) Height of the block currently being scanned
}
```

when action=='abort'

`boolean`. True if scan will be aborted (not necessarily before this RPC returns), or false if there is no scan to abort
