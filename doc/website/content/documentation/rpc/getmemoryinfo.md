---
title: "getmemoryinfo"
description: "Returns an object containing information about memory usage."
rpcCategory: "control"
---

[RPC reference](./) · control

# getmemoryinfo

Returns an object containing information about memory usage.

```sh
bitcoin-cli getmemoryinfo
```

## Arguments

**`mode`** (string, optional, default "stats")

determines what kind of information is returned.
  - "stats" returns general statistics about memory usage in the daemon.
  - "mallocinfo" returns an XML string describing low-level heap state (only available if compiled with glibc).

## Return value

mode "stats"

```text
{                         (json object)
  "locked" : {            (json object) Information about locked memory manager
    "used" : n,           (numeric) Number of bytes used
    "free" : n,           (numeric) Number of bytes available in current arenas
    "total" : n,          (numeric) Total number of bytes managed
    "locked" : n,         (numeric) Amount of bytes that succeeded locking. If this number is smaller than total, locking pages failed at some point and key data could be swapped to disk.
    "chunks_used" : n,    (numeric) Number allocated chunks
    "chunks_free" : n     (numeric) Number unused chunks
  }
}
```

mode "mallocinfo"

`string`. "&lt;malloc version="1"&gt;..."
