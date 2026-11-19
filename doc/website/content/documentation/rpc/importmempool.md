---
title: "importmempool"
description: "Import a mempool.dat file and attempt to add its contents to the mempool."
rpcCategory: "blockchain"
---

[RPC reference](./) · blockchain

# importmempool

Import a mempool.dat file and attempt to add its contents to the mempool.

```sh
bitcoin-cli importmempool /path/to/mempool.dat
```

## How it works

Warning: Importing untrusted files is dangerous, especially if metadata from the file is taken over.

## Arguments

**`filepath`** (string, required)

The mempool file

**`options`** (object, optional)

<details>
<summary>Argument formats</summary>

```text
1. filepath    (string, required) The mempool file
2. options     (json object, optional) Options object that can be used to pass named arguments, listed below.
```

</details>

<details>
<summary>Named arguments</summary>

```text
use_current_time            (boolean, optional, default=true) Whether to use the current system time or use the entry time metadata from the mempool file.
                            Warning: Importing untrusted metadata may lead to unexpected issues and undesirable behavior.
apply_fee_delta_priority    (boolean, optional, default=false) Whether to apply the fee delta metadata from the mempool file.
                            It will be added to any existing fee deltas.
                            The fee delta can be set by the prioritisetransaction RPC.
                            Warning: Importing untrusted metadata may lead to unexpected issues and undesirable behavior.
                            Only set this bool if you understand what it does.
apply_unbroadcast_set       (boolean, optional, default=false) Whether to apply the unbroadcast set metadata from the mempool file.
                            Warning: Importing untrusted metadata may lead to unexpected issues and undesirable behavior.
```

</details>

## Return value

```text
{}    (empty JSON object)
```
