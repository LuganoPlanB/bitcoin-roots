---
title: "getaddrmaninfo"
description: "Provides information about the node's address manager by returning the number of addresses in the `new` and `tried` tables and their sum for all networks."
rpcCategory: "network"
---

[RPC reference](./) · network

# getaddrmaninfo

Provides information about the node's address manager by returning the number of addresses in the \`new\` and \`tried\` tables and their sum for all networks.

```sh
bitcoin-cli getaddrmaninfo
```

## Arguments

No arguments.

## Return value

```text
{                   (json object) json object with network type as keys
  "network" : {     (json object) the network (ipv4, ipv6, onion, i2p, cjdns, all_networks)
    "new" : n,      (numeric) number of addresses in the new table, which represent potential peers the node has discovered but hasn't yet successfully connected to.
    "tried" : n,    (numeric) number of addresses in the tried table, which represent peers the node has successfully connected to in the past.
    "total" : n     (numeric) total number of addresses in both new/tried tables
  },
  ...
}
```
