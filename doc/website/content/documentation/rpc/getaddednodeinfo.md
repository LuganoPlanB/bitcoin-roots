---
title: "getaddednodeinfo"
description: "Returns information about the given added node, or all added nodes (note that onetry addnodes are not listed here)"
rpcCategory: "network"
---

[RPC reference](./) · network

# getaddednodeinfo

Returns information about the given added node, or all added nodes (note that onetry addnodes are not listed here)

```sh
bitcoin-cli getaddednodeinfo "192.168.0.201"
```

## Arguments

**`node`** (string, optional, default all nodes)

If provided, return information about this specific node, otherwise all nodes are returned.

## Return value

```text
[                                (json array)
  {                              (json object)
    "addednode" : "str",         (string) The node IP address or name (as provided to addnode)
    "connected" : true|false,    (boolean) If connected
    "addresses" : [              (json array) Only when connected = true
      {                          (json object)
        "address" : "str",       (string) The bitcoin server IP and port we're connected to
        "connected" : "str"      (string) connection, inbound or outbound
      },
      ...
    ]
  },
  ...
]
```
