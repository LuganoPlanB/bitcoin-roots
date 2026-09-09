# Bitcoin Roots

**Connected to the trunk. Conservative in policy. Neutral in consensus.**

<img src="./src/qt/res/src/bitcoinroots-logo.svg" alt="Bitcoin Roots logo" width="300">

For an immediately usable, binary version of the Bitcoin Roots software, see
the project website: [plan-b.foundation/bitcoin-roots](https://plan-b.foundation/bitcoin-roots/).

## What is Bitcoin Roots?

We want to stick to the plan and make it easy to run a Bitcoin Core
node at home without overloading network traffic and CPU.

Bitcoin Roots connects to the Bitcoin peer-to-peer network to download and fully
validate blocks and transactions. It also includes a wallet and graphical user
interface, which can be optionally built.

Bitcoin Roots is based on Bitcoin Core and maintains selected policy features
from the Bitcoin Knots 29.3 code line. The repository records
`29.3.knots20260507` as its Knots lineage reference, not as the exact Git parent
of the initial Bitcoin Roots commit.

Bitcoin Roots follows Bitcoin Core-compatible consensus while maintaining
conservative, configurable transaction relay and mempool policy.

In particular, **Bitcoin Roots does not enforce RDTS/BIP-110** consensus rules.

Further information about Bitcoin Roots is available in the
[doc folder](/doc).

## License

Bitcoin Roots is released under the terms of the MIT license. See
[COPYING](COPYING) for more information or see
https://opensource.org/licenses/MIT.

## Development Process

Development generally takes place as part of
[Bitcoin Core](https://github.com/bitcoin/bitcoin), and suitable changes are
merged into Bitcoin Roots for each release.

Features not suitable for Bitcoin Core may still be eligible for inclusion in
Bitcoin Roots, particularly where they improve node policy, resource control,
privacy, or operation without changing Bitcoin consensus.

Selected Bitcoin Roots features may also be maintained where appropriate.

Bitcoin Roots distinguishes between consensus and policy. Transactions rejected
by local relay or mempool policy may still be valid under Bitcoin consensus, and
valid blocks containing such transactions must continue to be accepted.

The project aims to remain connected to the Bitcoin development trunk while
preserving conservative node policy and consensus neutrality.

## Testing

Testing and code review are the bottleneck for development. Please help by
testing other people's pull requests, and remember that this is security-critical
software where mistakes may cost people money.

### Automated Testing

Developers are strongly encouraged to write
[unit tests](src/test/README.md) for new code.

Unit tests can be compiled and run with:

`ctest`

Regression and integration tests are available in [/test](/test) and can be run
with:

`build/test/functional/test_runner.py`

Continuous Integration should ensure that pull requests are built and tested on
supported platforms.

### Manual Quality Assurance (QA) Testing

Changes should be tested by somebody other than the developer who wrote the
code, especially for large or high-risk changes.

### Maintenance

Bitcoin Roots is maintained by the Plan ₿ Foundation under the
supervision of [Denis "Jaromil" Roio](https://jaromil.dyne.org), [Giacomo Zucco](https://x.com/giacomozucco) and [S₿AM](https://x.com/sbaaaam21).
