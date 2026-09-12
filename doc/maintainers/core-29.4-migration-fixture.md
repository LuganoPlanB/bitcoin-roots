# Core 29.4 migration fixture

`contrib/roots/core-29.4-migration-fixture.json` is a bounded specification of
the upstream input, not a Roots 29.4 candidate or a pre-approved merge.

The Core annotated tags resolve to v29.3 tag/commit/tree
`f2ca1bc…` / `99003b…` / `d7910b…` and v29.4
`4e70ea…` / `3fc086…` / `38ad59…`. The fixture has 39 Git-tree paths in
eleven upstream units. The supplied snapshots expose a fortieth path,
`src/clientversion.cpp`, only because exported archives expand `GIT_COMMIT_ID`.
It is recorded explicitly and must never be replayed as a source change.

Validate only against an already-populated, non-shallow Core clone:

```bash
python3 contrib/devtools/roots-294-fixture.py \
  contrib/roots/core-29.4-migration-fixture.json --repository /path/to/core
```

The validator performs no network access, checkout, patch application, or
Roots-tree mutation. A changed tag object, peeled commit, tree, path set, or
archive-only discrepancy is a fail-closed stale-fixture result. Critical and
high-risk units (validation, chainstate, LevelDB, P2P/mempool, wallet, and
build) remain manual-review inputs; a clean textual application is not an
acceptance decision.

The current forecast is deliberately an oracle, not a resolution: 14 paths
are exact applies, `src/common/netif.cpp` is provisionally absorbed, 12 are
clean textual candidates requiring their recorded gates, six are derived
manpages, and six Git-tree paths are manual-only. The snapshot-only
`src/clientversion.cpp` completes the seventh manual conflict listed by the
program plan. No outcome creates an output blob or authorizes a candidate.

Each of the seven manual records names an owner area, permitted alternatives,
forbidden outcomes, and gates. The schema deliberately rejects a selected
`resolution` at this stage. In particular, miner/mempool/P2P records require a
semantic port or explicit rejection; none can be treated as safe based on a
textual merge forecast.

The six manpages have one deferred recipe. Their only authoritative inputs are
candidate binaries, help/config output, and the post-replay candidate identity;
they must be generated after a candidate build, never hand-merged or generated
against the current Roots checkout. The provisional identity text intentionally
requires tag substitution and does not create a tag.

The acceptance contract is a future decision-ledger schema boundary. It
requires Linux, macOS, and Windows evidence, upstream and Roots tests,
wallet/database and disposable-datadir checks, package dry-runs, and generated
output freshness. Each unit's later decision is one of `applied`, `absorbed`,
`rewritten`, `rejected`, or `manual`; the fixture specifically requires proof
that RDTS/BIP110 enforcement remains absent and Roots relay policy never leaks
into block validity.
