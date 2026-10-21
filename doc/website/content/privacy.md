---
title: Privacy settings
description: Bitcoin Roots privacy defaults, Tor and I2P setup, private transaction broadcasting, and wallet coin-selection controls.
pageClass: bitcoin-roots-reading
---

# Privacy settings

Bitcoin Roots gives node operators control over network routing, transaction
broadcasting, and wallet coin selection. These controls protect different things:
Tor and I2P can hide your IP address from Bitcoin peers; wallet choices can reduce
some links between your coins. Transactions and their amounts remain public on
the blockchain.

This guide describes **v31.1-roots.2**, checked on 21 October 2026. Defaults below
assume a fresh configuration and wallet. Existing configuration, GUI settings,
wallet flags, and command-line arguments can change them. Check your executable
with `bitcoin-cli --version` and its built-in help.

## Defaults at a glance

| Feature or setting | Default | How to enable or use it |
| --- | --- | --- |
| Private transaction broadcasting | Off (`privatebroadcast=0`) | Set `privatebroadcast=1` and configure Tor or I2P. Applies to `sendrawtransaction`, not wallet sends. |
| Tor proxy for outbound connections | No explicit proxy | Set `onion=127.0.0.1:9050` for onion peers, or `proxy=127.0.0.1:9050` for general proxy routing. Control-port discovery is also supported. |
| Automatic Tor onion service | On (`listenonion=1`), subject to listening configuration | Requires a reachable, authenticated Tor control endpoint; normally `127.0.0.1:9051`. |
| Tor stream isolation | On (`proxyrandomize=1`) for configured proxy connections | Keep it enabled; Tor must support SOCKS authentication isolation. |
| I2P routing | Off; no SAM endpoint configured | Set `i2psam=127.0.0.1:7656` and enable the router's SAM bridge. |
| Inbound I2P connections | On (`i2pacceptincoming=1`) when SAM is configured, subject to listening settings | Set `i2pacceptincoming=0` for outbound-only I2P. |
| Encrypted P2P transport (BIP324) | On (`v2transport=1`) | Used with compatible peers; older peers can use v1. |
| Address-reuse avoidance | Off for newly created wallets (`avoid_reuse=false`) | Enable the per-wallet `avoid_reuse` flag. |
| Forced partial-spend avoidance | Off (`avoidpartialspends=0`) | Set `avoidpartialspends=1`; also enabled for wallets with `avoid_reuse`. |
| Extra fee budget for partial-spend avoidance | Zero (`maxapsfee=0`) | Grouping can still be chosen without an additional fee. Set a reviewed budget in BTC if desired. |
| Automatic wallet broadcasting | On (`walletbroadcast=1`) | Set `walletbroadcast=0` for an explicit external/manual submission workflow. |
| Bloom-filter service for peers (BIP37) | Off (`peerbloomfilters=0`) | Usually leave off. This setting controls what your node serves to peers. |
| Local address discovery | Normally on when listening without `externalip` or `proxy` | Set `discover=0` to disable discovery of your own addresses. |
| DNS peer discovery | Normally on (`dnsseed=1`) when low on peer addresses | Set `dnsseed=0` to disable DNS seeding; arrange another way to find peers. |

Configuration examples below belong in `bitcoin.conf`, one setting per line.
Restart Roots after editing startup settings. Wallet RPC changes are made while
the node is running and apply to the selected wallet.

## Tor: explicit setup or automatic discovery

Having Tor running on its SOCKS port **9050 alone does not guarantee discovery**.
Choose one of these approaches.

### Explicit onion proxy

For private broadcasts and connections to onion peers through a local Tor daemon:

```ini
onion=127.0.0.1:9050
privatebroadcast=1
proxyrandomize=1
```

`onion` only configures routing for onion peers. Regular IPv4/IPv6 peer
connections and DNS lookups can still use your ordinary network connection.
Tor Browser commonly uses port 9150 instead; use the endpoint actually running
on your machine.

### Automatic discovery through Tor control

With `listenonion=1` and compatible listening settings, Roots attempts to connect
to Tor's control endpoint, defaulting to `127.0.0.1:9051`. Tor must have its
control port enabled, and Roots must be able to authenticate using the Tor
cookie or a configured `torpassword`.

