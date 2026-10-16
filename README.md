Bitcoin Roots
=============

Bitcoin Roots connects to the Bitcoin peer-to-peer network to download and fully
validate blocks and transactions. It also includes a wallet and graphical user
interface, which can be optionally built.

Bitcoin Roots is based on Bitcoin Core with selected policy features from Bitcoin Knots.


Bitcoin Roots remains compatible with Bitcoin Core consensus. Its conservative
transaction relay and mempool policy is local and configurable: a transaction
rejected by policy can still be consensus-valid, and valid blocks containing it
remain acceptable. Bitcoin Roots does not enforce RDTS/BIP110 consensus rules.

See the [Roots/Core/Knots feature comparison](https://plan-b.foundation/bitcoin-roots/compare) for details.

For the selected Roots behavior implemented in the latest release, the follow-on work
that is not yet shipped, and explicit non-features, see the
[Roots feature catalog](doc/roots-features.md).

Bitcoin Core v31.1 is the latest direct base. See the [port review](doc/roots-v31-port-review.md)
for exact inputs, implementation dispositions and verification gaps.

Further information is available on the [Bitcoin Roots homepage](https://plan-b.foundation/bitcoin-roots) and in the [doc folder](/doc).

License
-------

Bitcoin Roots is released under the terms of the MIT license, same as Bitcoin Core. See
[COPYING](COPYING) for more information or https://opensource.org/license/MIT.

Development Process
-------------------

Bitcoin Roots follows Bitcoin Core development and updates a `-roots` patch to apply on every Core release. Features that improve node policy, resource control, privacy,
or operational security without changing Bitcoin consensus are maintained here.

The project distinguishes local policy from consensus. Transactions rejected by
local relay or mempool policy may still be valid under Bitcoin consensus, and
valid blocks containing them must continue to be accepted.

In every release cycle, releases sharing an exact Core base retain its
commit history and apply Roots commits on top of them, linearly. Each new major Core release usually requires minor adjustment fixes that are added as following commits and kept visible throug minor Roots releases. When moving to a new Core base, **Folding** incorporates
those adjustment fixes into the relevant thematic patches, retaining attribution and the
old releases. Each downloadable patch is the complete series for its stated
base, not just the changes since the previous Roots release. See the
[Bitcoin Roots maintainer workflow](contrib/roots/README.md#folding-at-a-new-core-base).

Testing
-------

Testing and code review are essential for this security-critical project. We extend Bitcoin Core testing with more coverage in Roots. Some tests run on each PR, others are too expensive and they run nightly, so PRs must wait at least one full day before being succesfully merged, if ever. See the [Actions log](https://github.com/LuganoPlanB/bitcoin-roots/actions) for an actual overview of the situation.

### Automated Testing

Developers are strongly encouraged to write [unit tests](src/test/README.md).
Unit tests can be run with `ctest`; further details are in
[/src/test/README.md](/src/test/README.md).

There are also [regression and integration tests](/test), written
in Python.
These tests can be run (if the [test dependencies](/test) are installed) with: `build/test/functional/test_runner.py`
(assuming `build` is your build directory).

Continuous integration builds and tests pull requests on supported platforms.
Required checks must pass before integration.

### Manual Quality Assurance (QA) Testing

Changes should be tested by somebody other than their author, especially when
they are large or high-risk.

Maintenance
-----------

Bitcoin Roots is designed, written and maintained by the Plan ₿ Foundation scientific director 
[Denis "Jaromil" Roio](https://jaromil.dyne.org), with advice and testing contributed by  
[Giacomo Zucco](https://x.com/giacomozucco) and
[S₿AM](https://x.com/sbaaaam21).

We welcome reviews and advice from Bitcoin Core developers and other experts and we are grateful for the contributions we already received, in particular by [@danielabrozzoni](https://github.com/danielabrozzoni) and (@devjoinedthechat)[https://github.com/devjoinedthechat].

