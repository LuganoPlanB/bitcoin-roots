---
title: "listlabels"
description: "Returns the list of all labels, or labels that are assigned to addresses with a specific purpose."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# listlabels

Returns the list of all labels, or labels that are assigned to addresses with a specific purpose.

```sh
bitcoin-cli listlabels
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## Arguments

**`purpose`** (string, optional)

Address purpose to list labels for ('send','receive'). An empty string is the same as not providing this argument.

## Return value

```text
[           (json array)
  "str",    (string) Label name
  ...
]
```
