---
title: "deriveaddresses"
description: "Derives one or more addresses corresponding to an output descriptor."
rpcCategory: "util"
---

[RPC reference](./) · util

# deriveaddresses

Derives one or more addresses corresponding to an output descriptor.

```sh
bitcoin-cli deriveaddresses "wpkh([d34db33f/84h/0h/0h]xpub6DJ2dNUysrn5Vt36jH2KLBT2i1auw1tTSSomg8PhqNiUtx8QX2SvC9nrHu81fT41fvDUnhMjEzQgXnQjKEu3oaqMSzhSrHMxyyoEAmUHQbY/0/*)#cjjspncu" "[0,2]"
```

## How it works

Examples of output descriptors are:
    pkh(&lt;pubkey&gt;)                                     P2PKH outputs for the given pubkey
    wpkh(&lt;pubkey&gt;)                                    Native segwit P2PKH outputs for the given pubkey
    sh(multi(&lt;n&gt;,&lt;pubkey&gt;,&lt;pubkey&gt;,...))              P2SH-multisig outputs for the given threshold and pubkeys
    raw(&lt;hex script&gt;)                                 Outputs whose output script equals the specified hex-encoded bytes
    tr(&lt;pubkey&gt;,multi\_a(&lt;n&gt;,&lt;pubkey&gt;,&lt;pubkey&gt;,...))   P2TR-multisig outputs for the given threshold and pubkeys

In the above, &lt;pubkey&gt; either refers to a fixed public key in hexadecimal notation, or to an xpub/xprv optionally followed by one
or more path elements separated by "/", where "h" represents a hardened child key.
For more information on output descriptors, see the documentation in the doc/descriptors.md file.

## Arguments

**`descriptor`** (string, required)

The descriptor.

**`range`** (number or array, optional)

If a ranged descriptor is used, this specifies the end or the range (in \[begin,end\] notation) to derive.

<details>
<summary>Argument formats</summary>

```text
1. descriptor    (string, required) The descriptor.
2. range         (numeric or array, optional) If a ranged descriptor is used, this specifies the end or the range (in [begin,end] notation) to derive.
```

</details>

## Return value

for single derivation descriptors

```text
[           (json array)
  "str",    (string) the derived addresses
  ...
]
```

for multipath descriptors

```text
[             (json array) The derived addresses for each of the multipath expansions of the descriptor, in multipath specifier order
  [           (json array) The derived addresses for a multipath descriptor expansion
    "str",    (string) the derived address
    ...
  ],
  ...
]
```
