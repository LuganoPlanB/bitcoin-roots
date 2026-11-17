---
title: "getrawchangeaddress"
description: "Returns a new Bitcoin address, for receiving change."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# getrawchangeaddress

Returns a new Bitcoin address, for receiving change.

```sh
bitcoin-cli getrawchangeaddress
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## How it works

This is for use with raw transactions, NOT normal use.

## Arguments

**`address_type`** (string, optional, default set by -changetype)

The address type to use. Options are "legacy", "p2sh-segwit", "bech32", "bech32m".

## Return value

`string`. The address
