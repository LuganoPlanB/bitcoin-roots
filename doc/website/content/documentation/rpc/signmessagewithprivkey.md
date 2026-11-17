---
title: "signmessagewithprivkey"
description: "Sign a message with a private key using the legacy compact format (for P2PKH addresses only)."
rpcCategory: "util"
---

[RPC reference](./) · util

# signmessagewithprivkey

Sign a message with a private key using the legacy compact format (for P2PKH addresses only).

```sh
bitcoin-cli signmessagewithprivkey "privkey" "my message"
```

## Arguments

**`privkey`** (string, required)

The private key to sign the message with.

**`message`** (string, required)

The message to create a signature of.

## Return value

`string`. The signature of the message encoded in base 64
