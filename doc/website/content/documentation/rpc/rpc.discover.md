---
title: "rpc.discover"
description: "Returns an OpenRPC schema as a description of this service."
rpcCategory: "control"
---

[RPC reference](./) · control

# rpc.discover

Returns an OpenRPC schema as a description of this service.

```sh
bitcoin-cli rpc.discover
```

## Arguments

No arguments.

## Return value

```text
{                                                      (json object)
  "openrpc" : "str",                                   (string) OpenRPC specification version.
  "info" : {                                           (json object) Metadata about this JSON-RPC interface.
    "title" : "str",                                   (string) API title.
    "version" : "str",                                 (string) Bitcoin Core version string.
    "description" : "str"                              (string) API description.
  },
  "methods" : [                                        (json array) Documented RPC methods.
    {                                                  (json object) An RPC method description object.
      "name" : "str",                                  (string) Method name.
      "description" : "str",                           (string) Method description.
      "params" : [                                     (json array) Method parameters.
        {                                              (json object) A parameter.
          "name" : "str",                              (string) Parameter name.
          "required" : true|false,                     (boolean) Whether the parameter is required.
          "schema" : xxx,                              (any) JSON Schema for the parameter.
          "description" : "str",                       (string, optional) Parameter description.
          "x-bitcoin-aliases" : [                      (json array, optional) Alternative parameter names.
            "str",                                     (string) An alias.
            ...
          ],
          "x-bitcoin-placeholder" : true|false,        (boolean, optional) Whether the parameter is retained only for compatibility.
          "x-bitcoin-also-positional" : true|false     (boolean, optional) Whether the parameter can also be passed positionally.
        },
        ...
      ],
      "result" : {                                     (json object) Method result.
        "name" : "str",                                (string) Result name.
        "schema" : xxx                                 (any) JSON Schema for the result. Numeric schemas may include "x-bitcoin-unit" property: "amount" which denotes a Bitcoin amount in BTC.
      },
      "x-bitcoin-category" : "str"                     (string) RPC category.
    },
    ...
  ]
}
```