After successful authentication, Roots asks Tor for its SOCKS listeners and
configures the onion proxy when `onion` is unset. It also creates an onion
service for incoming connections. This is control-port discovery, not a scan
for a SOCKS proxy. See the [Tor setup guide](/doc/tor) for authentication and
service configuration.

### Route regular outbound peers through Tor

For an example that restricts automatic outbound peer selection to onion peers:

```ini
proxy=127.0.0.1:9050
onlynet=onion
privatebroadcast=1
proxyrandomize=1
listen=0
discover=0
dnsseed=0
```

`proxy` supplies SOCKS routing, and `onlynet` restricts automatic outbound
connections. `onlynet` does not restrict incoming connections or necessarily
explicitly configured peers; review `addnode`, `connect`, and `seednode` too.
Here `listen=0` disables incoming connections, including automatic onion-service
creation. `dnsseed=0` disables DNS seeding, so a fresh node needs another source
of onion peer addresses. Review the Tor guide before adopting this profile.

Keep stream isolation enabled. It uses randomized SOCKS credentials to help
separate Tor streams, subject to Tor's isolation configuration. It does not
guarantee protection against timing correlation.

## I2P: configure the SAM bridge explicitly

Roots does not automatically discover a running I2P router. Enable its SAM
bridge and specify the endpoint, usually:

```ini
i2psam=127.0.0.1:7656
privatebroadcast=1
```

This permits I2P alongside other configured networks. Add `onlynet=i2p` to
restrict automatic outbound peers to I2P; that setting has the same explicit-peer
and inbound limitations described above. Set `i2pacceptincoming=0` if you only
want outbound I2P connections. See the [I2P setup guide](/doc/i2p) for router
and SAM configuration.

If both Tor and I2P are configured and available, private broadcasting can use
either. It randomly selects among eligible networks for each new private
broadcast connection. After successful outbound Tor connectivity, eligible
IPv4/IPv6 destinations may also be reached **through the Tor proxy**. This is
not direct clearnet fallback.

## Private transaction broadcasting

With `privatebroadcast=1`, transactions submitted through `sendrawtransaction`
are sent over short-lived private broadcast connections without first entering
your node's mempool. This reduces the association between a transaction and
your node's usual peer connections.

It cannot undo exposure from a transaction you already shared through ordinary
broadcasting or another service.

```sh
bitcoin-cli sendrawtransaction "SIGNED_TRANSACTION_HEX"
bitcoin-cli getprivatebroadcastinfo
```

The first command submits a real transaction. Use it only after reviewing a
fully signed transaction. The second reports pending private broadcasts.
`abortprivatebroadcast "TXID_OR_WTXID"` removes a transaction from the pending
queue; it cannot recall copies already sent to peers.

The RPC rejects submission if private broadcasting is enabled but neither
network is configured as reachable. A stopped proxy, unavailable peers, or
network disruption can still prevent delivery. Queueing is not confirmation.

Transactions sent through the built-in wallet are **not affected** by
`privatebroadcast`. It is also incompatible with `connect`, because it needs
to open new connections to randomly selected peers.

### Separate wallet preparation from submission

`walletbroadcast=0` disables automatic wallet transaction broadcasting. It does
not itself enable private broadcasting or make ordinary wallet sends private.
For a deliberate manual workflow, prepare and sign a PSBT, review it, finalize
it, and submit its signed hex with `sendrawtransaction` on the node configured
for private broadcasting. Consult the wallet RPC help for the specific workflow.
Explicit submission RPCs still broadcast when requested.

## Wallet privacy and coin selection

### Avoid address reuse

Use a fresh receiving address for each payment. The wallet's `avoid_reuse`
flag additionally tracks address reuse and changes coin selection to avoid
reused outputs where applicable. It does not erase address history or undo
links already visible on the blockchain.

Enable it for an existing wallet, replacing `WALLET_NAME`:

```sh
bitcoin-cli -rpcwallet=WALLET_NAME setwalletflag avoid_reuse true
bitcoin-cli -rpcwallet=WALLET_NAME getwalletinfo
```

