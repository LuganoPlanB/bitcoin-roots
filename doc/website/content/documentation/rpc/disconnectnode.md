---
title: "disconnectnode"
description: "Immediately disconnects from the specified peer node."
rpcCategory: "network"
rootsChange: "Disconnect peers by subnet as well as by address or node ID."
---

[RPC reference](./) · network

# disconnectnode

Immediately disconnects from the specified peer node.

```sh
bitcoin-cli disconnectnode "192.168.0.6:8333"
```

**New or changed in Roots**

Disconnect peers by subnet as well as by address or node ID.

## How it works

Strictly one out of 'address' and 'nodeid' can be provided to identify the node.

To disconnect by nodeid, either set 'address' to the empty string, or call using the named 'nodeid' argument only.

## Arguments

**`address`** (string, optional, default fallback to nodeid)

The IP address/port or subnet of the node(s)

**`nodeid`** (number, optional, default fallback to address)

The node ID (see getpeerinfo for node IDs)

## Return value

`json null`
