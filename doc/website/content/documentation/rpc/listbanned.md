---
title: "listbanned"
description: "List all manually banned IPs/Subnets."
rpcCategory: "network"
---

[RPC reference](./) · network

# listbanned

List all manually banned IPs/Subnets.

```sh
bitcoin-cli listbanned
```

## Arguments

No arguments.

## Return value

```text
[                              (json array)
  {                            (json object)
    "address" : "str",         (string) The IP/Subnet of the banned node
    "ban_created" : xxx,       (numeric) The UNIX epoch time the ban was created
    "banned_until" : xxx,      (numeric) The UNIX epoch time the ban expires
    "ban_duration" : xxx,      (numeric) The ban duration, in seconds
    "time_remaining" : xxx     (numeric) The time remaining until the ban expires, in seconds
  },
  ...
]
```
