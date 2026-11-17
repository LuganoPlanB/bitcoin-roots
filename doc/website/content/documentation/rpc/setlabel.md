---
title: "setlabel"
description: "Sets the label associated with the given address."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# setlabel

Sets the label associated with the given address.

```sh
bitcoin-cli setlabel "bc1q09vm5lfy0j5reeulh4x5752q25uqqvz34hufdl" "tabby"
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## Arguments

**`address`** (string, required)

The bitcoin address to be associated with a label.

**`label`** (string, required)

The label to assign to the address.

## Return value

`json null`
