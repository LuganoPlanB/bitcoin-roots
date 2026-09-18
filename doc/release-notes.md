Bitcoin Roots 29.4
==================

Bitcoin Roots 29.4 is based on the annotated Bitcoin Core `v29.4` release,
commit `3fc0865963a38b871e9f7d94e6151c4953563516`. The Roots canonical replay
is recorded at `cbc88cff9b35b95a549c0313e424e13093fcd6a1`; the private local
frozen production source is `dfc74d403585f7c23815ef80d2e206b85c33919a`.
These are provenance records, not a public release or tag authorization.

Roots retains its conservative, configurable transaction relay and mempool
policy. It remains compatible with Bitcoin Core consensus: a transaction that
local policy rejects can still be consensus-valid, and valid blocks containing
it remain acceptable. Bitcoin Roots does not enforce RDTS/BIP110 consensus
rules.

How to Upgrade
==============

Shut down the previous node cleanly and keep a verified wallet backup before
installing the new binary. Test upgrades first with regtest and an explicit
disposable data directory. Do not treat the policy settings as consensus rules
or use them to decide whether an otherwise valid block is acceptable.

Compatibility
=============

Bitcoin Roots inherits the supported-platform, dependency, and inherited
upstream limitations of
the corresponding Bitcoin Core release and its selected Knots-derived policy
layer. See `doc/dependencies.md` and `doc/build-unix.md` for current build
requirements.

Provenance and qualification
============================

The canonical lineage, accounting, replay evidence, and release build
attestations live under `contrib/roots/`. They bind the exact source and tree
that a future authorized release workflow must qualify; they do not substitute
for independent review, CI, or release authorization.

Credits
=======

Bitcoin Roots incorporates Bitcoin Core and selected Bitcoin Knots work. See
`COPYING` and the source history for applicable attribution.
