---
title: "getmempoolinfo"
description: "Returns details on the active state of the TX memory pool."
rpcCategory: "blockchain"
rootsChange: "Read your node's active relay settings in the policy field."
---

[RPC reference](./) · blockchain

# getmempoolinfo

Returns details on the active state of the TX memory pool.

```sh
bitcoin-cli getmempoolinfo
```

**New or changed in Roots**

Read your node's active relay settings in the policy field.

## How it works

The policy object reports initialized local admission controls, not consensus rules or mining limits.
Controls are startup-only, except the live dust rate; this RPC does not change policy.
Settings gated by require\_standard remain visible when that gate is false. Other admission checks still apply.
All policy fields are present; clients should allow additional fields. Values share the mempool observation, not a cross-RPC snapshot.

## Arguments

No arguments.

## Return value

```text
{                                                (json object)
  "loaded" : true|false,                         (boolean) True if the initial load attempt of the persisted mempool finished
  "size" : n,                                    (numeric) Current tx count
  "bytes" : n,                                   (numeric) Sum of policy virtual transaction sizes, including witness discount, sigop and data-carrier cost adjustments
  "usage" : n,                                   (numeric) Total memory usage for the mempool
  "total_fee" : n,                               (numeric) Total fees for the mempool in BTC, ignoring modified fees through prioritisetransaction
  "maxmempool" : n,                              (numeric) Maximum memory usage for the mempool
  "mempoolminfee" : n,                           (numeric) Minimum fee rate in BTC/kvB for tx to be accepted. Is the maximum of minrelaytxfee and minimum mempool fee
  "minrelaytxfee" : n,                           (numeric) Current minimum relay fee for transactions
  "incrementalrelayfee" : n,                     (numeric) minimum fee rate increment for mempool limiting or replacement in BTC/kvB
  "unbroadcastcount" : n,                        (numeric) Current number of transactions that haven't passed initial broadcast yet
  "permitbaremultisig" : true|false,             (boolean) True if the mempool accepts transactions with bare multisig outputs
  "maxdatacarriersize" : n,                      (numeric) Maximum data-carrier bytes under the configured carrier policy
  "limitclustercount" : n,                       (numeric) Maximum number of transactions that can be in a cluster (configured by -limitclustercount)
  "limitclustersize" : n,                        (numeric) Maximum size of a cluster in virtual bytes (configured by -limitclustersize)
  "optimal" : true|false,                        (boolean) If the mempool is in a known-optimal transaction ordering
  "policy" : {                                   (json object) Initialized local mempool admission controls. Allow flags are permissions, not unconditional acceptance guarantees
    "require_standard" : true|false,             (boolean) Enable transaction/input/witness standardness checks (default: true)
    "max_script_size" : n,                       (numeric) Script policy bound in bytes under standardness; scriptSig is also capped at 1650 bytes. Zero is a zero-byte bound (default: 1650)
    "bytes_per_sigop" : n,                       (numeric) Policy virtual bytes per sigop; zero removes this size contribution (default: 20)
    "bytes_per_sigop_strict" : n,                (numeric) Accurate sigop density bound in equivalent bytes per sigop, independent of standardness; zero disables this additional check (default: 20)
    "max_tx_legacy_sigops" : n,                  (numeric) Legacy transaction sigop bound under standardness; zero is a zero-sigop bound (default: 2500)
    "datacarrier" : true|false,                  (boolean) Whether an OP_RETURN carrier budget exists; false denies carriers under standardness/full-count checking (default: true)
    "max_datacarrier_bytes" : n,                 (numeric) Aggregate serialized carrier budget in bytes; zero when disabled or explicitly set to zero (default: 83)
    "datacarrier_fullcount" : true|false,        (boolean) Include recognized input/witness carriers in the aggregate budget independently of standardness (default: true)
    "datacarrier_weight_per_byte" : n,           (numeric) Rounded-up weight units per detected data byte, adding only weight in excess of its existing weight; zero adds none (default: 4)
    "accept_nonstd_datacarrier" : true|false,    (boolean) Permit recognized nonstandard carriers independently of standardness; the full-count budget still applies (default: false)
    "permit_bare_datacarrier" : true|false,      (boolean) Permit carrier-only transactions under standardness (default: false)
    "dust_relay_fee" : n,                        (numeric) Live dust threshold rate in BTC/kvB, also used by the sub-dust fee penalty; zero makes thresholds zero (default: 0.00003000)
    "subdust_fee_penalty" : true|false,          (boolean) Subtract accumulated output dust shortfalls from policy modified fees independently of standardness (default: true)
    "permit_bare_pubkey" : true|false,           (boolean) Permit bare pubkey outputs under standardness (default: false)
    "permit_bare_multisig" : true|false,         (boolean) Permit bare multisig outputs under standardness (default: false)
    "reject_parasites" : true|false,             (boolean) Reject the CAT21 locktime marker under standardness (default: true)
    "reject_tokens" : true|false,                (boolean) Enable recognized token filters under standardness (default: false)
    "accept_unknown_witness" : true|false,       (boolean) Permit unknown witness output versions under standardness (default: true)
    "permit_bare_anchor" : true|false,           (boolean) Permit anchor-only transactions under standardness (default: true)
    "ephemeral_anchor" : true|false,             (boolean) Permit anchor outputs under standardness (default: true)
    "ephemeral_dust" : true|false,               (boolean) Permit nonzero ephemeral dust under standardness, subject to other dust/package rules (default: false)
    "ephemeral_send" : true|false,               (boolean) Permit non-anchor dust under standardness, subject to other dust/package rules (default: false)
    "replacement" : "str",                       (string) Resolved replacement mode: never, optin, or always. Opt-in also permits non-signaling TRUC/v3 replacement (default: always)
    "truc" : "str",                              (string) Resolved local v3 policy: reject, accept, or enforce (default: enforce)
    "min_relay_coinblocks" : n,                  (numeric) Minimum input value times confirmation age in satoshi-blocks; zero disables this restriction (default: 0)
    "min_relay_maturity" : n,                    (numeric) Minimum input age in blocks, measured at the next block height; zero disables this restriction (default: 0)
    "spk_reuse" : "str"                          (string) Initialized script reuse mode for mempool policy conflicts: allow or conflict (default: allow)
  }
}
```