Review the RPC's warning: enabling this on an existing wallet requires a
blockchain rescan to correctly mark previously used destinations. Plan that
rescan separately, especially on a pruned node where historical blocks may be
unavailable. `avoid_reuse` is a persistent per-wallet flag, not a
`bitcoin.conf` setting. For a new wallet, `createwallet` accepts
`avoid_reuse=true`.

### Avoid partial spends

```ini
avoidpartialspends=1
```

This groups outputs by address, selecting many (possibly all) or none rather
than spending them piecemeal across transactions. It can reduce repeated
address linkage but may increase input count and fees. Wallets with
`avoid_reuse` enable this behavior as well.

Even with forced grouping off, `maxapsfee=0` permits the wallet to prefer a
grouped alternative when it costs no more. `maxapsfee` is an additional
**absolute fee in BTC**, not a fee rate; raising it trades fees for grouping.
Review coin control before combining outputs from different identities or
sources: combining inputs can reveal their common ownership.

### Inspect inputs before signing

The [wallet privacy tools](/wallet-privacy) expose address reuse, grouping,
selected inputs, fees, change, and RBF state. Coin control is a manual workflow,
not a background anonymity service. Private-key sweeping is also an explicit
workflow, with a signed, non-broadcast preview by default; sweeping multiple
inputs can link them on-chain.

## Other protections and their limits

- **Encrypted P2P transport:** `v2transport=1` is on by default. BIP324 encrypts
  traffic with compatible peers to reduce passive inspection. It does not hide
  your IP from the peer or authenticate the peer's identity, and v1 connections
  remain possible.
- **DNS and discovery:** `discover=0` stops local address discovery; it does not
  hide your IP from a directly connected peer. `dnsseed=0` disables DNS seeding,
  not every possible DNS lookup. Explicit hostnames and other settings still
  matter. An onion-only proxy is not a DNS proxy.
- **Bloom filters:** `peerbloomfilters=0` is the default. Enabling this makes
  your node serve BIP37 filters to clients; it does not make your own wallet
  private. A wallet using your own full node avoids sending its address queries
  to a third-party wallet server.

## Check your running node

```sh
bitcoin-cli --version
bitcoin-cli getnetworkinfo
bitcoin-cli getpeerinfo
bitcoin-cli getprivatebroadcastinfo
bitcoin-cli -rpcwallet=WALLET_NAME getwalletinfo
```

Inspect `networks` and `proxy` in `getnetworkinfo`; inspect peer `network`,
`connection_type`, and `transport_protocol_type` in `getpeerinfo`.
`getwalletinfo` reports the selected wallet's `avoid_reuse` flag.
These are useful observations, not a complete privacy audit. Use
`bitcoin-cli help COMMAND` for each RPC's contract. Do not publish raw diagnostic
output containing peer addresses or wallet details.

## Versioned references

Defaults and behavior above were checked against the released source:

- [Node option help and setup](https://github.com/LuganoPlanB/bitcoin-roots/blob/v31.1-roots.2/src/init.cpp)
- [Network defaults and private broadcast selection](https://github.com/LuganoPlanB/bitcoin-roots/blob/v31.1-roots.2/src/net.h) and [connection implementation](https://github.com/LuganoPlanB/bitcoin-roots/blob/v31.1-roots.2/src/net.cpp)
- [Tor control discovery](https://github.com/LuganoPlanB/bitcoin-roots/blob/v31.1-roots.2/src/torcontrol.cpp)
- [Private broadcast RPCs](https://github.com/LuganoPlanB/bitcoin-roots/blob/v31.1-roots.2/src/rpc/mempool.cpp)
- [Wallet options](https://github.com/LuganoPlanB/bitcoin-roots/blob/v31.1-roots.2/src/wallet/init.cpp), [wallet defaults](https://github.com/LuganoPlanB/bitcoin-roots/blob/v31.1-roots.2/src/wallet/wallet.h), and [wallet flags](https://github.com/LuganoPlanB/bitcoin-roots/blob/v31.1-roots.2/src/wallet/rpc/wallet.cpp)

These settings do not provide an anonymity guarantee, remove public transaction
history, or add CoinJoin or payjoin. Choose controls for the information you want
to protect, and verify the configuration of the node and wallet you actually use.
