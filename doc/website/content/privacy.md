---
title: Privacy settings
description: What other people can learn from your Bitcoin connection and your payments.
pageClass: bitcoin-roots-reading
---

# Privacy settings

Using your own node means your wallet can check your payments without asking someone else’s server about your addresses. Two other things are worth thinking about: who can see your internet address, and what your payments reveal on the blockchain.

## Who can see my connection?

When your node connects directly to another Bitcoin node, that peer can see your IP address. Bitcoin’s encrypted connections make traffic harder for someone along the way to read. The peer at the other end still knows where the connection came from.

You can route connections through Tor or I2P. Both need a service running on your computer or network. Setting up a proxy for one network can leave other connections direct. Follow the [Tor guide](/doc/tor) or [I2P guide](/doc/i2p) and check which networks your node actually uses.

## What can people see about a payment?

Bitcoin transactions are public. Give each payer a fresh receiving address. Reusing an address makes it easy to see that payments went to the same place. Spending coins together can also link payments that previously looked unrelated.

If you need to keep coins separate, the wallet lets you choose them yourself. See [wallet privacy tools](/wallet-privacy).

## What about private broadcasting?

For people submitting signed transactions with the `sendrawtransaction` command, Roots has a private broadcasting option. It uses separate, short-lived connections and requires Tor or I2P. Built-in wallet sends use their usual broadcast path. Once a transaction confirms, its details are public regardless of how it reached the network.

For configuration examples and the defaults in older releases, see the [v31.1-roots.2 settings guide](https://github.com/LuganoPlanB/bitcoin-roots/blob/aa1f4f7603/doc/website/content/privacy.md). Check your installed version before applying those settings.
