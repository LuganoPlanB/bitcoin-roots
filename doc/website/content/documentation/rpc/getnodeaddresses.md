---
title: "getnodeaddresses"
description: "Return known addresses, after filtering for quality and recency."
rpcCategory: "network"
---

[RPC reference](./) · network

# getnodeaddresses

Return known addresses, after filtering for quality and recency.

```sh
bitcoin-cli getnodeaddresses 8
```

## How it works

These can potentially be used to find new peers in the network.
The total number of addresses known to the node may be higher.

## Arguments

**`count`** (number, optional, default 1)

The maximum number of addresses to return. Specify 0 to return all known addresses.

**`network`** (string, optional, default all networks)

Return only addresses of the specified network. Can be one of: ipv4, ipv6, onion, i2p, cjdns.

## Return value

```text
[                         (json array)
  {                       (json object)
    "time" : xxx,         (numeric) The UNIX epoch time when the node was last seen
    "services" : n,       (numeric) The services offered by the node
    "address" : "str",    (string) The address of the node
    "port" : n,           (numeric) The port number of the node
    "network" : "str"     (string) The network (ipv4, ipv6, onion, i2p, cjdns) the node connected through
  },
  ...
]
```
