---
title: "enumeratesigners"
description: "Returns a list of external signers from -signer."
rpcCategory: "signer"
---

[RPC reference](./) · signer

# enumeratesigners

Returns a list of external signers from -signer.

```sh
bitcoin-cli enumeratesigners
```

## How it works

Signers with duplicate master key fingerprints are skipped.

## Arguments

No arguments.

## Return value

```text
{                               (json object)
  "signers" : [                 (json array)
    {                           (json object)
      "fingerprint" : "hex",    (string) Master key fingerprint
      "name" : "str"            (string) Device name, the model returned by the signer
    },
    ...
  ]
}
```
