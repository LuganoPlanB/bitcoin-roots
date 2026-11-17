---
title: "listaddressgroupings"
description: "Lists groups of addresses which have had their common ownership made public by common use as inputs or as the resulting change in past transactions"
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# listaddressgroupings

Lists groups of addresses which have had their common ownership made public by common use as inputs or as the resulting change in past transactions

```sh
bitcoin-cli listaddressgroupings
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## Arguments

No arguments.

## Return value

```text
[               (json array)
  [             (json array)
    [           (json array)
      "str",    (string) The bitcoin address
      n,        (numeric) The amount in BTC
      "str"     (string, optional) The label
    ],
    ...
  ],
  ...
]
```
