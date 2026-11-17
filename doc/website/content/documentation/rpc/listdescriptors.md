---
title: "listdescriptors"
description: "List all descriptors present in a wallet."
rpcCategory: "wallet"
rootsChange: "All modes require --allow-dangerous-rpc on the command line at startup."
---

[RPC reference](./) · wallet

# listdescriptors

List all descriptors present in a wallet.

```sh
bitcoin-cli listdescriptors
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.
- Start the node with `--allow-dangerous-rpc` to enable this command.

**New or changed in Roots**

All modes require --allow-dangerous-rpc on the command line at startup.

## How it works

Disabled entirely by default, including public-only mode; requires --allow-dangerous-rpc on the command line at startup.

## Arguments

**`private`** (boolean, optional, default false)

Show private descriptors.

## Return value

```text
{                                 (json object)
  "wallet_name" : "str",          (string) Name of wallet this operation was performed on
  "descriptors" : [               (json array) Array of descriptor objects (sorted by descriptor string representation)
    {                             (json object)
      "desc" : "str",             (string) Descriptor string representation
      "timestamp" : n,            (numeric) The creation time of the descriptor
      "active" : true|false,      (boolean) Whether this descriptor is currently used to generate new addresses
      "internal" : true|false,    (boolean, optional) True if this descriptor is used to generate change addresses. False if this descriptor is used to generate receiving addresses; defined only for active descriptors
      "range" : [                 (json array, optional) Defined only for ranged descriptors
        n,                        (numeric) Range start inclusive
        n                         (numeric) Range end inclusive
      ],
      "next" : n,                 (numeric, optional) Same as next_index field. Kept for compatibility reason.
      "next_index" : n            (numeric, optional) The next index to generate addresses from; defined only for ranged descriptors
    },
    ...
  ]
}
```
