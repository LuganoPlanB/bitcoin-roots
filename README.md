Bitcoin Roots
=============

Bitcoin Roots is a rolling patch on top of Bitcoin Core. Just like
Core, it runs a node that connects to the Bitcoin peer-to-peer network
to download and fully validate blocks and transactions. It also
includes a wallet and graphical user interface, which can be
optionally built.

Bitcoin Roots is maintained by the Plan-₿ foundation to add selected
features like increased policy knobs, BIP322, and security and privacy
features, all detailed on our website: https://plan-b.foundation/bitcoin-roots

Bitcoin Roots does not enforce RDTS/BIP110 consensus rules and remains
compatible with Bitcoin Core consensus. Its conservative transaction
relay and mempool policy is local and configurable: a transaction
rejected by policy can still be consensus-valid, and valid blocks
containing it remain acceptable.

License
-------

Bitcoin Roots is released under the terms of the MIT license, just
like Bitcoin Core. See [COPYING](COPYING) for more information or
https://opensource.org/license/MIT.

Development Process
-------------------

Bitcoin Roots development is maintained by
[Jaromil](https://jaromil.dyne.org) making use of advanced agentic
technologies. It follows Bitcoin Core development as upstream and
updates the `-roots.X` patch for each upstream release.

In every release cycle, releases sharing an exact Core base retain
their history and append fixes. When moving to a new Core base,
**Folding** incorporates those fixes into the relevant thematic
patches, retaining attribution and the old releases.

Each downloadable patch is the complete series for its stated
base, not just the changes since the previous Roots release. See the
[release and Folding policy](contrib/roots/README.md#folding-at-a-new-core-base).

To be in touch join this channel: https://t.me/bitcoinroots
