---
title: "simulaterawtransaction"
description: "Calculate the balance change resulting in the signing and broadcasting of the given transaction(s)."
rpcCategory: "wallet"
---

[RPC reference](./) · wallet

# simulaterawtransaction

Calculate the balance change resulting in the signing and broadcasting of the given transaction(s).

```sh
bitcoin-cli simulaterawtransaction ["myhex"]
```

## Requirements

- Use a loaded wallet. If you have more than one, select it with `-rpcwallet=NAME`.

## Arguments

**`rawtxs`** (array, required)

An array of hex strings of raw transactions.

**`options`** (object, optional)

<details>
<summary>Argument formats</summary>

```text
1. rawtxs          (json array, required) An array of hex strings of raw transactions.

     [
       "rawtx",    (string)
       ...
     ]
2. options         (json object, optional) Options object that can be used to pass named arguments, listed below.
```

</details>

<details>
<summary>Named arguments</summary>

```text
include_watchonly    (boolean, optional, default=false) (DEPRECATED) No longer used
```

</details>

## Return value

```text
{                          (json object)
  "balance_change" : n     (numeric) The wallet balance change (negative means decrease).
}
```
