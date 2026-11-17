---
title: "getnewaddress"
description: "Returns a new Bitcoin address for receiving payments."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# getnewaddress

Returns a new Bitcoin address for receiving payments.

```sh
bitcoin-cli getnewaddress
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## How it works

If 'label' is specified, it is added to the address book
so payments received with the address will be associated with 'label'.

## Arguments

**`label`** (string, optional, default "")

The label name for the address to be linked to. It can also be set to the empty string "" to represent the default label. The label does not need to exist, it will be created if there is no label by the given name.

**`address_type`** (string, optional, default set by -addresstype)

The address type to use. Options are "legacy", "p2sh-segwit", "bech32", "bech32m".

## Return value

`string`. The new bitcoin address
