---
title: "ping"
description: "Requests that a ping be sent to all other nodes, to measure ping time."
rpcCategory: "network"
---

[RPC reference](./) · network

# ping

Requests that a ping be sent to all other nodes, to measure ping time.

```sh
bitcoin-cli ping
```

## How it works

Results are provided in getpeerinfo.
Ping command is handled in queue with all other commands, so it measures processing backlog, not just network ping.

## Arguments

No arguments.

## Return value

`json null`
