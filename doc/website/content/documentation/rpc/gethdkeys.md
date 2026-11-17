---
title: "gethdkeys"
description: "List all BIP 32 HD keys in the wallet and which descriptors use them."
rpcCategory: "wallet"
rootsChange: "All modes require --allow-dangerous-rpc on the command line at startup."
---

[RPC reference](./) · wallet

# gethdkeys

List all BIP 32 HD keys in the wallet and which descriptors use them.

```sh
bitcoin-cli gethdkeys
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.
- Start the node with `--allow-dangerous-rpc` to enable this command.

**New or changed in Roots**

All modes require --allow-dangerous-rpc on the command line at startup.

## How it works

Disabled entirely by default, including public-only mode; requires --allow-dangerous-rpc on the command line at startup.

## Arguments

**`options`** (object, optional)

<details>
<summary>Argument formats</summary>

```text
1. options    (json object, optional) Options object that can be used to pass named arguments, listed below.
```

</details>

<details>
<summary>Named arguments</summary>

```text
active_only    (boolean, optional, default=false) Show the keys for only active descriptors
private        (boolean, optional, default=false) Show private keys
```

</details>

## Return value

```text
[                                  (json array)
  {                                (json object)
    "xpub" : "str",                (string) The extended public key
    "has_private" : true|false,    (boolean) Whether the wallet has the private key for this xpub
    "xprv" : "str",                (string, optional) The extended private key if "private" is true
    "descriptors" : [              (json array) Array of descriptor objects that use this HD key
      {                            (json object)
        "desc" : "str",            (string) Descriptor string public representation
        "active" : true|false      (boolean) Whether this descriptor is currently used to generate new addresses
      },
      ...
    ]
  },
  ...
]
```
