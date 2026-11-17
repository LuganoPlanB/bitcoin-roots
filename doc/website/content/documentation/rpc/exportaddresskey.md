---
title: "exportaddresskey"
description: "Returns local private keys for a recognized address known to the selected descriptor wallet."
rpcCategory: "wallet"
rootsChange: "Export the private keys your wallet holds for an address."
---

[RPC reference](./) · wallet

# exportaddresskey

Returns local private keys for a recognized address known to the selected descriptor wallet.

```sh
bitcoin-cli exportaddresskey "bc1q09vm5lfy0j5reeulh4x5752q25uqqvz34hufdl"
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.
- Start the node with `--allow-dangerous-rpc` to enable this command.

**New or changed in Roots**

Export the private keys your wallet holds for an address.

## How it works

Disabled by default; requires --allow-dangerous-rpc on the command line at startup.
Supports P2PKH, P2WPKH, P2SH, P2WSH and Taproot tr()/rawtr(), including nested wrappers, multisig and supported Miniscript.
Returns every available key even when other keys, preimages or timelocks are needed to spend.
Uses existing wallet metadata and known indices; missing redeem/witness scripts or Taproot commitments produce a missing spending/script information error.
It does not recover unknown script hashes, search unknown derivation paths or infer keys from arbitrary script data.
The canonical keys array is sorted by full serialized public key, with sorted unique roles.
The top-level private\_key\_hex and compressed fields are compatibility aliases for original P2PKH/native P2WPKH exports.
Wallet mode returns original stored/derived scalars and full public-key serializations, including Taproot internal, direct output, leaf and MuSig2 participant keys.
The taproot\_output mode returns effective key-path keys, applying the exact known TapTweak (including tree-less tr()) to internal keys. Direct output/rawtr() keys are not tweaked again.
Script-only or MuSig2 participant-only ownership can succeed in wallet mode while taproot\_output fails. Participant secrets are not aggregate or output secrets, even when all participants are local.
A MuSig2 participant used before post-aggregation derivation, or any explicitly reused key, may control many outputs. Export does not promise an address-exclusive or leaf-only secret.
A direct address for a stored descriptor root can export that root's private key, including an HD master private key.
Exported keys are not a wallet backup; scripts, control blocks, cosigners, preimages or timelocks may still be required.
Requires wallet unlock. The result contains a spending secret, including when displayed in the RPC console.
Disclosure of a non-hardened child key together with the corresponding ancestor xpub can compromise that ancestor's private subtree.
RPC responses, JSON/client buffers and console output may retain secrets; this RPC does not guarantee erasure of those copies.

## Arguments

**`address`** (string, required)

The wallet address

**`key_type`** (string, optional, default "wallet")

The key type: wallet or taproot\_output. Null uses the default. taproot\_output requires a Taproot address

## Return value

```text
{                                   (json object)
  "keys" : [                        (json array) Local private keys, sorted by full serialized public key
    {                               (json object)
      "pubkey" : "hex",             (string) Full SEC public key corresponding to the returned scalar
      "private_key_hex" : "hex",    (string) 32-byte unsigned big-endian private scalar (64 lowercase hex characters, including leading zeros); not WIF
      "compressed" : true|false,    (boolean) Whether the returned key uses compressed public-key serialization
      "roles" : [                   (json array) Sorted unique key roles
        "str",                      (string) script (non-Taproot), taproot_internal (raw internal key), taproot_script (leaf key), taproot_output (direct/effective output key), or musig2_participant (participant, not aggregate key)
        ...
      ]
    },
    ...
  ],
  "private_key_hex" : "hex",        (string, optional) Compatibility alias for original P2PKH/native P2WPKH wallet-mode exports
  "compressed" : true|false         (boolean, optional) Compatibility alias for original P2PKH/native P2WPKH wallet-mode exports
}
```
