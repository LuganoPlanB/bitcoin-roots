---
title: "addnode"
description: "Attempts to add or remove a node from the addnode list."
rpcCategory: "network"
---

[RPC reference](./) · network

# addnode

Attempts to add or remove a node from the addnode list.

```sh
bitcoin-cli addnode "192.168.0.6:8333" "onetry" true
```

## How it works

Or try a connection to a node once.
Nodes added using addnode (or -connect) are protected from DoS disconnection and are not required to be
full nodes/support SegWit as other outbound peers are (though such peers will not be synced from).
Addnode connections are limited to 8 at a time and are counted separately from the -maxconnections limit.

## Arguments

**`node`** (string, required)

The IP address/hostname optionally followed by :port of the peer to connect to

**`command`** (string, required)

'add' to add a node to the list, 'remove' to remove a node from the list, 'onetry' to try a connection to the node once

**`v2transport`** (boolean, optional, default set by -v2transport)

Attempt to connect using BIP324 v2 transport protocol (ignored for 'remove' command)

## Return value

`json null`
