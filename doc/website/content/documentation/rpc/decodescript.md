---
title: "decodescript"
description: "Decode a hex-encoded script."
rpcCategory: "rawtransactions"
---

[RPC reference](./) · rawtransactions

# decodescript

Decode a hex-encoded script.

```sh
bitcoin-cli decodescript "hexstring"
```

## Arguments

**`hexstring`** (string, required)

the hex-encoded script

## Return value

```text
{                             (json object)
  "asm" : "str",              (string) Disassembly of the script
  "desc" : "str",             (string) Inferred descriptor for the script
  "type" : "str",             (string) The output type (e.g. nonstandard, anchor, pubkey, pubkeyhash, scripthash, multisig, nulldata, witness_v0_scripthash, witness_v0_keyhash, witness_v1_taproot, witness_unknown)
  "address" : "str",          (string, optional) The Bitcoin address (only if a well-defined address exists)
  "p2sh" : "str",             (string, optional) address of P2SH script wrapping this redeem script (not returned for types that should not be wrapped)
  "segwit" : {                (json object, optional) Result of a witness output script wrapping this redeem script (not returned for types that should not be wrapped)
    "asm" : "str",            (string) Disassembly of the output script
    "hex" : "hex",            (string) The raw output script bytes, hex-encoded
    "type" : "str",           (string) The type of the output script (e.g. witness_v0_keyhash or witness_v0_scripthash)
    "address" : "str",        (string, optional) The Bitcoin address (only if a well-defined address exists)
    "desc" : "str",           (string) Inferred descriptor for the script
    "p2sh-segwit" : "str"     (string) address of the P2SH script wrapping this witness redeem script
  }
}
```
