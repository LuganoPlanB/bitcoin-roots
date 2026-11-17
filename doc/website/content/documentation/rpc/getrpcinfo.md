---
title: "getrpcinfo"
description: "Returns details of the RPC server."
rpcCategory: "control"
---

[RPC reference](./) · control

# getrpcinfo

Returns details of the RPC server.

```sh
bitcoin-cli getrpcinfo
```

## Arguments

No arguments.

## Return value

```text
{                          (json object)
  "active_commands" : [    (json array) All active commands
    {                      (json object) Information about an active command
      "method" : "str",    (string) The name of the RPC command
      "duration" : n       (numeric) The running time in microseconds
    },
    ...
  ],
  "logpath" : "str"        (string) The complete file path to the debug log
}
```
