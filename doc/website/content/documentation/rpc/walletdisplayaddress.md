---
title: "walletdisplayaddress"
description: "Display address on an external signer for verification."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# walletdisplayaddress

Display address on an external signer for verification.

```sh
bitcoin-cli walletdisplayaddress "YOUR_ADDRESS"
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## Arguments

**`address`** (string, required)

bitcoin address to display

## Return value

```text
{                       (json object)
  "address" : "str"     (string) The address as confirmed by the signer
}
```
