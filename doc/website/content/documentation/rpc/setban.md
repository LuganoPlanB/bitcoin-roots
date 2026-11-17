---
title: "setban"
description: "Attempts to add or remove an IP/Subnet from the banned list."
rpcCategory: "network"
---

[RPC reference](./) · network

# setban

Attempts to add or remove an IP/Subnet from the banned list.

```sh
bitcoin-cli setban "192.168.0.6" "add" 86400
```

## Arguments

**`subnet`** (string, required)

The IP/Subnet (see getpeerinfo for nodes IP) with an optional netmask (default is /32 = single IP)

**`command`** (string, required)

'add' to add an IP/Subnet to the list, 'remove' to remove an IP/Subnet from the list

**`bantime`** (number, optional, default 0)

time in seconds how long (or until when if \[absolute\] is set) the IP is banned (0 or empty means using the default time of 24h which can also be overwritten by the -bantime startup argument)

**`absolute`** (boolean, optional, default false)

If set, the bantime must be an absolute timestamp expressed in UNIX epoch time

## Return value

`json null`
