---
title: "getblockfrompeer"
description: "Attempt to fetch block from a given peer."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# getblockfrompeer

Attempt to fetch block from a given peer.

```sh
bitcoin-cli getblockfrompeer "00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09" 0
```

## How it works

We must have the header for this block, e.g. using submitheader.
The block will not have any undo data which can limit the usage of the block data in a context where the undo data is needed.
Subsequent calls for the same block may cause the response from the previous peer to be ignored.
Peers generally ignore requests for a stale block that they never fully verified, or one that is more than a month old.
When a peer does not respond with a block, we will disconnect.
Note: The block could be re-pruned as soon as it is received.

Returns an empty JSON object if the request was successfully scheduled.

## Arguments

**`blockhash`** (string, required)

The block hash to try to fetch

**`peer_id`** (number, required)

The peer to fetch it from (see getpeerinfo for peer IDs)

## Return value

```text
{}    (empty JSON object)
```
