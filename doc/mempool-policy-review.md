# Historical policy review and current v32 contracts

The released S27 review and its S28/S29/S30/S34/S36 follow-ups remain available
as the byte-identical [v31.1-roots.6 historical review](historical/mempool-policy-review-v31.1-roots.6.md).
Its source is the exact released commit
`e93a8cca047c12ce9cab97181dd30705ed0e7e83`, original Git blob
`1837f64c27e05e1c3749ea82fac344007fe17dca`. Embedded relative paths refer to
that original v31 document/source context; the [pinned released document](https://github.com/LuganoPlanB/bitcoin-roots/blob/v31.1-roots.6/doc/mempool-policy-review.md)
preserves those historical links. The inventory, raw-measurement context and
32,768-slot baseline are historical, not current v32 qualification or defaults.

The unpublished v32.1 candidate starts directly from Core
`f0de87f040621468b956f01f526a43a010310501`. Current behavior is described by
the [preservation catalog](roots-features.md), [port review](roots-v32-port-review.md)
and owning [policy grammar](feature-contracts/policy-parsing.md),
[effective-policy RPC](feature-contracts/effective-policy.md) and
[subdust](feature-contracts/subdust.md) contracts. Their current-source tests
and exact build/run evidence govern the port; the old review is not substituted
for them. Native v32 graph/package/replacement/HTTP interfaces remain native
where the carried contract does not require an intentional adaptation.

Admission, relay and mining-template restrictions remain local policy.
Consensus-valid blocks containing locally rejected transactions remain valid.
Extra compact-reconstruction references do not admit transactions to the
mempool or authorize relay. No RDTS/BIP110 or new consensus/privacy behavior is
introduced by restoring the review or tooling. The production cache retains
Core's 100-entry count and the Roots 10 decimal MB charged-dynamic-byte limit;
slot/object overhead, shared ownership and process RSS remain separate budgets.

The restored [offline cache experiment](../contrib/roots/cache-bench.md) preserves
the S29 multi-cell exact-Core-versus-Roots comparison, injected insertion and
InitData CPU/wall metrics, Linux process memory/CPU samples, reference snapshots,
observed reconstruction requests/hits/wire bytes and raw paired repetitions.
Only disposable exports receive instrumentation. Its explicit v31.1 and v32.1
adapters require exact source/configuration/API boundaries. Current native cache
microbenchmarks complement this experiment; they do not replace it. Whole-vector
snapshots warm the reconstruction input, and there is no uninstrumented
production latency control or mainnet representativeness claim. A bounded smoke
or a unit-test pass cannot justify a default change or qualify an unrun campaign.
