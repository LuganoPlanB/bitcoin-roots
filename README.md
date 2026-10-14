Bitcoin Roots
=============

Connected to the trunk. Conservative in policy. Neutral in consensus.

This is the local Core v31.1 development candidate. Publishing and release
tagging are separate from this port. See the [port review](doc/roots-v31-port-review.md)
for exact inputs, implementation dispositions and verification gaps.

What is Bitcoin Roots?
----------------------

Bitcoin Roots connects to the Bitcoin peer-to-peer network to download and fully
validate blocks and transactions. It also includes a wallet and graphical user
interface, which can be optionally built.

Bitcoin Roots is based on Bitcoin Core with selected policy features from the
Bitcoin Knots `29.3.knots20260507` code line. The Knots reference describes
feature lineage only; Bitcoin Core v31.1 is this candidate’s direct base.

Bitcoin Roots remains compatible with Bitcoin Core consensus. Its conservative
transaction relay and mempool policy is local and configurable: a transaction
rejected by policy can still be consensus-valid, and valid blocks containing it
remain acceptable. Bitcoin Roots does not enforce RDTS/BIP110 consensus rules.

For the selected Roots behavior implemented in this candidate, the follow-on work
that is not yet shipped, and explicit non-features, see the
[Roots feature catalog](doc/roots-features.md).

Further information is available in the [doc folder](/doc).

License
-------

Bitcoin Roots is released under the terms of the MIT license. See
[COPYING](COPYING) for more information or https://opensource.org/license/MIT.

Development Process
-------------------

Bitcoin Roots follows Bitcoin Core development and incorporates suitable changes
for each release. Features that improve node policy, resource control, privacy,
or operation without changing Bitcoin consensus may also be maintained here.

The project distinguishes local policy from consensus. Transactions rejected by
local relay or mempool policy may still be valid under Bitcoin consensus, and
valid blocks containing them must continue to be accepted.

In every release cycle, releases sharing an exact Core base retain their
history and append fixes. When moving to a new Core base, **Folding** incorporates
those fixes into the relevant thematic patches, retaining attribution and the
old releases. Each downloadable patch is the complete series for its stated
base, not just the changes since the previous Roots release. See the
[release and Folding policy](contrib/roots/README.md#folding-at-a-new-core-base).

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

Continuous integration builds and tests pull requests on supported platforms.
Required checks must pass before integration.

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
