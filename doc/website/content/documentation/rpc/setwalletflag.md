---
title: "setwalletflag"
description: "Change the state of the given wallet flag for a wallet."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# setwalletflag

Change the state of the given wallet flag for a wallet.

```sh
bitcoin-cli setwalletflag avoid_reuse
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## Arguments

**`flag`** (string, required)

The name of the flag to change. Current available flags: avoid\_reuse

**`value`** (boolean, optional, default true)

The new state.

## Return value

```text
{                               (json object)
  "flag_name" : "str",          (string) The name of the flag that was modified
  "flag_state" : true|false,    (boolean) The new state of the flag
  "warnings" : "str"            (string, optional) Any warnings associated with the change
}
```
