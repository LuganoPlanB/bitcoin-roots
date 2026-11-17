---
title: "verifychain"
description: "Verifies blockchain database."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# verifychain

Verifies blockchain database.

```sh
bitcoin-cli verifychain
```

## Arguments

**`checklevel`** (number, optional, default 3, range=0-4)

How thorough the block verification is:
- level 0 reads the blocks from disk
- level 1 verifies block validity
- level 2 verifies undo data
- level 3 checks disconnection of tip blocks
- level 4 tries to reconnect the blocks
- each level includes the checks of the previous levels

**`nblocks`** (number, optional, default 6, 0=all)

The number of blocks to check.

## Return value

`boolean`. Verification finished successfully. If false, check debug log for reason.
