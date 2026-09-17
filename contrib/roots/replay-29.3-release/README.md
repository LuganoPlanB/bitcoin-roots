# L7 29.3 release-specific calibration

This L7-owned replacement calibrates the immutable Knots tag
`99ee26e9df0e63a5d1e0ab6bd46b1862ce67648b` to the Roots release commit
`42098b53c57fb6818736c7b6732fa84ffe6ad391`. It does not modify or supersede
the reviewed L3 manifest. L3 classifies the first-fork layer; this replacement
is needed because the release range has 185 changed paths, including the two
release paths not owned by that L3 scope: `src/qt/res/src/bitcoinknots-logo.svg`
and `test/functional/mining_mainnet.py`.

The patch and tree locks are factual replay inputs. `manual-review-proposal.json`
is deliberately not a L6 ledger, approval, or candidate-evidence instance: its
entries await the mandatory independent L7 review before any canonical L6
record may be materialized.
