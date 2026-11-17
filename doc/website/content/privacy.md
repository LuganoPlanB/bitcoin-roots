---
title: Privacy settings
description: What other people can learn from your Bitcoin connection and your payments.
pageClass: bitcoin-roots-reading
---

# Privacy settings

When you use the Roots wallet, your own node checks for incoming payments. You don’t have to give another wallet service your addresses. Your peers can still see where you connect from, and anyone can look up a confirmed transaction on the blockchain.

## Who can see my connection?

Any peer you connect to directly can see your IP address. Roots v32.1-roots.2 uses encrypted connections with peers that support them, but encryption still leaves your IP visible to the peer.

### Connect through Tor

Start Tor on your computer. In Roots, open **Settings → Options → Network**, select **Connect through SOCKS5 proxy (default proxy)** and enter Tor’s address and port. A local Tor service usually uses `127.0.0.1` and port `9050`. Restart Roots. Its outgoing IPv4 and IPv6 peer connections will now go through Tor.

For the same setup in `bitcoin.conf`, add this line and restart Roots.

```ini
proxy=127.0.0.1:9050
```

The separate **onion services** proxy (`onion=`) applies only to onion peers. Used on its own, it leaves ordinary IPv4 and IPv6 connections direct. To choose only onion peers for automatic outgoing connections, add `onlynet=onion`. See the [Tor guide](/doc/tor) if you also want peers to reach you at an onion address.

### Connect to I2P peers

Start an I2P router and enable its **SAM bridge**, the service Roots uses to reach it. In Roots’ **Options**, choose **Open Configuration File**. Add the line below to `bitcoin.conf`, using your router’s address and port, then restart Roots.

```ini
i2psam=127.0.0.1:7656
```

This adds I2P alongside your other networks. To choose only I2P peers for automatic outgoing connections, also set `onlynet=i2p`. Incoming and manually added connections are separate. To decline incoming I2P connections, set `i2pacceptincoming=0`. [I2P setup guide](/doc/i2p)

**Check your connections.** Run `bitcoin-cli getnetworkinfo`. Under `networks`, check the `proxy` field for IPv4 and IPv6 if you set up Tor, or the `reachable` field for I2P. Run `bitcoin-cli getpeerinfo` to see which networks your current peers actually use.

## What can people see about a payment?

**Use a fresh receiving address for each payment.** If you give two people the same address, either can look it up and see both payments.

**Choose which coins to spend together.** Paying with coins from two different sources can link them on the blockchain. Open **Coin control** to review the coins before sending. [Wallet privacy tools](/wallet-privacy)

::: details Optional wallet settings
If you’ve received several payments at one address, `avoidpartialspends=1` in `bitcoin.conf` asks the wallet to spend coins from that address together. This can reduce repeated links to the address, but using more coins can raise the fee.

The wallet can also track reused addresses and avoid spending those coins where possible. To enable this on an existing wallet, replace `WALLET_NAME` with its name.

```sh
bitcoin-cli -rpcwallet=WALLET_NAME setwalletflag avoid_reuse true
```

This is a per-wallet setting. To mark addresses used before you enabled it, you’ll need to rescan the blockchain. A pruned node may need to download older blocks again.
:::

## What about private broadcasting?

If you send from the built-in wallet, it broadcasts payments to peers as usual. **Private broadcasting** is an experimental option for people submitting a signed transaction with `sendrawtransaction`. It is off by default.

With Tor or I2P configured, add `privatebroadcast=1` to `bitcoin.conf` and restart Roots. It then uses short-lived connections for these submissions, making it harder to tie a transaction to your usual peers. This requires Roots to choose its own peers and is incompatible with `connect=`.

Run `bitcoin-cli getprivatebroadcastinfo` to see pending broadcasts. A queued transaction can still be waiting for a connection. Once it confirms, the payment is public whichever route it took.
