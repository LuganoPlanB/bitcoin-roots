---
title: "sendrawtransaction"
description: "Submit a raw transaction (serialized, hex-encoded) to the network."
rpcCategory: "rawtransactions"
---

[RPC reference](./) · rawtransactions

# sendrawtransaction

Submit a raw transaction (serialized, hex-encoded) to the network.

```sh
bitcoin-cli sendrawtransaction "signedhex"
```

## How it works

If -privatebroadcast is disabled, then the transaction will be put into the
local mempool of the node and will be sent unconditionally to all currently
connected peers, so using sendrawtransaction for manual rebroadcast will degrade
privacy by leaking the transaction's origin, as nodes will normally not
rebroadcast non-wallet transactions already in their mempool.

If -privatebroadcast is enabled, then the transaction will be sent via
dedicated, short-lived connections to Tor or I2P peers, or to IPv4/IPv6 peers
via the Tor network. This provides best-effort concealment of the transaction's origin.
Private broadcast is experimental and may change in future releases.
Submission does not itself add the transaction to the local mempool; normal
mempool acceptance and relay apply when it is received back from the network.
The private broadcast queue is bounded: when it is full, this RPC fails and
the transaction is not scheduled until an existing one completes or is
aborted. Use getprivatebroadcastinfo to inspect the queue and abortprivatebroadcast to abort.

A specific exception, RPC\_TRANSACTION\_ALREADY\_IN\_UTXO\_SET, may throw if the transaction cannot be added to the mempool.

Related RPCs: createrawtransaction, signrawtransactionwithkey

## Arguments

**`hexstring`** (string, required)

The hex string of the raw transaction

**`maxfeerate`** (number or string, optional, default "0.10")

Reject transactions whose fee rate is higher than the specified value, expressed in BTC/kvB.
Fee rates larger than 1BTC/kvB are rejected.
Set to 0 to accept any fee rate.

**`maxburnamount`** (number or string, optional, default "0.00")

Reject transactions with provably unspendable outputs (e.g. 'datacarrier' outputs that use the OP\_RETURN opcode) greater than the specified value, expressed in BTC.
If burning funds through unspendable outputs is desired, increase this value.
This check is based on heuristics and does not guarantee spendability of outputs.

<details>
<summary>Argument formats</summary>

```text
1. hexstring        (string, required) The hex string of the raw transaction
2. maxfeerate       (numeric or string, optional, default="0.10") Reject transactions whose fee rate is higher than the specified value, expressed in BTC/kvB.
                    Fee rates larger than 1BTC/kvB are rejected.
                    Set to 0 to accept any fee rate.
3. maxburnamount    (numeric or string, optional, default="0.00") Reject transactions with provably unspendable outputs (e.g. 'datacarrier' outputs that use the OP_RETURN opcode) greater than the specified value, expressed in BTC.
                    If burning funds through unspendable outputs is desired, increase this value.
                    This check is based on heuristics and does not guarantee spendability of outputs.
```

</details>

## Return value

`string`. The transaction hash in hex
