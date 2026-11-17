---
title: "getzmqnotifications"
description: "Returns information about the active ZeroMQ notifications."
rpcCategory: "zmq"
---

[RPC reference](./) · zmq

# getzmqnotifications

Returns information about the active ZeroMQ notifications.

```sh
bitcoin-cli getzmqnotifications
```

## Arguments

No arguments.

## Return value

```text
[                         (json array)
  {                       (json object)
    "type" : "str",       (string) Type of notification
    "address" : "str",    (string) Address of the publisher
    "hwm" : n             (numeric) Outbound message high water mark
  },
  ...
]
```
