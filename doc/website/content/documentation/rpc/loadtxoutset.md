---
title: "loadtxoutset"
description: "Load the serialized UTXO set from a file."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# loadtxoutset

Load the serialized UTXO set from a file.

```sh
bitcoin-cli -rpcclienttimeout=0 loadtxoutset utxo.dat
```

## How it works

Once this snapshot is loaded, its contents will be deserialized into a second chainstate data structure, which is then used to sync to the network's tip. Meanwhile, the original chainstate will complete the initial block download process in the background, eventually validating up to the block that the snapshot is based upon.

The result is a usable bitcoind instance that is current with the network tip in a matter of minutes rather than hours. UTXO snapshot are typically obtained from third-party sources (HTTP, torrent, etc.) which is reasonable since their contents are always checked by hash.

You can find more information on this process in the \`assumeutxo\` design document (&lt;https://github.com/bitcoin/bitcoin/blob/master/doc/design/assumeutxo.md&gt;).

## Arguments

**`path`** (string, required)

path to the snapshot file. If relative, will be prefixed by datadir.

## Return value

```text
{                        (json object)
  "coins_loaded" : n,    (numeric) the number of coins loaded from the snapshot
  "tip_hash" : "hex",    (string) the hash of the base of the snapshot
  "base_height" : n,     (numeric) the height of the base of the snapshot
  "path" : "str"         (string) the absolute path that the snapshot was loaded from
}
```
