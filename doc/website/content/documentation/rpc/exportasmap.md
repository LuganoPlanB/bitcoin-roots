---
title: "exportasmap"
description: "Export the embedded ASMap data to a file."
rpcCategory: "network"
---

[RPC reference](./) · network

# exportasmap

Export the embedded ASMap data to a file.

```sh
bitcoin-cli exportasmap "asmap.dat"
```

## How it works

Any existing file at the path will be overwritten.

## Arguments

**`path`** (string, required)

Path to the output file. If relative, will be prefixed by datadir.

## Return value

```text
{                         (json object)
  "path" : "str",         (string) the absolute path that the ASMap data was written to
  "bytes_written" : n,    (numeric) the number of bytes written to the file
  "file_hash" : "hex"     (string) the SHA256 hash of the exported ASMap data
}
```
