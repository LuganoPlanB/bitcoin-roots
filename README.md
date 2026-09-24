Bitcoin Roots
=============

Connected to the trunk. Conservative in policy. Neutral in consensus.

For an immediately usable binary release, see
[plan-b.foundation/bitcoin-roots](https://plan-b.foundation/bitcoin-roots/).

What is Bitcoin Roots?
----------------------

Bitcoin Roots connects to the Bitcoin peer-to-peer network to download and fully
validate blocks and transactions. It also includes a wallet and graphical user
interface, which can be optionally built.

Bitcoin Roots is based on Bitcoin Core with selected policy features from the
Bitcoin Knots `29.3.knots20260507` code line. The Knots reference describes
feature lineage only; Bitcoin Core v29.4 is the direct base.

Bitcoin Roots remains compatible with Bitcoin Core consensus. Its conservative
transaction relay and mempool policy is local and configurable: a transaction
rejected by policy can still be consensus-valid, and valid blocks containing it
remain acceptable. Bitcoin Roots does not enforce RDTS/BIP110 consensus rules.

For the selected Roots behavior available in this release, the follow-on work
that is not yet shipped, and explicit non-features, see the
[Roots feature catalog](doc/roots-features.md).

Further information is available in the [doc folder](/doc).

License
-------

Bitcoin Roots is released under the terms of the MIT license. See
[COPYING](COPYING) for more information or https://opensource.org/licenses/MIT.

Development Process
-------------------

Bitcoin Roots follows Bitcoin Core development and incorporates suitable changes
for each release. Features that improve node policy, resource control, privacy,
or operation without changing Bitcoin consensus may also be maintained here.

The project distinguishes local policy from consensus. Transactions rejected by
local relay or mempool policy may still be valid under Bitcoin consensus, and
valid blocks containing them must continue to be accepted.

The contribution guide is [CONTRIBUTING.md](CONTRIBUTING.md); developer guidance
is in [doc/developer-notes.md](doc/developer-notes.md).

Testing
-------

Testing and code review are essential for this security-critical project. Please
help by testing other contributors' pull requests.

### Automated Testing

Developers are strongly encouraged to write [unit tests](src/test/README.md).
Unit tests can be run with `ctest`; further details are in
[/src/test/README.md](/src/test/README.md).

There are also [regression and integration tests](/test), written
in Python.
These tests can be run (if the [test dependencies](/test) are installed) with: `build/test/functional/test_runner.py`
(assuming `build` is your build directory).

Continuous integration should ensure that pull requests are built and tested on
supported platforms.

### Manual Quality Assurance (QA) Testing

Changes should be tested by somebody other than their author, especially when
they are large or high-risk.

Maintenance
-----------

Bitcoin Roots is maintained by the Plan ₿ Foundation under the supervision of
[Denis "Jaromil" Roio](https://jaromil.dyne.org),
[Giacomo Zucco](https://x.com/giacomozucco), and
[S₿AM](https://x.com/sbaaaam21).

Translations
------------

Do not manually edit generated translations; see the
[translation process](doc/translation_process.md).
