---
title: "getdescriptorinfo"
description: "Analyses a descriptor."
rpcCategory: "util"
---

[RPC reference](./) · util

# getdescriptorinfo

Analyses a descriptor.

```sh
bitcoin-cli getdescriptorinfo "wpkh([d34db33f/84h/0h/0h]0279be667ef9dcbbac55a06295Ce870b07029Bfcdb2dce28d959f2815b16f81798)"
```

## Arguments

**`descriptor`** (string, required)

The descriptor.

## Return value

```text
{                                   (json object)
  "descriptor" : "str",             (string) The descriptor, without private keys. For a multipath descriptor, only the first will be returned.
  "multipath_expansion" : [         (json array, optional) All descriptors produced by expanding multipath derivation elements. Only if the provided descriptor specifies multipath derivation elements.
    "str",                          (string)
    ...
  ],
  "checksum" : "str",               (string) The checksum for the input descriptor
  "isrange" : true|false,           (boolean) Whether the descriptor is ranged
  "issolvable" : true|false,        (boolean) Whether the descriptor is solvable
  "hasprivatekeys" : true|false     (boolean) Whether the input descriptor contained at least one private key
}
```
