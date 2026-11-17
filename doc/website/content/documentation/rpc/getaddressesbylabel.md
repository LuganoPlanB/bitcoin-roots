---
title: "getaddressesbylabel"
description: "Returns the list of addresses assigned the specified label."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# getaddressesbylabel

Returns the list of addresses assigned the specified label.

```sh
bitcoin-cli getaddressesbylabel "tabby"
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## Arguments

**`label`** (string, required)

The label.

## Return value

```text
{                         (json object) json object with addresses as keys
  "address" : {           (json object) json object with information about address
    "purpose" : "str"     (string) Purpose of address ("send" for sending address, "receive" for receiving address)
  },
  ...
}
```
