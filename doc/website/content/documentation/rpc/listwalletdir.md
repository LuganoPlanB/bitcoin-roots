---
title: "listwalletdir"
description: "Returns a list of wallets in the wallet directory."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# listwalletdir

Returns a list of wallets in the wallet directory.

```sh
bitcoin-cli listwalletdir
```

## Arguments

No arguments.

## Return value

```text
{                        (json object)
  "wallets" : [          (json array)
    {                    (json object)
      "name" : "str",    (string) The wallet name
      "warnings" : [     (json array) Warning messages related to loading the wallet (may be empty).
        "str",           (string)
        ...
      ]
    },
    ...
  ]
}
```
