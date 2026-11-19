---
title: "getprivatebroadcastinfo"
description: "Returns information about transactions tracked for private broadcast."
rpcCategory: "rawtransactions"
---

[RPC reference](./) · rawtransactions

# getprivatebroadcastinfo

Returns information about transactions tracked for private broadcast.

```sh
bitcoin-cli getprivatebroadcastinfo
```

## How it works

Transactions that have reached the send-attempt limit remain in the result with attempts\_remaining=0.
This method is only available when running with -privatebroadcast enabled.

## Arguments

No arguments.

## Return value

```text
{                                  (json object)
  "transactions" : [               (json array)
    {                              (json object)
      "txid" : "hex",              (string) The transaction hash in hex
      "wtxid" : "hex",             (string) The transaction witness hash in hex
      "hex" : "hex",               (string) The serialized, hex-encoded transaction data
      "time_added" : xxx,          (numeric) The time this transaction was added to the private broadcast queue (seconds since epoch)
      "attempts_remaining" : n,    (numeric) The number of additional private broadcast send attempts allowed for this transaction
      "peers" : [                  (json array) Per-peer send and acknowledgment information for this transaction
        {                          (json object)
          "address" : "str",       (string) The address of the peer to which the transaction was sent
          "sent" : xxx,            (numeric) The time this transaction was picked for sending to this peer via private broadcast (seconds since epoch)
          "received" : xxx         (numeric, optional) The time this peer acknowledged reception of the transaction (seconds since epoch)
        },
        ...
      ]
    },
    ...
  ]
}
```
